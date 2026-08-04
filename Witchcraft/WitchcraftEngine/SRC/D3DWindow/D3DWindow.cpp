#include "D3DWindow.h"
#include "D3DWindowAssetHelpers.h"
#include "D3DWindowShadowHelpers.h"
#include "HELPERS/Helpers.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "ECS/WitchcraECS.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/SkinnedMeshComponent.h"
#include "ECS/Component/SkinningRuntimeComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "Common/SkinningSharedTypes.h"
#include "System/WitchcraftFile/WMaterialFile.h"
#include "System/WitchcraftFile/WModelFile.h"
#include "../String/SStringUtils.h"
#include "Editor/Editor.h"
#include "D3DWindowStaticHelpers.h"
#include "D3DWindowGeometry.h"

FrameResource::FrameResource()
{

}

FrameResource::~FrameResource()
{

}

void FrameResource::Create(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	if(materialCount > 0)
		MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
	if (objectCount > 0)
	{
		ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
		SkinningCB = std::make_unique<UploadBuffer<SkinningConstants>>(device, objectCount, true);
	}
	VolumetricLightObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, 256, true);
	LightCB = std::make_unique<UploadBuffer<LightConstants>>(device, 1, true);
	// SSAO 使用 root CBV 直接指向 b0。为规避部分驱动对小常量缓冲的越界预取，
	// 这里给 AO 常量多分配几个 256B 段作为冗余空间。
	AOCB = std::make_unique<UploadBuffer<AOConstants>>(device, 4, true);
	PostProcessCB = std::make_unique<UploadBuffer<PostProcessConstants>>(device, 1, true);
}

D3DWindow* D3DWindow::s_app = nullptr;

std::vector<RenderItem*> D3DWindow::CollectRenderItems(const std::map<std::wstring, RenderItem*>& renderItemMap)
{
	std::vector<RenderItem*> renderItems;
	renderItems.reserve(renderItemMap.size());
	for (const auto& pair : renderItemMap)
	{
		if (pair.second != nullptr)
			renderItems.push_back(pair.second);
	}

	return renderItems;
}

ID3D12PipelineState* D3DWindow::ResolvePipelineStateForRenderItem(
	const RenderItem* renderItem,
	ID3D12PipelineState* defaultPipelineState,
	UINT pipelineNumber) const
{
	if (renderItem == nullptr || renderItem->Geo == nullptr)
		return defaultPipelineState;

	const UINT vertexStride = renderItem->Geo->VertexByteStride != 0
		? renderItem->Geo->VertexByteStride
		: renderItem->Geo->vertexBufferView.StrideInBytes;
	const bool usesSkinnedVertexLayout = vertexStride == sizeof(Witchcraft::Animation::SkinnedVertex);
	// 过渡期仍沿用 skinned 专用 PSO；等 compute pre-skinning 输出变形后的常规 VB 后，
	// 这里可以逐步退回到 defaultPipelineState，从而收缩整套 skinned shader 变体。
	if (renderItem->IsSkinned && renderItem->SourceEntity != nullptr)
	{
		if (kEnableComputeSkinning)
		{
			SceneEntityBase* runtimeOwnerEntity =
				ResolveSkinningRuntimeOwnerEntity(mLastExternalECS, renderItem->SourceEntity);
			const auto deformedEntryIt = mSkinnedDeformCache.find(runtimeOwnerEntity);
			if (deformedEntryIt != mSkinnedDeformCache.end())
			{
				const SkinnedDeformCacheEntry& deformedEntry = deformedEntryIt->second;
				if (deformedEntry.GpuResourcesReady &&
					CurrBackBufferIndex < deformedEntry.DeformedVertexBuffers.size() &&
					deformedEntry.DeformedVertexBuffers[CurrBackBufferIndex] != nullptr)
				{
					return defaultPipelineState;
				}
			}
		}
	}
	if (!usesSkinnedVertexLayout)
		return defaultPipelineState;

	switch (pipelineNumber)
	{
	case 不透明物体管道:
		return SkinnedOpaquePipelineState != nullptr ? SkinnedOpaquePipelineState.Get() : defaultPipelineState;
	case 半透明物体管道:
		return SkinnedTransparentNearOpaquePipelineState != nullptr ? SkinnedTransparentNearOpaquePipelineState.Get() : defaultPipelineState;
	case 透明物体管道:
		return SkinnedTransparentPipelineState != nullptr ? SkinnedTransparentPipelineState.Get() : defaultPipelineState;
	case 阴影管道:
		return SkinnedShadowPipelineState != nullptr ? SkinnedShadowPipelineState.Get() : defaultPipelineState;
	case 法线绘制管道:
		return SkinnedNormalPipelineState != nullptr ? SkinnedNormalPipelineState.Get() : defaultPipelineState;
	case InteractionOutlinePass::InteractionPipelineTag:
		return interactionOutlinePass.GetSkinnedInteractionPipelineState() != nullptr
			? interactionOutlinePass.GetSkinnedInteractionPipelineState()
			: defaultPipelineState;
	case InteractionOutlinePass::OutlinePipelineTag:
		return interactionOutlinePass.GetSkinnedOutlinePipelineState() != nullptr
			? interactionOutlinePass.GetSkinnedOutlinePipelineState()
			: defaultPipelineState;
	default:
		return defaultPipelineState;
	}
}

DirectX::BoundingBox D3DWindow::BuildRelaxedFrustumCullingBounds(const DirectX::BoundingBox& worldBounds)
{
	DirectX::BoundingBox relaxedCullingBounds = worldBounds;
	const float maxExtent = (std::max)(worldBounds.Extents.x, (std::max)(worldBounds.Extents.y, worldBounds.Extents.z));
	const float cullingPadding = 0.10f + maxExtent * 0.30f;
	relaxedCullingBounds.Extents.x += cullingPadding;
	relaxedCullingBounds.Extents.y += cullingPadding;
	relaxedCullingBounds.Extents.z += cullingPadding;
	return relaxedCullingBounds;
}

bool D3DWindow::DoesCullingBoundsIntersectFrustum(
	const DirectX::BoundingBox& worldBounds,
	const DirectX::BoundingFrustum& worldFrustum)
{
	return worldFrustum.Intersects(BuildRelaxedFrustumCullingBounds(worldBounds));
}

bool D3DWindow::TryBuildEntityWorldCullingBounds(
	SceneEntityBase* entity,
	WitchcraECS* ecs,
	DirectX::BoundingBox* outWorldBounds) const
{
	if (entity == nullptr || ecs == nullptr || outWorldBounds == nullptr)
		return false;

	TransformComponent* transformComponent = ecs->GetComponent<TransformComponent>(entity);
	if (transformComponent == nullptr)
		return false;

	DirectX::XMFLOAT4X4 worldTransform = MathHelps::Identity;
	DirectX::XMFLOAT4X4 texTransform = MathHelps::Identity;
	if (!BuildEntityRenderTransforms(entity, ecs, &worldTransform, &texTransform))
		return false;

	const DirectX::BoundingBox localBounds = transformComponent->GetBoundingBox();
	localBounds.Transform(*outWorldBounds, DirectX::XMLoadFloat4x4(&worldTransform));
	return true;
}

bool D3DWindow::IsEntitySubtreeVisibleInFrustum(
	SceneEntityBase* entity,
	WitchcraECS* ecs,
	const DirectX::BoundingFrustum& worldFrustum,
	std::unordered_set<SceneEntityBase*>& visitedEntities,
	bool* outAnyCullingBounds) const
{
	if (entity == nullptr || ecs == nullptr)
		return false;
	if (!visitedEntities.insert(entity).second)
		return false;
	if (!ecs->IsEntityVisible(entity))
		return false;

	DirectX::BoundingBox worldBounds{};
	if (TryBuildEntityWorldCullingBounds(entity, ecs, &worldBounds))
	{
		if (outAnyCullingBounds != nullptr)
			*outAnyCullingBounds = true;
		if (DoesCullingBoundsIntersectFrustum(worldBounds, worldFrustum))
			return true;
	}

	SceneEntityType sceneType = SceneEntityType::StaticScenery;
	(void)ecs->GetEntitySceneType(entity, &sceneType);
	if (sceneType == SceneEntityType::Interactive)
		return false;

	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
	{
		if (IsEntitySubtreeVisibleInFrustum(childEntity, ecs, worldFrustum, visitedEntities, outAnyCullingBounds))
			return true;
	}

	return false;
}

bool D3DWindow::ShouldCullRenderItemByMainCameraFrustum(
	const RenderItem& renderItem,
	const DirectX::BoundingFrustum& worldFrustum) const
{
	if (renderItem.DisableFrustumCulling || renderItem.IsSkinned || renderItem.IsBillboard)
		return false;

	bool hasAnyCullingBounds = false;
	if (renderItem.HasLocalBounds)
	{
		DirectX::BoundingBox worldBounds;
		renderItem.LocalBounds.Transform(worldBounds, DirectX::XMLoadFloat4x4(&renderItem.WorldTransform));
		hasAnyCullingBounds = true;
		if (DoesCullingBoundsIntersectFrustum(worldBounds, worldFrustum))
			return false;
	}

	SceneEntityBase* cullingRootEntity =
		renderItem.CullingSourceEntity != nullptr ? renderItem.CullingSourceEntity : renderItem.SourceEntity;
	if (mLastExternalECS == nullptr || cullingRootEntity == nullptr)
		return hasAnyCullingBounds;

	SceneEntityType cullingRootSceneType = renderItem.SceneType;
	(void)mLastExternalECS->GetEntitySceneType(cullingRootEntity, &cullingRootSceneType);
	if (cullingRootSceneType == SceneEntityType::Interactive)
	{
		if (!hasAnyCullingBounds)
		{
			DirectX::BoundingBox rootWorldBounds{};
			if (TryBuildEntityWorldCullingBounds(cullingRootEntity, mLastExternalECS, &rootWorldBounds))
			{
				hasAnyCullingBounds = true;
				if (DoesCullingBoundsIntersectFrustum(rootWorldBounds, worldFrustum))
					return false;
			}
		}

		return hasAnyCullingBounds;
	}

	std::unordered_set<SceneEntityBase*> visitedEntities;
	bool hierarchyHasAnyCullingBounds = false;
	if (IsEntitySubtreeVisibleInFrustum(
		cullingRootEntity,
		mLastExternalECS,
		worldFrustum,
		visitedEntities,
		&hierarchyHasAnyCullingBounds))
	{
		return false;
	}

	return hasAnyCullingBounds || hierarchyHasAnyCullingBounds;
}

bool D3DWindow::AreMatrixElementsNearlyEqual(const DirectX::XMFLOAT4X4& lhs, const DirectX::XMFLOAT4X4& rhs, float epsilon)
{
	for (UINT rowIndex = 0; rowIndex < 4; ++rowIndex)
	{
		for (UINT columnIndex = 0; columnIndex < 4; ++columnIndex)
		{
			if (std::abs(lhs.m[rowIndex][columnIndex] - rhs.m[rowIndex][columnIndex]) > epsilon)
				return false;
		}
	}
	return true;
}

bool D3DWindow::CompareShadowCasterEntryByName(
	const std::pair<std::wstring, const RenderItem*>& lhs,
	const std::pair<std::wstring, const RenderItem*>& rhs)
{
	return lhs.first < rhs.first;
}

bool D3DWindow::StaticShadowCacheResourcesMatchLayout(
	const std::vector<ShadowCache2DResource>& staticShadowCache2DResources,
	const std::vector<UINT>& texture2DSizes,
	const std::vector<ShadowCacheCubeResource>& staticPointLightShadowCubeResources,
	const std::vector<UINT>& pointLightCubeSizes)
{
	if (staticShadowCache2DResources.size() != texture2DSizes.size())
		return false;
	if (staticPointLightShadowCubeResources.size() != pointLightCubeSizes.size())
		return false;

	for (UINT slotIndex = 0; slotIndex < texture2DSizes.size(); ++slotIndex)
	{
		const ShadowCache2DResource& cacheResource = staticShadowCache2DResources[slotIndex];
		if (cacheResource.Resource == nullptr ||
			cacheResource.Width != texture2DSizes[slotIndex] ||
			cacheResource.Height != texture2DSizes[slotIndex])
		{
			return false;
		}
	}

	for (UINT cubeIndex = 0; cubeIndex < pointLightCubeSizes.size(); ++cubeIndex)
	{
		const ShadowCacheCubeResource& cacheResource = staticPointLightShadowCubeResources[cubeIndex];
		if (cacheResource.Resource == nullptr ||
			cacheResource.FaceSize != pointLightCubeSizes[cubeIndex])
		{
			return false;
		}
	}

	return true;
}

void D3DWindow::DrawOutlinePassItems(
	ID3D12GraphicsCommandList* cmdList,
	ID3D12PipelineState* pipelineState,
	UINT pipelineTag,
	const std::vector<RenderItem*>& selectedOutlineItems)
{
	ComPtr<ID3D12PipelineState> pipelineRef = pipelineState;
	DrawRenderItems(cmdList, selectedOutlineItems, pipelineRef, pipelineTag);
}

D3D12_GPU_VIRTUAL_ADDRESS D3DWindow::PrepareVolumetricLightDrawObjectCB(
	UploadBuffer<ObjectConstants>* volumetricObjectCB,
	D3D12_GPU_VIRTUAL_ADDRESS objectCBBaseAddress,
	UINT objectCBByteSize,
	UINT drawIndex,
	UINT lightIndex)
{
	if (volumetricObjectCB == nullptr)
		return 0;

	ObjectConstants objectConstants = {};
	objectConstants.WorldTransform = MathHelps::Identity;
	objectConstants.TexTransform = MathHelps::Identity;
	// 用对象常量的 TexTransform.x 传递 light index，供体积光 shader 读取对应灯数据。
	objectConstants.TexTransform._11 = static_cast<float>(lightIndex);
	volumetricObjectCB->CopyData(static_cast<int>(drawIndex), objectConstants);

	return objectCBBaseAddress + static_cast<UINT64>(drawIndex) * objectCBByteSize;
}

DirectX::XMFLOAT4X4 D3DWindow::BuildBillboardWorldTransform(
	const DirectX::XMFLOAT4X4& anchorTransform,
	const BillboardData& billboard,
	const DirectX::XMFLOAT3& cameraPosition,
	const DirectX::XMMATRIX& viewMatrix,
	const DirectX::XMMATRIX& projMatrix,
	float renderTargetHeight)
{
	using namespace DirectX;

	const XMMATRIX anchor = XMLoadFloat4x4(&anchorTransform);
	XMVECTOR center = XMVectorSet(anchorTransform._41, anchorTransform._42, anchorTransform._43, 1.0f);
	center += XMVector3TransformNormal(XMLoadFloat3(&billboard.Offset), anchor);

	XMVECTOR cameraPos = XMLoadFloat3(&cameraPosition);
	XMVECTOR toCamera = cameraPos - center;
	if (XMVectorGetX(XMVector3LengthSq(toCamera)) <= 1e-8f)
		toCamera = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	toCamera = XMVector3Normalize(toCamera);

	XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR right = XMVectorZero();
	XMVECTOR up = XMVectorZero();

	if (billboard.FacingMode == BillboardFacingMode::YAxisOnly)
	{
		XMVECTOR flatForward = XMVectorSet(XMVectorGetX(toCamera), 0.0f, XMVectorGetZ(toCamera), 0.0f);
		if (XMVectorGetX(XMVector3LengthSq(flatForward)) <= 1e-8f)
			flatForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		flatForward = XMVector3Normalize(flatForward);
		right = XMVector3Normalize(XMVector3Cross(worldUp, flatForward));
		up = worldUp;
	}
	else
	{
		right = XMVector3Cross(worldUp, toCamera);
		if (XMVectorGetX(XMVector3LengthSq(right)) <= 1e-8f)
			right = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
		right = XMVector3Normalize(right);
		up = XMVector3Normalize(XMVector3Cross(toCamera, right));
	}

	float halfWidth = (std::max)(billboard.Width, 0.001f) * 0.5f;
	float halfHeight = (std::max)(billboard.Height, 0.001f) * 0.5f;
	if (billboard.Mode == BillboardMode::ScreenSize)
	{
		const float viewDepth = std::abs(XMVectorGetZ(XMVector3TransformCoord(center, viewMatrix)));
		const float safeTargetHeight = (std::max)(renderTargetHeight, 1.0f);
		const float projY = (std::max)(XMVectorGetY(projMatrix.r[1]), 1e-4f);
		const float ndcHalfHeight = (std::max)(billboard.ScreenSize, 1.0f) / safeTargetHeight;
		halfHeight = viewDepth * ndcHalfHeight / projY;
		halfWidth = halfHeight * ((std::max)(billboard.Width, 0.001f) / (std::max)(billboard.Height, 0.001f));
	}

	XMMATRIX scale = XMMatrixScaling(halfWidth * 2.0f, halfHeight * 2.0f, 1.0f);
	XMMATRIX rotation = XMMATRIX(
		XMVectorSet(XMVectorGetX(right), XMVectorGetY(right), XMVectorGetZ(right), 0.0f),
		XMVectorSet(XMVectorGetX(up), XMVectorGetY(up), XMVectorGetZ(up), 0.0f),
		XMVectorSet(XMVectorGetX(toCamera), XMVectorGetY(toCamera), XMVectorGetZ(toCamera), 0.0f),
		XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));
	XMMATRIX translation = XMMatrixTranslationFromVector(center);

	XMFLOAT4X4 outWorld = MathHelps::Identity;
	XMStoreFloat4x4(&outWorld, scale * rotation * translation);
	return outWorld;
}

HANDLE D3DWindow::CreateAutoResetEventHandle()
{
	return CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

HANDLE D3DWindow::CreateManualResetEventHandle()
{
	return CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

bool D3DWindow::CompareRenderItemPairByName(const std::pair<std::wstring, RenderItem*>& lhs, const std::pair<std::wstring, RenderItem*>& rhs)
{
	return lhs.first < rhs.first;
}

void D3DWindow::SplitRenderItemsToThreadBatches(
	std::vector<std::pair<std::wstring, RenderItem*>>& items,
	std::vector<RenderItem*> (&targetBatches)[NumContexts])
{
	std::sort(items.begin(), items.end(), CompareRenderItemPairByName);

	size_t itemIndex = 0;
	const size_t baseBatchSize = items.size() / NumContexts;
	const size_t remainder = items.size() % NumContexts;

	for (UINT threadIndex = 0; threadIndex < NumContexts; ++threadIndex)
	{
		const size_t batchSize = baseBatchSize + (threadIndex < remainder ? 1u : 0u);
		auto& batch = targetBatches[threadIndex];
		batch.reserve(batchSize);
		for (size_t i = 0; i < batchSize; ++i)
		{
			batch.push_back(items[itemIndex++].second);
		}
	}
}

void D3DWindow::BuildCameraFrustumCornersWorldSpace(const DirectX::XMMATRIX& invViewProj, DirectX::XMFLOAT3 outCorners[8])
{
	static const DirectX::XMFLOAT3 ndcCorners[8] =
	{
		{ -1.0f, -1.0f, 0.0f }, // near
		{ -1.0f, +1.0f, 0.0f },
		{ +1.0f, +1.0f, 0.0f },
		{ +1.0f, -1.0f, 0.0f },
		{ -1.0f, -1.0f, 1.0f }, // far
		{ -1.0f, +1.0f, 1.0f },
		{ +1.0f, +1.0f, 1.0f },
		{ +1.0f, -1.0f, 1.0f }
	};

	for (UINT i = 0; i < 8; ++i)
	{
		const DirectX::XMVECTOR cornerH = DirectX::XMVectorSet(ndcCorners[i].x, ndcCorners[i].y, ndcCorners[i].z, 1.0f);
		const DirectX::XMVECTOR cornerW = DirectX::XMVector3TransformCoord(cornerH, invViewProj);
		DirectX::XMStoreFloat3(&outCorners[i], cornerW);
	}
}

bool D3DWindow::TryReserveSrvDescriptorSlots(UINT count, const wchar_t* context, UINT* outBaseIndex)
{
	if (outBaseIndex != nullptr)
		*outBaseIndex = SrvDescriptorHeapIndex;

	if (count == 0)
		return true;

	if (SrvDescriptorHeapCapacity == 0 ||
		SrvDescriptorHeapIndex > SrvDescriptorHeapCapacity ||
		count > (SrvDescriptorHeapCapacity - SrvDescriptorHeapIndex))
	{
		const std::wstring contextText = (context != nullptr) ? context : L"UnknownContext";
		const std::wstring debugText =
			L"[D3DWindow] SRV descriptor heap capacity exceeded. context=" + contextText +
			L", request=" + std::to_wstring(count) +
			L", used=" + std::to_wstring(SrvDescriptorHeapIndex) +
			L", capacity=" + std::to_wstring(SrvDescriptorHeapCapacity) + L"\n";
		EngineHelpers::AddLog(debugText.c_str());
		return false;
	}

	if (outBaseIndex != nullptr)
		*outBaseIndex = SrvDescriptorHeapIndex;
	SrvDescriptorHeapIndex += count;
	return true;
}

UINT64 D3DWindow::ComputeDeferredReleaseFence() const
{
	// 保守策略：至少跨过交换链缓冲区数量的信号点，避免“本帧替换、下帧仍被引用”的释放竞态。
	return fenceValue + static_cast<UINT64>(SwapChainBufferCount) + 1ull;
}

void D3DWindow::DrainDeferredReleasesByCompletedFence()
{
	const UINT64 completedFence =
		(fence != nullptr) ? fence->GetCompletedValue() : ~static_cast<UINT64>(0);

	if (!DeferredReleaseGeometries.empty())
	{
		size_t writeIndex = 0;
		for (size_t readIndex = 0; readIndex < DeferredReleaseGeometries.size(); ++readIndex)
		{
			if (DeferredReleaseGeometries[readIndex].SafeFence > completedFence)
			{
				if (writeIndex != readIndex)
					DeferredReleaseGeometries[writeIndex] = std::move(DeferredReleaseGeometries[readIndex]);
				++writeIndex;
			}
		}
		DeferredReleaseGeometries.resize(writeIndex);
	}

	if (!DeferredReleasePipelineStates.empty())
	{
		size_t writeIndex = 0;
		for (size_t readIndex = 0; readIndex < DeferredReleasePipelineStates.size(); ++readIndex)
		{
			if (DeferredReleasePipelineStates[readIndex].SafeFence > completedFence)
			{
				if (writeIndex != readIndex)
					DeferredReleasePipelineStates[writeIndex] = std::move(DeferredReleasePipelineStates[readIndex]);
				++writeIndex;
			}
		}
		DeferredReleasePipelineStates.resize(writeIndex);
	}
}

D3DWindow::D3DWindow()
{
	s_app = this;

	BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; //DXGI_FORMAT_R16G16B16A16_FLOAT;//
	DepthStencilFormat = DXGI_FORMAT_D32_FLOAT;
	MainPassCB.ShadowSettings = { ShadowConfig.DefaultOpacity, ShadowConfig.DefaultSoftness };
	MainPassCB.AOSettings = { AOConfig.Enabled ? 1.0f : 0.0f, AOConfig.Strength };
	MainPassCB.ShadowMaskSettings = {
		(ShadowMaskConfig.Enabled && ShadowMaskConfig.UseInMainPbr && AOConfig.Enabled) ? 1.0f : 0.0f,
		ShadowMaskConfig.Cascade0BlurRadius,
		ShadowMaskConfig.Cascade1BlurRadius,
		ShadowMaskConfig.Cascade2BlurRadius
	};
}

D3DWindow::~D3DWindow()
{
	if (d3dDevice != nullptr)
		FlushCommandQueue();

	// 关闭线程事件和线程句柄。
	for (int threadIndex = 0; threadIndex < NumContexts; ++threadIndex)
	{
		for (UINT workerPhaseIndex = 0; workerPhaseIndex < 工作阶段计数; ++workerPhaseIndex)
		{
			CloseHandle(workerBeginRecordCommand[workerPhaseIndex][threadIndex]);
		}
		CloseHandle(workerFinishedRecordCommand[threadIndex]);
		CloseHandle(threadHandles[threadIndex]);
	}

	if (fenceEvent != nullptr)
	{
		CloseHandle(fenceEvent);
		fenceEvent = nullptr;
	}

	s_app = nullptr;
}

bool D3DWindow::Create(HWND hWnd, Timer* timer, Editor* editor)
{
	WinInfo.m_hWnd = hWnd;
	mTimer = timer;
	mEditor = editor;

	// 先建立设备、交换链与描述符堆，后续资源创建都依赖这些基础对象。
	CreateDevice();
	CreateCommandQueueAndSwapChain();
	CreateDescriptorHeaps();

	// 从关闭状态开始。
	// 这是因为第一次引用命令列表时，我们将其重置，并且需要在调用Reset之前将其关闭。
	CloseCommandList();

	//进行初始大小调整代码。
	sharedNormalPrepass.Initialize(d3dDevice.Get());
	ambientOcclusion.Initialize(d3dDevice.Get());
	mDirectionalShadowMaskPass.Initialize(d3dDevice.Get());
	interactionOutlinePass.Initialize(d3dDevice.Get());
	mSkinWeightVizPass.Initialize(d3dDevice.Get());
	volumetricLightPass.Initialize(d3dDevice.Get());
	mOITCompositePass.Initialize(d3dDevice.Get());
	mFXAAPass.Initialize(d3dDevice.Get());
	mSkeletonOverlayPass.Initialize(d3dDevice.Get());
	mGizmoPass.Initialize(d3dDevice.Get());
	mSkinningComputePass.Initialize(d3dDevice.Get());
	OnResize();
	shadowMapPass.Initialize(d3dDevice.Get(), MainCommandList.Get(), ShadowConfig.ShadowMapSize, ShadowConfig.ShadowMapSize);
	pointLightShadowCubePool.Create(d3dDevice.Get(), ShadowConfig.ShadowMapSize);
	ambientOcclusion.BuildOffsetVectors();

	// OnResize 内部会 CloseCommandListAndSynchronize，返回时主命令列表处于关闭状态。
	// 这里先 Reset，再录制随机向量纹理上传命令，避免 COMMAND_LIST_CLOSED 调试层报错。
	ResetCommandList();
	ambientOcclusion.BuildRandomVectorTexture(MainCommandList.Get());

	mCamera.SetPosition(0.0f, 5.0f, -15.0f);

	textR = new TextRenderPass(d3dDevice.Get(), MainCommandList.Get(), SwapChainBufferCount);
	textR->SetScreenSize(static_cast<float>(WinInfo.Width), static_cast<float>(WinInfo.Height));

	CreateRootSignature();
	CreatePipesAndShaders();
	AddShapeGeometry();
	AddBillboardGeometry();
	AddTransformGizmoGeometry();
	CreateSRVDescriptorHeap();
	// 预留一个“空白 2D 纹理”SRV（nullptr 资源），作为所有贴图失效场景的统一兜底。
	// 这样即使场景没有天空，或天空索引异常，也不会落到未初始化描述符。
	if (!TryReserveSrvDescriptorSlots(1, L"NullTexture Reserved SRV", &NullTextureHeapIndex))
		return false;
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nullTextureSrvDesc = {};
		nullTextureSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullTextureSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullTextureSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullTextureSrvDesc.Texture2D.MostDetailedMip = 0;
		nullTextureSrvDesc.Texture2D.MipLevels = 1;
		nullTextureSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		nullTextureSrvDesc.Texture2D.PlaneSlice = 0;
		CD3DX12_CPU_DESCRIPTOR_HANDLE nullTextureSrvHandle(
			SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			NullTextureHeapIndex,
			CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(nullptr, &nullTextureSrvDesc, nullTextureSrvHandle);
		SkyTexHeapIndex = NullTextureHeapIndex;
		SkyMapIndex = NullTextureHeapIndex;
	}
	LoadTextures();
	textR->SetSharedSrvDescriptorHeap(SrvDescriptorHeap.Get(), CbvSrvUavDescriptorSize, SrvDescriptorHeapIndex, 64u);
	if (!TryReserveSrvDescriptorSlots(textR->GetSrvDescriptorCount(), L"TextRender Reserved SRV Range"))
		return false;

	if (!textR->DXCreateFont(L"DATA\\Fonts\\STXIHEI.TTF", 34))
		return false;

	BuildMaterials();
	BuildLight();

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvCPUHandle(SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	if (!TryReserveSrvDescriptorSlots(
		ShadowPoolLimits::Combined2DTextureCount + ShadowPoolLimits::PointCubeTextureCount,
		L"ShadowMap Reserved SRV Range",
		&ShadowMapHeapStartIndex))
		return false;
	D3D12_SHADER_RESOURCE_VIEW_DESC nullShadowSrvDesc = {};
	nullShadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullShadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	nullShadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullShadowSrvDesc.Texture2D.MostDetailedMip = 0;
	nullShadowSrvDesc.Texture2D.MipLevels = 1;
	nullShadowSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	nullShadowSrvDesc.Texture2D.PlaneSlice = 0;
	for (UINT shadowIndex = 0; shadowIndex < ShadowPoolLimits::Combined2DTextureCount; ++shadowIndex)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE reservedShadowSrv(srvCPUHandle, ShadowMapHeapStartIndex + shadowIndex, CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(nullptr, &nullShadowSrvDesc, reservedShadowSrv);
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC nullCubeSrvDesc = {};
	nullCubeSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullCubeSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	nullCubeSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	nullCubeSrvDesc.TextureCube.MostDetailedMip = 0;
	nullCubeSrvDesc.TextureCube.MipLevels = 1;
	nullCubeSrvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	for (UINT cubeIndex = 0; cubeIndex < ShadowPoolLimits::PointCubeTextureCount; ++cubeIndex)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE reservedCubeSrv(
			srvCPUHandle,
			ShadowMapHeapStartIndex + ShadowPoolLimits::Combined2DTextureCount + cubeIndex,
			CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(nullptr, &nullCubeSrvDesc, reservedCubeSrv);
	}
	// DSV 布局：
	// 0: 主深度
	// 1..(MaxShadowMapCount * 6): 阴影 DSV 预留区（点光 cubemap 需要 6 个面）
	// 最后 1 个: SharedNormalPrepass 深度
	SharedNormalPrepassDepthDsvIndex = 1 + ShadowConfig.MaxShadowMapCount * 6;
	CD3DX12_CPU_DESCRIPTOR_HANDLE sharedNormalPrepassRtvHandle(
		RtvHeap->GetCPUDescriptorHandleForHeapStart(),
		SwapChainBufferCount,
		RtvDescriptorSize);
	if (!TryReserveSrvDescriptorSlots(2, L"SharedNormalPrepass Reserved SRV Range", &SharedNormalPrepassHeapStartIndex))
		return false;
	sharedNormalPrepass.BuildDescriptors(
		GetCpuSrv().Offset(SharedNormalPrepassHeapStartIndex, CbvSrvUavDescriptorSize),
		GetGpuSrv().Offset(SharedNormalPrepassHeapStartIndex, CbvSrvUavDescriptorSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(DsvHeap->GetCPUDescriptorHandleForHeapStart(), SharedNormalPrepassDepthDsvIndex, DsvDescriptorSize),
		sharedNormalPrepassRtvHandle,
		CbvSrvUavDescriptorSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE ambientOcclusionRtvHandle(
		RtvHeap->GetCPUDescriptorHandleForHeapStart(),
		SwapChainBufferCount + 1,
		RtvDescriptorSize);
	if (!TryReserveSrvDescriptorSlots(3, L"AmbientOcclusion Reserved SRV Range", &AmbientOcclusionHeapStartIndex))
		return false;
	ambientOcclusion.BuildDescriptors(
		GetCpuSrv().Offset(AmbientOcclusionHeapStartIndex, CbvSrvUavDescriptorSize),
		GetGpuSrv().Offset(AmbientOcclusionHeapStartIndex, CbvSrvUavDescriptorSize),
		ambientOcclusionRtvHandle,
		CbvSrvUavDescriptorSize,
		RtvDescriptorSize);

	DirectionalShadowMaskRtvStartIndex =
		SwapChainBufferCount + 3 + SwapChainBufferCount * 2 + SwapChainBufferCount + SwapChainBufferCount;
	if (!TryReserveSrvDescriptorSlots(2, L"DirectionalShadowMask Reserved SRV Range", &DirectionalShadowMaskHeapStartIndex))
		return false;
	DirectionalShadowMaskDescriptorsInitialized = true;
	BuildDirectionalShadowMaskDescriptors();

	// 后处理场景颜色 SRV：每个后备缓冲对应一个输入纹理。
	if (!TryReserveSrvDescriptorSlots(SwapChainBufferCount, L"PostProcessSceneColor Reserved SRV Range", &PostProcessSceneColorHeapStartIndex))
		return false;
	PostProcessSceneColorDescriptorsInitialized = true;
	BuildPostProcessSceneColorDescriptors();

	// 交互描边遮罩 SRV：每个后备缓冲对应一个遮罩纹理。
	if (!TryReserveSrvDescriptorSlots(SwapChainBufferCount, L"InteractionOutlineMask Reserved SRV Range", &InteractionOutlineMaskHeapStartIndex))
		return false;
	InteractionOutlineMaskDescriptorsInitialized = true;
	BuildInteractionOutlineMaskDescriptors();

	// AO 场景输入 SRV：每帧占 2 个（normal/sceneDepth）。
	if (!TryReserveSrvDescriptorSlots(SwapChainBufferCount * 2, L"AmbientOcclusionSceneInput Reserved SRV Range", &AOSceneInputHeapStartIndex))
		return false;
	AOSceneInputDescriptorsInitialized = true;
	BuildAOSceneInputDescriptors();

	// 透明 OIT SRV：每帧占 2 个（accum/reveal），按 [accum, reveal] 成对连续存放。
	if (!TryReserveSrvDescriptorSlots(SwapChainBufferCount * 2, L"TransparentOit Reserved SRV Range", &TransparentOitHeapStartIndex))
		return false;
	TransparentOitDescriptorsInitialized = true;
	BuildTransparentOitDescriptors();

	CreateFrameResources();
	CloseCommandListAndSynchronize();

	return true;
}

void D3DWindow::CreateCommandQueueAndSwapChain()
{
	//描述并创建命令队列。
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ThrowIfFailed(d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&CommandQueue)));
	SetD3DObjectName(CommandQueue.Get(), L"命令队列_直接渲染");

	//释放我们将重新创建的上一个交换链。
	SwapChain.Reset();

	//描述并创建交换链。
	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = WinInfo.Width;
	sd.BufferDesc.Height = WinInfo.Height;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = BackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.OutputWindow = WinInfo.m_hWnd;
	sd.Windowed = true;
	sd.SwapEffect = dxSwapEffect;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	//注意：交换链使用队列执行刷新。
	ComPtr<IDXGISwapChain> p_SwapChain = nullptr;
	ThrowIfFailed(dxgiFactory->CreateSwapChain(
		CommandQueue.Get(),
		&sd,
		p_SwapChain.GetAddressOf()));
	ThrowIfFailed(p_SwapChain.As(&SwapChain));

	ThrowIfFailed(d3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(MainCommandAllocator.GetAddressOf())));
	SetD3DObjectName(MainCommandAllocator.Get(), L"命令分配器_主初始化");

	//创建用于初始GPU设置的主命令列表。
	ThrowIfFailed(d3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		MainCommandAllocator.Get(),     // 关联的命令分配器
		nullptr,                    // 初始的管道状态对象
		IID_PPV_ARGS(MainCommandList.GetAddressOf())));
	SetD3DObjectName(MainCommandList.Get(), L"命令列表_主初始化");

	// 为每个帧资源创建一组命令分配器与命令列表。
	for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
	{
		ThrowIfFailed(d3dDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(mFrameResources[frameIndex].BeginCommandAllocator.GetAddressOf())));
		SetD3DObjectName(
			mFrameResources[frameIndex].BeginCommandAllocator.Get(),
			BuildD3DFrameObjectName(L"命令分配器_帧开始", frameIndex).c_str());

		//创建用于初始GPU设置的主命令列表。
		ThrowIfFailed(d3dDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mFrameResources[frameIndex].BeginCommandAllocator.Get(),     // 关联的命令分配器
			nullptr,                    // 初始的管道状态对象
			IID_PPV_ARGS(mFrameResources[frameIndex].BeginCommandList.GetAddressOf())));
		SetD3DObjectName(
			mFrameResources[frameIndex].BeginCommandList.Get(),
			BuildD3DFrameObjectName(L"命令列表_帧开始", frameIndex).c_str());
		ThrowIfFailed(mFrameResources[frameIndex].BeginCommandList->Close());

		ThrowIfFailed(d3dDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(mFrameResources[frameIndex].MidCommandAllocator.GetAddressOf())));
		SetD3DObjectName(
			mFrameResources[frameIndex].MidCommandAllocator.Get(),
			BuildD3DFrameObjectName(L"命令分配器_中段天空与透明准备", frameIndex).c_str());

		//创建用于初始GPU设置的主命令列表。
		ThrowIfFailed(d3dDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mFrameResources[frameIndex].MidCommandAllocator.Get(),     // 关联的命令分配器
			nullptr,                    // 初始的管道状态对象
			IID_PPV_ARGS(mFrameResources[frameIndex].MidCommandList.GetAddressOf())));
		SetD3DObjectName(
			mFrameResources[frameIndex].MidCommandList.Get(),
			BuildD3DFrameObjectName(L"命令列表_中段天空与透明准备", frameIndex).c_str());
		ThrowIfFailed(mFrameResources[frameIndex].MidCommandList->Close());

		ThrowIfFailed(d3dDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(mFrameResources[frameIndex].AoCommandAllocator.GetAddressOf())));
		SetD3DObjectName(
			mFrameResources[frameIndex].AoCommandAllocator.Get(),
			BuildD3DFrameObjectName(L"命令分配器_环境遮蔽AO", frameIndex).c_str());

		ThrowIfFailed(d3dDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mFrameResources[frameIndex].AoCommandAllocator.Get(),
			nullptr,
			IID_PPV_ARGS(mFrameResources[frameIndex].AoCommandList.GetAddressOf())));
		SetD3DObjectName(
			mFrameResources[frameIndex].AoCommandList.Get(),
			BuildD3DFrameObjectName(L"命令列表_环境遮蔽AO", frameIndex).c_str());
		ThrowIfFailed(mFrameResources[frameIndex].AoCommandList->Close());

		for (UINT threadIndex = 0; threadIndex < NumContexts; ++threadIndex)
		{
			ThrowIfFailed(d3dDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&mFrameResources[frameIndex].threadCommandAllocators[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].threadCommandAllocators[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令分配器_不透明主颜色", frameIndex, threadIndex).c_str());

			// 创建每个上下文对应的工作线程命令列表。
			ThrowIfFailed(d3dDevice->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				mFrameResources[frameIndex].threadCommandAllocators[threadIndex].Get(),
				nullptr,
				IID_PPV_ARGS(&mFrameResources[frameIndex].threadCommandLists[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].threadCommandLists[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令列表_不透明主颜色", frameIndex, threadIndex).c_str());
			ThrowIfFailed(mFrameResources[frameIndex].threadCommandLists[threadIndex]->Close());

			ThrowIfFailed(d3dDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&mFrameResources[frameIndex].translucentThreadCommandAllocators[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].translucentThreadCommandAllocators[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令分配器_半透明近不透明", frameIndex, threadIndex).c_str());

			ThrowIfFailed(d3dDevice->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				mFrameResources[frameIndex].translucentThreadCommandAllocators[threadIndex].Get(),
				nullptr,
				IID_PPV_ARGS(&mFrameResources[frameIndex].translucentThreadCommandLists[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].translucentThreadCommandLists[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令列表_半透明近不透明", frameIndex, threadIndex).c_str());
			ThrowIfFailed(mFrameResources[frameIndex].translucentThreadCommandLists[threadIndex]->Close());

			ThrowIfFailed(d3dDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&mFrameResources[frameIndex].transparentThreadCommandAllocators[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].transparentThreadCommandAllocators[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令分配器_透明OIT累积", frameIndex, threadIndex).c_str());

			ThrowIfFailed(d3dDevice->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				mFrameResources[frameIndex].transparentThreadCommandAllocators[threadIndex].Get(),
				nullptr,
				IID_PPV_ARGS(&mFrameResources[frameIndex].transparentThreadCommandLists[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].transparentThreadCommandLists[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令列表_透明OIT累积", frameIndex, threadIndex).c_str());
			ThrowIfFailed(mFrameResources[frameIndex].transparentThreadCommandLists[threadIndex]->Close());

			ThrowIfFailed(d3dDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&mFrameResources[frameIndex].shadowThreadCommandAllocators[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].shadowThreadCommandAllocators[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令分配器_阴影深度", frameIndex, threadIndex).c_str());

			ThrowIfFailed(d3dDevice->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				mFrameResources[frameIndex].shadowThreadCommandAllocators[threadIndex].Get(),
				nullptr,
				IID_PPV_ARGS(&mFrameResources[frameIndex].shadowThreadCommandLists[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].shadowThreadCommandLists[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令列表_阴影深度", frameIndex, threadIndex).c_str());
			ThrowIfFailed(mFrameResources[frameIndex].shadowThreadCommandLists[threadIndex]->Close());

			ThrowIfFailed(d3dDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&mFrameResources[frameIndex].normalThreadCommandAllocators[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].normalThreadCommandAllocators[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令分配器_法线预通道", frameIndex, threadIndex).c_str());

			ThrowIfFailed(d3dDevice->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				mFrameResources[frameIndex].normalThreadCommandAllocators[threadIndex].Get(),
				nullptr,
				IID_PPV_ARGS(&mFrameResources[frameIndex].normalThreadCommandLists[threadIndex])));
			SetD3DObjectName(
				mFrameResources[frameIndex].normalThreadCommandLists[threadIndex].Get(),
				BuildD3DFrameThreadObjectName(L"命令列表_法线预通道", frameIndex, threadIndex).c_str());
			ThrowIfFailed(mFrameResources[frameIndex].normalThreadCommandLists[threadIndex]->Close());
		}

		ThrowIfFailed(d3dDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(mFrameResources[frameIndex].EndCommandAllocator.GetAddressOf())));
		SetD3DObjectName(
			mFrameResources[frameIndex].EndCommandAllocator.Get(),
			BuildD3DFrameObjectName(L"命令分配器_后处理与收尾", frameIndex).c_str());

		//创建用于初始GPU设置的主命令列表。
		ThrowIfFailed(d3dDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mFrameResources[frameIndex].EndCommandAllocator.Get(),     // 关联的命令分配器
			nullptr,                    // 初始的管道状态对象
			IID_PPV_ARGS(mFrameResources[frameIndex].EndCommandList.GetAddressOf())));
		SetD3DObjectName(
			mFrameResources[frameIndex].EndCommandList.Get(),
			BuildD3DFrameObjectName(L"命令列表_后处理与收尾", frameIndex).c_str());
		ThrowIfFailed(mFrameResources[frameIndex].EndCommandList->Close());
	}
}

void D3DWindow::CreateDevice()
{
	//加载渲染管道依赖项。
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// 启用D3D12调试层。
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	// 创建工厂
	// 使用DXGI 1.1工厂生成枚举适配器，创建交换链以及将窗口与alt + enter键序列相关联的对象，
	// 以便切换到全屏显示模式和从全屏显示模式切换。
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	int adapterIndex = 0;
	bool adapterFound = false;

	// 枚举适配器，并选择合适的适配器来创建3D设备对象
	while (dxgiFactory->EnumAdapters1(adapterIndex, &DeviceAdapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 adapterDesc{};
		DeviceAdapter->GetDesc1(&adapterDesc);
		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			adapterIndex++;
			continue;
		}

		HRESULT result = D3D12CreateDevice(
			DeviceAdapter.Get(),
			dxFeatureLevel,
			_uuidof(ID3D12Device),
			nullptr);

		if (SUCCEEDED(result))
		{
			adapterFound = true;
			break;
		}

		adapterIndex++;
	}

	// 尝试创建D3D硬件设备
	ThrowIfFailed(D3D12CreateDevice(
		DeviceAdapter.Get(),
		dxFeatureLevel,
		IID_PPV_ARGS(&d3dDevice)));

	RtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	DsvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	CbvSrvUavDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	ThrowIfFailed(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	fenceEvent = CreateEventEx(nullptr, L"", false, EVENT_ALL_ACCESS);
	assert(fenceEvent != nullptr);
}

void D3DWindow::CreateDescriptorHeaps()
{
	// RTV 组成：
	// 1) 交换链后备缓冲 SwapChainBufferCount 个；
	// 2) AO: global normal + ambient0 + ambient1 共 3 个；
	// 3) 透明 OIT: 每帧 accum/reveal 各一个，共 SwapChainBufferCount * 2 个；
	// 4) 后处理场景颜色 RTV：每帧 1 个；
	// 5) 交互描边遮罩 RTV：每帧 1 个；
	// 6) DirectionalShadowMask: mask + blur temp 共 2 个。
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 3 + SwapChainBufferCount * 2 + SwapChainBufferCount + SwapChainBufferCount + 2;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(RtvHeap.GetAddressOf())));

	// 为主深度、全部阴影贴图和全局 AO 深度预留 DSV。
	// 现在点光源阴影改为 cubemap：1 个逻辑槽位对应 6 个物理面 DSV。
	// 因此这里不能再按“MaxShadowMapCount 个逻辑槽位 = MaxShadowMapCount 个 DSV”估算。
	const UINT maxShadowDsvCount = ShadowConfig.MaxShadowMapCount * 6;
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1 + maxShadowDsvCount + 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(DsvHeap.GetAddressOf())));
}

void D3DWindow::OnResize(bool Fullscreen)
{
	assert(d3dDevice != nullptr);
	assert(SwapChain != nullptr);
	assert(MainCommandAllocator != nullptr);
	CurrentRenderFramePlan.Valid = false;

	// 如果进入全屏状态一定要新的尺寸大于旧尺寸才继续调整大小否则直接返回
	if (Fullscreen && (WinInfo.Width * WinInfo.Height > EngineHelpers::GetDisplayWidth(WinInfo.m_hWnd) * EngineHelpers::GetDisplayHeight(WinInfo.m_hWnd)))
		return;

	// 更改任何资源之前先冲洗。
	FlushCommandQueue();

	// 重置 command list
	ResetCommandList();

	// 释放我们将重新创建的先前资源。
	for (int i = 0; i < SwapChainBufferCount; ++i)
	{
		SwapChainBuffer[i].Reset();
		mFrameResources[i].mCopyTexture.Reset();
		mFrameResources[i].mPostProcessSceneColor.Reset();
		mFrameResources[i].mInteractionOutlineMask.Reset();
		mFrameResources[i].mTransparentOitAccum.Reset();
		mFrameResources[i].mTransparentOitReveal.Reset();
	}

	DepthStencilBuffer.Reset();
	ThrowIfFailed(SwapChain->GetFullscreenState(&WinInfo.fullscreenState, nullptr));
	if (!Fullscreen)
	{
		WinInfo.Width = EngineHelpers::GetContextWidth(WinInfo.m_hWnd);
		WinInfo.Height = EngineHelpers::GetContextHeight(WinInfo.m_hWnd);


		if (WinInfo.fullscreenState != 0)
		{
			WinInfo.fullscreenState = 0;
			ThrowIfFailed(SwapChain->SetFullscreenState(WinInfo.fullscreenState, nullptr));
		}
	}
	else
	{
		WinInfo.Width = EngineHelpers::GetDisplayWidth(WinInfo.m_hWnd);
		WinInfo.Height = EngineHelpers::GetDisplayHeight(WinInfo.m_hWnd);

		if (WinInfo.fullscreenState != 1)
		{
			WinInfo.fullscreenState = 1;
			ThrowIfFailed(SwapChain->SetFullscreenState(WinInfo.fullscreenState, nullptr));
		}
	}
	if (WinInfo.Width * WinInfo.Height == 0)
	{
		// 当前帧不做重建，但要把主命令列表恢复为关闭状态，避免后续误用。
		CloseCommandList();
		return;
	}

	// 窗口尺寸变化后，交换链、深度缓冲、视口和投影矩阵都需要同步刷新。
	// 调整交换链的大小。
	ThrowIfFailed(SwapChain->ResizeBuffers(
		SwapChainBufferCount,
		WinInfo.Width, WinInfo.Height,
		BackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	// 每次调整大小，需要将后缓冲置为0。
	CurrBackBufferIndex = 0;

	//创建渲染目标视图（RTV）。
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart());

	// 为每个帧创建一个RTV。
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(SwapChain->GetBuffer(i, IID_PPV_ARGS(&SwapChainBuffer[i])));
		d3dDevice->CreateRenderTargetView(SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		SwapChainBuffer[i]->SetName((L"SwapChainBuffer" + std::to_wstring(i)).c_str());
		rtvHeapHandle.Offset(1, RtvDescriptorSize);
	}

	// 创建深度模板缓冲区和视图。
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = WinInfo.Width;
	depthStencilDesc.Height = WinInfo.Height;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;

	//更正2016年11月12日：SSAO章节要求对深度缓冲区有SRV才能从深度缓冲区中读取。
	//因此，因为我们需要为同一资源创建两个视图：
		 // 1. SRV格式：DXGI_FORMAT_R24_UNORM_X8_TYPELESS
		 // 2. DSV格式：DXGI_FORMAT_D24_UNORM_S8_UINT
		 //我们需要使用无类型格式创建深度缓冲区资源。
	depthStencilDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = DepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(DepthStencilBuffer.GetAddressOf())));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());

	// 使用资源格式将描述符创建为整个资源的MIP级别0。
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	d3dDevice->CreateDepthStencilView(DepthStencilBuffer.Get(), &dsvDesc, dsvHandle);

	//将资源从其初始状态转换为深度缓冲区。
	D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(DepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	MainCommandList->ResourceBarrier(1, &Barriers);
	DepthStencilBufferState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	CD3DX12_RESOURCE_DESC copyTexDesc;
	copyTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	copyTexDesc.Alignment = 0;
	copyTexDesc.Width = WinInfo.Width;
	copyTexDesc.Height = WinInfo.Height;
	copyTexDesc.DepthOrArraySize = 1;
	copyTexDesc.MipLevels = 1;
	copyTexDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	copyTexDesc.SampleDesc.Count = 1;
	copyTexDesc.SampleDesc.Quality = 0;
	copyTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	// AO 深度拷贝纹理只用于 Copy + SRV 采样，不作为真正的深度缓冲使用。
	// 这里保持普通纹理语义，避免以 DEPTH_WRITE/DS 资源方式创建后再做采样，
	// 触发驱动侧的访问权限冲突。
	copyTexDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&copyTexDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(mFrameResources[i].mCopyTexture.GetAddressOf())));
		mFrameResources[i].mCopyTexture->SetName((L"CopyTexture" + std::to_wstring(i)).c_str());
		CopyTextureStates[i] = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	// 后处理场景颜色缓冲：保存 FXAA 的输入图像。
	CD3DX12_RESOURCE_DESC postProcessSceneColorDesc;
	postProcessSceneColorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	postProcessSceneColorDesc.Alignment = 0;
	postProcessSceneColorDesc.Width = WinInfo.Width;
	postProcessSceneColorDesc.Height = WinInfo.Height;
	postProcessSceneColorDesc.DepthOrArraySize = 1;
	postProcessSceneColorDesc.MipLevels = 1;
	postProcessSceneColorDesc.Format = BackBufferFormat;
	postProcessSceneColorDesc.SampleDesc.Count = 1;
	postProcessSceneColorDesc.SampleDesc.Quality = 0;
	postProcessSceneColorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	postProcessSceneColorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	const float postProcessSceneColorClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	CD3DX12_CLEAR_VALUE postProcessSceneColorClearValue(BackBufferFormat, postProcessSceneColorClearColor);
	D3D12_RENDER_TARGET_VIEW_DESC postProcessSceneColorRtvDesc = {};
	postProcessSceneColorRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	postProcessSceneColorRtvDesc.Format = BackBufferFormat;
	postProcessSceneColorRtvDesc.Texture2D.MipSlice = 0;
	postProcessSceneColorRtvDesc.Texture2D.PlaneSlice = 0;
	PostProcessSceneColorRtvStartIndex = SwapChainBufferCount + 3 + SwapChainBufferCount * 2;

	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&postProcessSceneColorDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&postProcessSceneColorClearValue,
			IID_PPV_ARGS(mFrameResources[i].mPostProcessSceneColor.GetAddressOf())));
		mFrameResources[i].mPostProcessSceneColor->SetName((L"PostProcessSceneColor" + std::to_wstring(i)).c_str());

		CD3DX12_CPU_DESCRIPTOR_HANDLE postProcessSceneColorRtvHandle(
			RtvHeap->GetCPUDescriptorHandleForHeapStart(),
			PostProcessSceneColorRtvStartIndex + i,
			RtvDescriptorSize);
		d3dDevice->CreateRenderTargetView(
			mFrameResources[i].mPostProcessSceneColor.Get(),
			&postProcessSceneColorRtvDesc,
			postProcessSceneColorRtvHandle);
	}

	// 交互描边遮罩：独立于后处理场景颜色，避免状态切换互相踩踏。
	CD3DX12_RESOURCE_DESC interactionOutlineMaskDesc = postProcessSceneColorDesc;
	const float interactionOutlineMaskClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	CD3DX12_CLEAR_VALUE interactionOutlineMaskClearValue(BackBufferFormat, interactionOutlineMaskClearColor);
	D3D12_RENDER_TARGET_VIEW_DESC interactionOutlineMaskRtvDesc = postProcessSceneColorRtvDesc;
	InteractionOutlineMaskRtvStartIndex = PostProcessSceneColorRtvStartIndex + SwapChainBufferCount;

	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&interactionOutlineMaskDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&interactionOutlineMaskClearValue,
			IID_PPV_ARGS(mFrameResources[i].mInteractionOutlineMask.GetAddressOf())));
		mFrameResources[i].mInteractionOutlineMask->SetName((L"InteractionOutlineMask" + std::to_wstring(i)).c_str());

		CD3DX12_CPU_DESCRIPTOR_HANDLE interactionOutlineMaskRtvHandle(
			RtvHeap->GetCPUDescriptorHandleForHeapStart(),
			InteractionOutlineMaskRtvStartIndex + i,
			RtvDescriptorSize);
		d3dDevice->CreateRenderTargetView(
			mFrameResources[i].mInteractionOutlineMask.Get(),
			&interactionOutlineMaskRtvDesc,
			interactionOutlineMaskRtvHandle);
	}

	// 透明 OIT 缓冲：
	// - Accum: 累积预乘颜色与权重和；
	// - Reveal: 累积透射率（初始化为 1，按 (1 - alpha) 递减）。
	CD3DX12_RESOURCE_DESC transparentOitAccumDesc;
	transparentOitAccumDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	transparentOitAccumDesc.Alignment = 0;
	transparentOitAccumDesc.Width = WinInfo.Width;
	transparentOitAccumDesc.Height = WinInfo.Height;
	transparentOitAccumDesc.DepthOrArraySize = 1;
	transparentOitAccumDesc.MipLevels = 1;
	transparentOitAccumDesc.Format = TransparentOitAccumFormat;
	transparentOitAccumDesc.SampleDesc.Count = 1;
	transparentOitAccumDesc.SampleDesc.Quality = 0;
	transparentOitAccumDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	transparentOitAccumDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	CD3DX12_RESOURCE_DESC transparentOitRevealDesc = transparentOitAccumDesc;
	transparentOitRevealDesc.Format = TransparentOitRevealFormat;

	const float transparentOitAccumClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	const float transparentOitRevealClearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	CD3DX12_CLEAR_VALUE transparentOitAccumClearValue(TransparentOitAccumFormat, transparentOitAccumClearColor);
	CD3DX12_CLEAR_VALUE transparentOitRevealClearValue(TransparentOitRevealFormat, transparentOitRevealClearColor);

	const UINT transparentOitRtvStartIndex = SwapChainBufferCount + 3;
	D3D12_RENDER_TARGET_VIEW_DESC transparentOitAccumRtvDesc = {};
	transparentOitAccumRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	transparentOitAccumRtvDesc.Format = TransparentOitAccumFormat;
	transparentOitAccumRtvDesc.Texture2D.MipSlice = 0;
	transparentOitAccumRtvDesc.Texture2D.PlaneSlice = 0;

	D3D12_RENDER_TARGET_VIEW_DESC transparentOitRevealRtvDesc = {};
	transparentOitRevealRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	transparentOitRevealRtvDesc.Format = TransparentOitRevealFormat;
	transparentOitRevealRtvDesc.Texture2D.MipSlice = 0;
	transparentOitRevealRtvDesc.Texture2D.PlaneSlice = 0;

	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&transparentOitAccumDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&transparentOitAccumClearValue,
			IID_PPV_ARGS(mFrameResources[i].mTransparentOitAccum.GetAddressOf())));
		mFrameResources[i].mTransparentOitAccum->SetName((L"TransparentOitAccum" + std::to_wstring(i)).c_str());

		ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&transparentOitRevealDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&transparentOitRevealClearValue,
			IID_PPV_ARGS(mFrameResources[i].mTransparentOitReveal.GetAddressOf())));
		mFrameResources[i].mTransparentOitReveal->SetName((L"TransparentOitReveal" + std::to_wstring(i)).c_str());

		CD3DX12_CPU_DESCRIPTOR_HANDLE transparentOitAccumRtvHandle(
			RtvHeap->GetCPUDescriptorHandleForHeapStart(),
			transparentOitRtvStartIndex + i * 2,
			RtvDescriptorSize);
		d3dDevice->CreateRenderTargetView(
			mFrameResources[i].mTransparentOitAccum.Get(),
			&transparentOitAccumRtvDesc,
			transparentOitAccumRtvHandle);

		CD3DX12_CPU_DESCRIPTOR_HANDLE transparentOitRevealRtvHandle = transparentOitAccumRtvHandle;
		transparentOitRevealRtvHandle.Offset(1, RtvDescriptorSize);
		d3dDevice->CreateRenderTargetView(
			mFrameResources[i].mTransparentOitReveal.Get(),
			&transparentOitRevealRtvDesc,
			transparentOitRevealRtvHandle);
	}

	CloseCommandListAndSynchronize();

	// 此处设置窗口大小和裁剪大小
	m_viewport = CD3DX12_VIEWPORT{ 0.0f, 0.0f,
		static_cast<float>(WinInfo.Width),
		static_cast<float>(WinInfo.Height),
		0.0f,1.0f };
	m_scissorRect = CD3DX12_RECT{ 0, 0,
		(long)WinInfo.Width,
		(long)WinInfo.Height };
	if (textR != nullptr)
		textR->SetScreenSize(static_cast<float>(WinInfo.Width), static_cast<float>(WinInfo.Height));

	// 初始化相机状态
	mCamera.SetLens(0.25f * MathHelps::Pi,
		static_cast<float>(WinInfo.Width) / static_cast<float>(WinInfo.Height),
		1.0f, 1000.0f);

	shadowMapPass.OnResize(ShadowConfig.ShadowMapSize, ShadowConfig.ShadowMapSize);
	sharedNormalPrepass.OnResize(WinInfo.Width, WinInfo.Height);
	ambientOcclusion.OnResize(WinInfo.Width, WinInfo.Height);
	mDirectionalShadowMaskPass.OnResize(WinInfo.Width, WinInfo.Height);

	BuildPostProcessSceneColorDescriptors();
	BuildInteractionOutlineMaskDescriptors();
	BuildAOSceneInputDescriptors();
	BuildDirectionalShadowMaskDescriptors();
	BuildTransparentOitDescriptors();
}

bool D3DWindow::SerEditorDrawd()
{
	renderEditor = !renderEditor;
	return renderEditor;
}

// 创建根签名
void D3DWindow::CreateRootSignature()
{
	// 着色器程序通常需要资源作为输入（常量缓冲区，纹理，采样器）。
	// 根签名定义着色器程序期望的资源。
	// 如果我们将着色器程序视为函数，将输入资源视为函数参数，则可以将根签名视为定义函数签名。
	{
		const UINT shadowRegisterStart = 13;
		const UINT directionalShadowRegisterCount = ShadowPoolLimits::DirectionalTextureCount;
		const UINT spotShadowRegisterStart = shadowRegisterStart + directionalShadowRegisterCount;
		const UINT spotShadowRegisterCount = ShadowPoolLimits::SpotTextureCount;
		const UINT pointLightCubeRegisterStart = spotShadowRegisterStart + spotShadowRegisterCount;
		const UINT pointLightCubeRegisterCount = 64;
		const UINT aoRegisterIndex = pointLightCubeRegisterStart + pointLightCubeRegisterCount;
		const UINT directionalShadowMaskRegisterIndex = aoRegisterIndex + 1;
		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] =
		{
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 12, 1, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, directionalShadowRegisterCount, shadowRegisterStart, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, spotShadowRegisterCount, spotShadowRegisterStart, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, pointLightCubeRegisterCount, pointLightCubeRegisterStart, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, aoRegisterIndex, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, directionalShadowMaskRegisterIndex, 0}
		};

		// 根参数可以是表，根描述符或根常量。
		CD3DX12_ROOT_PARAMETER1 slotRootParameter[11];

		// 创建根CBV。效果提示：从最频繁到最不频繁的顺序
		slotRootParameter[0].InitAsConstantBufferView(0); // 逐对象 CBV
		slotRootParameter[1].InitAsConstantBufferView(1); // 逐 Pass CBV
		slotRootParameter[2].InitAsConstantBufferView(2); // 逐光源 Pass CBV
		slotRootParameter[3].InitAsConstantBufferView(3); // 逐材质 CBV
		slotRootParameter[4].InitAsConstantBufferView(4); // 逐蒙皮调色板 CBV
		slotRootParameter[5].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[6].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[7].InitAsDescriptorTable(2, &descriptorRanges[2], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[8].InitAsDescriptorTable(1, &descriptorRanges[4], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[9].InitAsDescriptorTable(1, &descriptorRanges[5], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[10].InitAsDescriptorTable(1, &descriptorRanges[6], D3D12_SHADER_VISIBILITY_PIXEL);

		auto staticSamplers = GetStaticSamplers();

		//根签名是一个根参数的数组。
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
		rootSigDesc.Init_1_1(_countof(slotRootParameter), slotRootParameter,
			(UINT)staticSamplers.size(), staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		//使用单个插槽创建根签名，该插槽指向由单个常量缓冲区组成的描述符范围
		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc,
			D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}

		ThrowIfFailed(hr);
		ThrowIfFailed(d3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(RootSignature.GetAddressOf())));

	}

	ambientOcclusion.CreateRootSignature();
	textR->CreateRootSignature();
	mDirectionalShadowMaskPass.CreateRootSignature();
	mSkinningComputePass.CreateRootSignature();
}

// 创建管道状态，其中包括编译和加载着色器。
void D3DWindow::CreatePipesAndShaders()
{
	D3D_SHADER_MACRO skinnedTransparentNearOpaqueDefines[] =
	{
		{ "SKINNED_MESH", "1" },
		{ "TRANSPARENT_PASS", "1" },
		{ "TRANSPARENT_OPAQUE_CUTOFF_PASS", "1" },
		{ nullptr, nullptr }
	};
	D3D_SHADER_MACRO transparentPassDefines[] =
	{
		{ "TRANSPARENT_PASS", "1" },
		{ "TRANSPARENT_OIT_PASS", "1" },
		{ nullptr, nullptr }
	};
	D3D_SHADER_MACRO transparentNearOpaqueDefines[] =
	{
		{ "TRANSPARENT_PASS", "1" },
		{ "TRANSPARENT_OPAQUE_CUTOFF_PASS", "1" },
		{ nullptr, nullptr }
	};

	vertexShader[天空着色器] = CompileShader(L"DATA/Shaders/Sky", nullptr, "VS", "vs_5_1");
	pixelShader[天空着色器] = CompileShader(L"DATA/Shaders/Sky", nullptr, "PS", "ps_5_1");
	vertexShader[不透明物体着色器] = CompileShader(L"DATA/Shaders/pbrx", nullptr, "VS", "vs_5_1");
	pixelShader[不透明物体着色器] = CompileShader(L"DATA/Shaders/pbrx", nullptr, "PS", "ps_5_1");
	vertexShader[透明物体着色器] = CompileShader(L"DATA/Shaders/pbrx", transparentPassDefines, "VS", "vs_5_1");
	pixelShader[透明物体着色器] = CompileShader(L"DATA/Shaders/pbrx", transparentPassDefines, "PS", "ps_5_1");
	vertexShader[半透明物体着色器] = CompileShader(L"DATA/Shaders/pbrx", transparentNearOpaqueDefines, "VS", "vs_5_1");
	pixelShader[半透明物体着色器] = CompileShader(L"DATA/Shaders/pbrx", transparentNearOpaqueDefines, "PS", "ps_5_1");
	vertexShader[蒙皮半透明着色器] = CompileShader(L"DATA/Shaders/pbrx", skinnedTransparentNearOpaqueDefines, "VS", "vs_5_1");

	// 定义顶点输入布局。
	InputElementDescs =
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",		0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,		0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BINORMAL",	0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 60, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	SkinnedInputElementDescs = InputElementDescs;
	SkinnedInputElementDescs.push_back({ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 72, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
	SkinnedInputElementDescs.push_back({ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 88, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });

	//描述并创建用于渲染场景的PSO。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc = {};
	ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	basePsoDesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	basePsoDesc.pRootSignature = RootSignature.Get();
	basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	basePsoDesc.SampleMask = UINT_MAX;
	basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	basePsoDesc.NumRenderTargets = 1;
	basePsoDesc.RTVFormats[0] = BackBufferFormat;
	basePsoDesc.SampleDesc.Count = 1;
	basePsoDesc.SampleDesc.Quality = 0;
	basePsoDesc.DSVFormat = DepthStencilFormat;

	//
	// 天空的PSO。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = basePsoDesc;

	// 相机位于天球内部，因此请关闭消隐功能。
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	// 确保depth函数不是LESS，而是LESS_EQUAL。
	// 否则，如果将深度缓冲区清除为1，
	// 则z = 1（NDC）处的归一化深度值将无法通过深度测试。
	skyPsoDesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	skyPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[天空着色器].Get());
	skyPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[天空着色器].Get());
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&skyPsoDesc,
		IID_PPV_ARGS(&PipelineState[天空管道])));
	SetD3DObjectName(PipelineState[天空管道].Get(), L"管线_天空盒");

	//
	//不透明对象的PSO。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = basePsoDesc;
	opaquePsoDesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	opaquePsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[不透明物体着色器].Get());
	opaquePsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[不透明物体着色器].Get());
	opaquePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc,
		IID_PPV_ARGS(&PipelineState[不透明物体管道])));
	SetD3DObjectName(PipelineState[不透明物体管道].Get(), L"管线_不透明物体_PBR");

	//
	// 透明中的“近不透明”子通道 PSO：
	// - 使用透明材质规则计算 alpha，但仅保留 alpha 接近 1 的片元；
	// - 直接写回主颜色 + 主深度，优先稳定遮挡关系，消除发灰/闪烁。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentNearOpaquePsoDesc = basePsoDesc;
	transparentNearOpaquePsoDesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	transparentNearOpaquePsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[半透明物体着色器].Get());
	transparentNearOpaquePsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[半透明物体着色器].Get());
	transparentNearOpaquePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	transparentNearOpaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	transparentNearOpaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	transparentNearOpaquePsoDesc.DepthStencilState.DepthEnable = TRUE;
	transparentNearOpaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	transparentNearOpaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&transparentNearOpaquePsoDesc,
		IID_PPV_ARGS(&PipelineState[半透明物体管道])));
	SetD3DObjectName(PipelineState[半透明物体管道].Get(), L"管线_半透明近不透明");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedTransparentNearOpaquePsoDesc = transparentNearOpaquePsoDesc;
	skinnedTransparentNearOpaquePsoDesc.InputLayout = { SkinnedInputElementDescs.data(), (UINT)SkinnedInputElementDescs.size() };
	skinnedTransparentNearOpaquePsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[蒙皮半透明着色器].Get());
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&skinnedTransparentNearOpaquePsoDesc,
		IID_PPV_ARGS(&SkinnedTransparentNearOpaquePipelineState)));
	SetD3DObjectName(SkinnedTransparentNearOpaquePipelineState.Get(), L"管线_蒙皮半透明近不透明");

	// 透明 PSO 重建策略：
	// 1) 保持与主路径一致的输入布局与根签名；
	// 2) 使用 Weighted-Blended OIT 两目标累积（不排序）；
	// 3) 深度测试开启但深度写关闭，仅利用不透明深度做遮挡；
	// 4) 保持双面渲染（Cull=None）。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = {};
	ZeroMemory(&transparentPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	transparentPsoDesc.InputLayout = { InputElementDescs.data(), static_cast<UINT>(InputElementDescs.size()) };
	transparentPsoDesc.pRootSignature = RootSignature.Get();
	transparentPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	transparentPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	transparentPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	transparentPsoDesc.BlendState.IndependentBlendEnable = TRUE;
	transparentPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	transparentPsoDesc.SampleMask = UINT_MAX;
	transparentPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	transparentPsoDesc.NumRenderTargets = 2;
	transparentPsoDesc.RTVFormats[0] = TransparentOitAccumFormat;
	transparentPsoDesc.RTVFormats[1] = TransparentOitRevealFormat;
	transparentPsoDesc.SampleDesc.Count = 1;
	transparentPsoDesc.SampleDesc.Quality = 0;
	transparentPsoDesc.DSVFormat = DepthStencilFormat;

	// RTV0: Accum（累积预乘颜色 + alpha）
	D3D12_RENDER_TARGET_BLEND_DESC& transparentAccumBlend = transparentPsoDesc.BlendState.RenderTarget[0];
	transparentAccumBlend.BlendEnable = TRUE;
	transparentAccumBlend.SrcBlend = D3D12_BLEND_ONE;
	transparentAccumBlend.DestBlend = D3D12_BLEND_ONE;
	transparentAccumBlend.BlendOp = D3D12_BLEND_OP_ADD;
	transparentAccumBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparentAccumBlend.DestBlendAlpha = D3D12_BLEND_ONE;
	transparentAccumBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparentAccumBlend.RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_RED |
		D3D12_COLOR_WRITE_ENABLE_GREEN |
		D3D12_COLOR_WRITE_ENABLE_BLUE |
		D3D12_COLOR_WRITE_ENABLE_ALPHA;

	// RTV1: Reveal（累计透射率 reveal *= (1 - alpha)）
	D3D12_RENDER_TARGET_BLEND_DESC& transparentRevealBlend = transparentPsoDesc.BlendState.RenderTarget[1];
	transparentRevealBlend.BlendEnable = TRUE;
	transparentRevealBlend.SrcBlend = D3D12_BLEND_ZERO;
	transparentRevealBlend.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
	transparentRevealBlend.BlendOp = D3D12_BLEND_OP_ADD;
	transparentRevealBlend.SrcBlendAlpha = D3D12_BLEND_ZERO;
	transparentRevealBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	transparentRevealBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparentRevealBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;

	transparentPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	transparentPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

	// 透明链路改为 OIT 后，固定关闭 DepthBias，避免与不透明深度产生竞争闪烁。
	transparentPsoDesc.RasterizerState.DepthBias = 0;
	transparentPsoDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;
	transparentPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;

	transparentPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[透明物体着色器].Get());
	transparentPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[透明物体着色器].Get());
	ComPtr<ID3D12PipelineState> newTransparentPso = nullptr;
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc,
		IID_PPV_ARGS(newTransparentPso.GetAddressOf())));
	SetD3DObjectName(newTransparentPso.Get(), L"管线_透明OIT累积");

	// 旧 PSO 可能仍被当前帧正在录制/待执行的命令列表引用，先延迟释放。
	if (PipelineState[透明物体管道] != nullptr)
		DeferredReleasePipelineStates.push_back({ PipelineState[透明物体管道], ComputeDeferredReleaseFence() });

	PipelineState[透明物体管道] = newTransparentPso;

	if (SkinnedTransparentPipelineState != nullptr)
		DeferredReleasePipelineStates.push_back({ SkinnedTransparentPipelineState, ComputeDeferredReleaseFence() });

	//
	//用于阴影贴图传递的PSO。
	//
	shadowMapPass.CreatePipesAndShaders(vertexShader[阴影着色器], pixelShader[阴影着色器],
		basePsoDesc, PipelineState[阴影管道]);
	SetD3DObjectName(PipelineState[阴影管道].Get(), L"管线_阴影深度_通用");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedShadowPsoDesc = basePsoDesc;
	skinnedShadowPsoDesc.InputLayout = { SkinnedInputElementDescs.data(), (UINT)SkinnedInputElementDescs.size() };
	skinnedShadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[阴影着色器].Get());
	skinnedShadowPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[阴影着色器].Get());
	skinnedShadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	skinnedShadowPsoDesc.NumRenderTargets = 0;
	skinnedShadowPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	skinnedShadowPsoDesc.DepthStencilState.DepthEnable = TRUE;
	skinnedShadowPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	skinnedShadowPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	skinnedShadowPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	skinnedShadowPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&skinnedShadowPsoDesc,
		IID_PPV_ARGS(&SkinnedShadowPipelineState)));
	SetD3DObjectName(SkinnedShadowPipelineState.Get(), L"管线_蒙皮阴影深度_通用");

	mDirectionalShadowMaskPass.CreatePipesAndShaders(basePsoDesc);
	mDirectionalShadowMaskPass.OnResize(WinInfo.Width, WinInfo.Height);

	// 方向光 采用专用阴影 PSO（按级联递增偏移），缓解远处地面条纹与闪烁。
	for (UINT cascadeIndex = 0; cascadeIndex < 4u; ++cascadeIndex)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC directionalCascadeShadowPsoDesc = basePsoDesc;
		directionalCascadeShadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[阴影着色器].Get());
		directionalCascadeShadowPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[阴影着色器].Get());
		directionalCascadeShadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
		directionalCascadeShadowPsoDesc.NumRenderTargets = 0;
		directionalCascadeShadowPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		directionalCascadeShadowPsoDesc.DepthStencilState.DepthEnable = TRUE;
		directionalCascadeShadowPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		directionalCascadeShadowPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		directionalCascadeShadowPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		// 方向光级联现在不再对 caster 做 receiver-box CPU 裁剪。
		// 如果继续双面写入 shadow map，大地面/薄面在远级联里很容易把自身前表面写成遮挡深度，
		// 镜头拉远后表现为屏幕下半部分大块错误变暗。Front-face culling 让阴影图主要写背面深度，
		// 是减少 directional self-shadow/acne 的常规处理；普通 point/spot shadow PSO 维持原配置。
		directionalCascadeShadowPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
		// 统一级联写入偏移，避免不同级联因偏移差异在拼接处形成可见条纹。
		directionalCascadeShadowPsoDesc.RasterizerState.DepthBias = 12000;
		directionalCascadeShadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.35f;
		directionalCascadeShadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;

		ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&directionalCascadeShadowPsoDesc,
			IID_PPV_ARGS(&DirectionalCascadeShadowPipelineState[cascadeIndex])));
		SetD3DObjectName(
			DirectionalCascadeShadowPipelineState[cascadeIndex].Get(),
			(L"管线_CSM方向光级联阴影_" + std::to_wstring(cascadeIndex)).c_str());
		D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedDirectionalCascadeShadowPsoDesc = directionalCascadeShadowPsoDesc;
		skinnedDirectionalCascadeShadowPsoDesc.InputLayout = { SkinnedInputElementDescs.data(), (UINT)SkinnedInputElementDescs.size() };
		skinnedDirectionalCascadeShadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[阴影着色器].Get());
		ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&skinnedDirectionalCascadeShadowPsoDesc,
			IID_PPV_ARGS(&SkinnedDirectionalCascadeShadowPipelineState[cascadeIndex])));
		SetD3DObjectName(
			SkinnedDirectionalCascadeShadowPipelineState[cascadeIndex].Get(),
			(L"管线_蒙皮CSM方向光级联阴影_" + std::to_wstring(cascadeIndex)).c_str());
	}

	//
	// 调试层的 PSO。
	//
	//D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = basePsoDesc;
	//debugPsoDesc.pRootSignature = RootSignature.Get();
	//debugPsoDesc.VS = CD3DX12_SHADER_BYTECODE(debugvertexShader.Get());
	//debugPsoDesc.PS = CD3DX12_SHADER_BYTECODE(debugpixelShader.Get());
	//ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&debugPsoDesc,
	//	IID_PPV_ARGS(&debugPipelineState)));
	//SetD3DObjectName(debugPipelineState.Get(), L"管线_调试绘制");

	std::vector<ComPtr<ID3DBlob>>AOvertexShader;
	std::vector<ComPtr<ID3DBlob>>AOpixelShader;
	std::vector<ComPtr<ID3D12PipelineState>> AOPipelineState;

	//
	// AO的PSO。
	//

	AOvertexShader.resize(3);
	AOpixelShader.resize(3);
	AOPipelineState.resize(3);
	ambientOcclusion.CreatePipesAndShaders(AOvertexShader, AOpixelShader, basePsoDesc, AOPipelineState);
	vertexShader[法线绘制着色器]= AOvertexShader[0];
	pixelShader[法线绘制着色器] = AOpixelShader[0];
	PipelineState[法线绘制管道] = AOPipelineState[0];
	SetD3DObjectName(PipelineState[法线绘制管道].Get(), L"管线_法线预通道");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedDrawNormalsPsoDesc = basePsoDesc;
	skinnedDrawNormalsPsoDesc.InputLayout = { SkinnedInputElementDescs.data(), static_cast<UINT>(SkinnedInputElementDescs.size()) };
	skinnedDrawNormalsPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[法线绘制着色器].Get());
	skinnedDrawNormalsPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[法线绘制着色器].Get());
	skinnedDrawNormalsPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	skinnedDrawNormalsPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	skinnedDrawNormalsPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	skinnedDrawNormalsPsoDesc.SampleDesc.Count = 1;
	skinnedDrawNormalsPsoDesc.SampleDesc.Quality = 0;
	skinnedDrawNormalsPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&skinnedDrawNormalsPsoDesc,
		IID_PPV_ARGS(&SkinnedNormalPipelineState)));
	SetD3DObjectName(SkinnedNormalPipelineState.Get(), L"管线_蒙皮法线预通道");
	vertexShader[环境遮蔽着色器] = AOvertexShader[1];
	pixelShader[环境遮蔽着色器] = AOpixelShader[1];
	PipelineState[环境遮蔽管道] = AOPipelineState[1];
	SetD3DObjectName(PipelineState[环境遮蔽管道].Get(), L"管线_环境遮蔽AO");
	vertexShader[遮蔽模糊着色器] = AOvertexShader[2];
	pixelShader[遮蔽模糊着色器] = AOpixelShader[2];
	PipelineState[遮蔽模糊管道] = AOPipelineState[2];
	SetD3DObjectName(PipelineState[遮蔽模糊管道].Get(), L"管线_环境遮蔽AO模糊");

	// 交互遮罩、描边及其蒙皮变体由 InteractionOutlinePass 内部编译 shader 并创建 PSO。
	interactionOutlinePass.SetSharedRootSignature(RootSignature.Get());
	interactionOutlinePass.SetPipelineInputs(
		InputElementDescs,
		SkinnedInputElementDescs,
		BackBufferFormat,
		DepthStencilFormat);
	interactionOutlinePass.CreatePipesAndShaders();

	// 蒙皮权重可视化 PSO（通过 SkinWeightVizPass 创建）
	{
		// basePsoDesc 的 InputLayout 是标准顶点，这里需要蒙皮输入布局。
		D3D12_GRAPHICS_PIPELINE_STATE_DESC skinWeightVizBasePsoDesc = basePsoDesc;
		skinWeightVizBasePsoDesc.InputLayout = { SkinnedInputElementDescs.data(), static_cast<UINT>(SkinnedInputElementDescs.size()) };
		mSkinWeightVizPass.SetSharedRootSignature(RootSignature.Get());
		mSkinWeightVizPass.SetBasePsoDesc(skinWeightVizBasePsoDesc);
		mSkinWeightVizPass.CreatePipesAndShaders();

		// 为 SkinWeightVizPass 注入长期依赖
		mSkinWeightVizPass.SetSrvDescriptorHeap(SrvDescriptorHeap.Get());
		mSkinWeightVizPass.SetOtherTexDescriptor(otherTexDescriptor);
		mSkinWeightVizPass.SetDeferredReleaseQueue(&DeferredReleaseGeometries);
		mSkinWeightVizPass.SetComputeDeferredReleaseFenceFn([this]() { return ComputeDeferredReleaseFence(); });
		mSkinWeightVizPass.SetResolveSkinningOwnerFn(ResolveSkinningRuntimeOwnerEntity);
		mSkinWeightVizPass.SetEditor(mEditor);
	}

	//
	// FXAA 后处理 PSO。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC postProcessPsoDesc = basePsoDesc;
	postProcessPsoDesc.InputLayout = { nullptr, 0 };
	postProcessPsoDesc.DepthStencilState.DepthEnable = FALSE;
	postProcessPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	postProcessPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	// 全屏后处理 PSO 已由 FXAAPass 自行管理，不再写入全局 PipelineState 数组。
	// postProcessPsoDesc 仍保留供 VolumetricLightPass 使用。

	//
	// 透明 OIT 合成 PSO 已由 OITCompositePass 自行管理。

	//
	// 体积光 PSO（逐灯全屏体积积分，不依赖任何模型网格）。
	//
	volumetricLightPass.SetSharedRootSignature(RootSignature.Get());
	volumetricLightPass.SetBasePsoDesc(postProcessPsoDesc);
	volumetricLightPass.CreatePipesAndShaders();

	//
	//文字的PSO。
	//
	textR->CreatePipesAndShaders(BackBufferFormat, DepthStencilFormat, &PipelineState[文字管道]);

	// 所有图形 Pass 共享主根签名。
	mGizmoPass.SetSharedRootSignature(RootSignature.Get());
	mSkeletonOverlayPass.SetSharedRootSignature(RootSignature.Get());
	mOITCompositePass.SetSharedRootSignature(RootSignature.Get());
	mFXAAPass.SetSharedRootSignature(RootSignature.Get());

	mGizmoPass.SetBasePsoDesc(basePsoDesc);
	mSkeletonOverlayPass.SetBasePsoDesc(basePsoDesc);
	mOITCompositePass.SetBasePsoDesc(basePsoDesc);
	mFXAAPass.SetBasePsoDesc(basePsoDesc);

	mOITCompositePass.CreatePipesAndShaders();
	mFXAAPass.CreatePipesAndShaders();
	mSkeletonOverlayPass.CreatePipesAndShaders();
	mGizmoPass.CreatePipesAndShaders();
	mSkinningComputePass.CreatePipesAndShaders();
}

void D3DWindow::CreateSRVDescriptorHeap()
{
	//
	//创建SRV堆。
	//
	constexpr UINT kMinSrvDescriptorCapacity = 16384;
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = (std::max)(SwapChainBufferCount * 256, kMinSrvDescriptorCapacity);
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	//srvHeapDesc.NodeMask = 0;

	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(
		&srvHeapDesc, IID_PPV_ARGS(&SrvDescriptorHeap)));
	SrvDescriptorHeapCapacity = srvHeapDesc.NumDescriptors;
	SrvDescriptorHeapIndex = 0;
}

void D3DWindow::BuildPostProcessSceneColorDescriptors()
{
	if (!PostProcessSceneColorDescriptorsInitialized)
		return;
	if (SrvDescriptorHeap == nullptr || d3dDevice == nullptr)
		return;

	D3D12_SHADER_RESOURCE_VIEW_DESC sceneColorSrvDesc = {};
	sceneColorSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	sceneColorSrvDesc.Format = BackBufferFormat;
	sceneColorSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sceneColorSrvDesc.Texture2D.MostDetailedMip = 0;
	sceneColorSrvDesc.Texture2D.MipLevels = 1;
	sceneColorSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	CD3DX12_CPU_DESCRIPTOR_HANDLE sceneColorSrvCpuHandle(
		SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		PostProcessSceneColorHeapStartIndex,
		CbvSrvUavDescriptorSize);

	for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
	{
		auto sceneColorResource = mFrameResources[frameIndex].mPostProcessSceneColor.Get();
		if (sceneColorResource == nullptr)
			continue;

		CD3DX12_CPU_DESCRIPTOR_HANDLE targetHandle = sceneColorSrvCpuHandle;
		targetHandle.Offset(frameIndex, CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(sceneColorResource, &sceneColorSrvDesc, targetHandle);
	}
}

void D3DWindow::BuildInteractionOutlineMaskDescriptors()
{
	if (!InteractionOutlineMaskDescriptorsInitialized)
		return;
	if (SrvDescriptorHeap == nullptr || d3dDevice == nullptr)
		return;

	D3D12_SHADER_RESOURCE_VIEW_DESC outlineMaskSrvDesc = {};
	outlineMaskSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	outlineMaskSrvDesc.Format = BackBufferFormat;
	outlineMaskSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	outlineMaskSrvDesc.Texture2D.MostDetailedMip = 0;
	outlineMaskSrvDesc.Texture2D.MipLevels = 1;
	outlineMaskSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	CD3DX12_CPU_DESCRIPTOR_HANDLE outlineMaskSrvCpuHandle(
		SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		InteractionOutlineMaskHeapStartIndex,
		CbvSrvUavDescriptorSize);

	for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
	{
		auto outlineMaskResource = mFrameResources[frameIndex].mInteractionOutlineMask.Get();
		if (outlineMaskResource == nullptr)
			continue;

		CD3DX12_CPU_DESCRIPTOR_HANDLE targetHandle = outlineMaskSrvCpuHandle;
		targetHandle.Offset(frameIndex, CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(outlineMaskResource, &outlineMaskSrvDesc, targetHandle);
	}
}

void D3DWindow::BuildAOSceneInputDescriptors()
{
	if (!AOSceneInputDescriptorsInitialized)
		return;
	sharedNormalPrepass.BuildSceneInputDescriptors(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		AOSceneInputHeapStartIndex,
		CbvSrvUavDescriptorSize,
		SwapChainBufferCount,
		DepthStencilBuffer.Get());
}

void D3DWindow::BuildDirectionalShadowMaskDescriptors()
{
	if (!DirectionalShadowMaskDescriptorsInitialized)
		return;

	mDirectionalShadowMaskPass.BuildDescriptors(
		GetCpuSrv().Offset(DirectionalShadowMaskHeapStartIndex, CbvSrvUavDescriptorSize),
		GetGpuSrv().Offset(DirectionalShadowMaskHeapStartIndex, CbvSrvUavDescriptorSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(
			RtvHeap->GetCPUDescriptorHandleForHeapStart(),
			DirectionalShadowMaskRtvStartIndex,
			RtvDescriptorSize),
		CbvSrvUavDescriptorSize,
		RtvDescriptorSize);
}

void D3DWindow::BuildTransparentOitDescriptors()
{
	if (!TransparentOitDescriptorsInitialized)
		return;
	if (SrvDescriptorHeap == nullptr || d3dDevice == nullptr)
		return;

	D3D12_SHADER_RESOURCE_VIEW_DESC accumSrvDesc = {};
	accumSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	accumSrvDesc.Format = TransparentOitAccumFormat;
	accumSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	accumSrvDesc.Texture2D.MostDetailedMip = 0;
	accumSrvDesc.Texture2D.MipLevels = 1;
	accumSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	D3D12_SHADER_RESOURCE_VIEW_DESC revealSrvDesc = accumSrvDesc;
	revealSrvDesc.Format = TransparentOitRevealFormat;

	CD3DX12_CPU_DESCRIPTOR_HANDLE transparentOitSrvCpuHandle(
		SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		TransparentOitHeapStartIndex,
		CbvSrvUavDescriptorSize);

	for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
	{
		auto accumResource = mFrameResources[frameIndex].mTransparentOitAccum.Get();
		auto revealResource = mFrameResources[frameIndex].mTransparentOitReveal.Get();
		if (accumResource == nullptr || revealResource == nullptr)
			continue;

		CD3DX12_CPU_DESCRIPTOR_HANDLE accumHandle = transparentOitSrvCpuHandle;
		accumHandle.Offset(frameIndex * 2, CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(accumResource, &accumSrvDesc, accumHandle);

		CD3DX12_CPU_DESCRIPTOR_HANDLE revealHandle = accumHandle;
		revealHandle.Offset(1, CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(revealResource, &revealSrvDesc, revealHandle);
	}
}

void D3DWindow::LoadTextures()
{
	ResourceUploadBatch resourceUpload(d3dDevice.Get());
	resourceUpload.Begin();

	if (SrvDescriptorHeapCapacity == 0)
	{
		EngineHelpers::AddLog(L"[D3DWindow] LoadTextures skipped: SRV descriptor heap is not initialized.");
		return;
	}

	Texture DiffuseTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"DiffuseMap", L"DATA/Textures/white8x8.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture defaultNmapTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"defaultNmap", L"DATA/Textures/8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture defaultMetallic(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"defaultMetal", L"DATA/Textures/8K_Metalness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture defaultRoughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"defaultRough", L"DATA/Textures/8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"Diffuse"].resize(5);
	TextureGroups[L"Diffuse"][0] = DiffuseTex;
	TextureGroups[L"Diffuse"][1] = defaultNmapTex;
	TextureGroups[L"Diffuse"][3] = defaultMetallic;
	TextureGroups[L"Diffuse"][4] = defaultRoughness;

	Texture skyCubeTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"skyMap", L"DATA/HDRIs/scythian_tombs_2_4k.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"skyMap"].resize(1);
	TextureGroups[L"skyMap"][0] = skyCubeTex;
	SkyTexHeapIndex = skyCubeTex.GetIndex();

	Texture bricksTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"bricksDiffuseMap", L"DATA/Textures/bricks2.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture bricksMapTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"bricksNormalMap", L"DATA/Textures/bricks2_nmap.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"bricks"].resize(2);
	TextureGroups[L"bricks"][0] = bricksTex;
	TextureGroups[L"bricks"][1] = bricksMapTex;

	Texture stoneTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"stoneDiffuseMap", L"DATA/Textures/stone.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"stone"].resize(1);
	TextureGroups[L"stone"][0] = stoneTex;

	Texture tileTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"tileDiffuseMap", L"DATA/Textures/tile.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture tileMapTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"tileNormalMap", L"DATA/Textures/tile_nmap.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"tile"].resize(2);
	TextureGroups[L"tile"][0] = tileTex;
	TextureGroups[L"tile"][1] = tileMapTex;

	//--------------------------------------------------------
	Texture semlcibb_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Albedo", L"DATA/Textures/Brick_Modern/semlcibb_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Normal", L"DATA/Textures/Brick_Modern/semlcibb_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Specular", L"DATA/Textures/Brick_Modern/semlcibb_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Roughness", L"DATA/Textures/Brick_Modern/semlcibb_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Displacement", L"DATA/Textures/Brick_Modern/semlcibb_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"semlcibb"].resize(12);
	TextureGroups[L"semlcibb"][0] = semlcibb_Albedo;
	TextureGroups[L"semlcibb"][1] = semlcibb_Normal;
	TextureGroups[L"semlcibb"][2] = semlcibb_Specular;
	TextureGroups[L"semlcibb"][4] = semlcibb_Roughness;
	TextureGroups[L"semlcibb"][5] = semlcibb_Displacement;

	//--------------------------------------------------------
	Texture rustediron_basecolor(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"rustediron_basecolor", L"DATA/Textures/rustediron/rustediron2_basecolor.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rustediron_normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"rustediron_normal", L"DATA/Textures/rustediron/rustediron2_normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture rustediron_metallic(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"rustediron_metallic", L"DATA/Textures/rustediron/rustediron2_metallic.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rustediron_roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"rustediron_roughness", L"DATA/Textures/rustediron/rustediron2_roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"rustediron"].resize(12);
	TextureGroups[L"rustediron"][0] = rustediron_basecolor;
	TextureGroups[L"rustediron"][1] = rustediron_normal;
	TextureGroups[L"rustediron"][3] = rustediron_metallic;
	TextureGroups[L"rustediron"][4] = rustediron_roughness;

	//--------------------------------------------------------
	Texture rm4kshp_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Albedo", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Normal", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Specular", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Roughness", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Displacement", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"rm4kshp"].resize(12);
	TextureGroups[L"rm4kshp"][0] = rm4kshp_Albedo;
	TextureGroups[L"rm4kshp"][1] = rm4kshp_Normal;
	TextureGroups[L"rm4kshp"][2] = rm4kshp_Specular;
	TextureGroups[L"rm4kshp"][4] = rm4kshp_Roughness;
	TextureGroups[L"rm4kshp"][5] = rm4kshp_Displacement;

	//--------------------------------------------------------
	Texture sdbhdd3b_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Albedo", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Normal", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Specular", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Roughness", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Displacement", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"sdbhdd3b"].resize(12);
	TextureGroups[L"sdbhdd3b"][0] = sdbhdd3b_Albedo;
	TextureGroups[L"sdbhdd3b"][1] = sdbhdd3b_Normal;
	TextureGroups[L"sdbhdd3b"][2] = sdbhdd3b_Specular;
	TextureGroups[L"sdbhdd3b"][4] = sdbhdd3b_Roughness;
	TextureGroups[L"sdbhdd3b"][5] = sdbhdd3b_Displacement;

	//--------------------------------------------------------
	Texture sfknaeoa_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Albedo", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Normal", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Specular", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Roughness", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Displacement", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);

	SrvDescriptorHeapIndex++;
	TextureGroups[L"sfknaeoa"].resize(12);
	TextureGroups[L"sfknaeoa"][0] = sfknaeoa_Albedo;
	TextureGroups[L"sfknaeoa"][1] = sfknaeoa_Normal;
	TextureGroups[L"sfknaeoa"][2] = sfknaeoa_Specular;
	TextureGroups[L"sfknaeoa"][4] = sfknaeoa_Roughness;
	TextureGroups[L"sfknaeoa"][5] = sfknaeoa_Displacement;

	//--------------------------------------------------------
	Texture copper_rock1_alb(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"copper-rock1-alb", L"DATA/Textures/rockcopper/copper-rock1-alb.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture copper_rock1_normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"copper-rock1-normal", L"DATA/Textures/rockcopper/copper-rock1-normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture copper_rock1_metal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"copper-rock1-metal", L"DATA/Textures/rockcopper/copper-rock1-metal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture copper_rock1_rough(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"copper-rock1-rough", L"DATA/Textures/rockcopper/copper-rock1-rough.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"copper_rock1"].resize(12);
	TextureGroups[L"copper_rock1"][0] = copper_rock1_alb;
	TextureGroups[L"copper_rock1"][1] = copper_rock1_normal;
	TextureGroups[L"copper_rock1"][3] = copper_rock1_metal;
	TextureGroups[L"copper_rock1"][4] = copper_rock1_rough;

	//--------------------------------------------------------
	Texture scpgdgca_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Albedo", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Normal", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Specular", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Roughness", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Displacement", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"scpgdgca"].resize(12);
	TextureGroups[L"scpgdgca"][0] = scpgdgca_Albedo;
	TextureGroups[L"scpgdgca"][1] = scpgdgca_Normal;
	TextureGroups[L"scpgdgca"][2] = scpgdgca_Specular;
	TextureGroups[L"scpgdgca"][4] = scpgdgca_Roughness;
	TextureGroups[L"scpgdgca"][5] = scpgdgca_Displacement;

	//--------------------------------------------------------
	Texture se2abbvc_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Albedo", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Normal", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Specular", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Roughness", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Displacement", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"se2abbvc"].resize(12);
	TextureGroups[L"se2abbvc"][0] = se2abbvc_Albedo;
	TextureGroups[L"se2abbvc"][1] = se2abbvc_Normal;
	TextureGroups[L"se2abbvc"][2] = se2abbvc_Specular;
	TextureGroups[L"se2abbvc"][4] = se2abbvc_Roughness;
	TextureGroups[L"se2abbvc"][5] = se2abbvc_Displacement;

	auto uploadResourcesFinished = resourceUpload.End(
		CommandQueue.Get());

	uploadResourcesFinished.wait();
}

void D3DWindow::AddShapeGeometry() { D3DWindowGeometryProvider::AddShapeGeometry(this); }

void D3DWindow::AddShapeGeometry(MeshGeometry* geo) { D3DWindowGeometryProvider::AddShapeGeometry(this, geo); }

void D3DWindow::AddBillboardGeometry() { D3DWindowGeometryProvider::AddBillboardGeometry(this); }

void D3DWindow::AddTransformGizmoGeometry() { D3DWindowGeometryProvider::AddTransformGizmoGeometry(this); }

void D3DWindow::RemoveShapeGeometry(std::wstring name) { D3DWindowGeometryProvider::RemoveShapeGeometry(this, name); }

bool D3DWindow::HasShapeGeometry(const std::wstring& name) const { return D3DWindowGeometryProvider::HasShapeGeometry(this, name); }

void D3DWindow::BuildMaterials()
{
	auto autoMaterial = std::make_unique<Material>();
	autoMaterial->SetName(L"autoMat");
	autoMaterial->MatCBIndex = 0;
	autoMaterial->DiffuseTexture = &TextureGroups[L"Diffuse"][0];
	autoMaterial->NormalTexture = &TextureGroups[L"Diffuse"][1];
	autoMaterial->SpecularTexture = nullptr;
	autoMaterial->MetallicTexture = &TextureGroups[L"Diffuse"][3];;
	autoMaterial->RoughnessTexture = &TextureGroups[L"Diffuse"][4];;
	autoMaterial->MatTransform = MathHelps::Identity;
	autoMaterial->Properties.UseNormalTexture = 1u;
	autoMaterial->Properties.UseMetallicTexture = 1u;
	autoMaterial->Properties.UseRoughnessTexture = 1u;
	autoMaterial->Properties.UseSpecularTexture = 0u;
	autoMaterial->Properties.UseDiffuseTexture = 1u;
	autoMaterial->Properties.Metallic = 0.0f;
	autoMaterial->Properties.Roughness = 0.0f;
	autoMaterial->Properties.ClearCoatThickness = 0.0f;
	autoMaterial->Properties.ClearCoatRoughness = 0.0f;
	autoMaterial->Properties.Anisotropy = 0.0f;
	autoMaterial->Properties.AnisotropyRotation = 0.0f;

	Materials[L"autoMat"] = *autoMaterial.get();

	auto sky = std::make_unique<Material>();
	sky->SetName(L"sky");
	sky->MatCBIndex = 1;
	sky->DiffuseTexture = &TextureGroups[L"skyMap"][0];
	//sky->NormalTexture = 0;//sky就不需要设置了
	SkyMapIndex = TextureGroups[L"skyMap"][0].GetIndex();
	SkyTexHeapIndex = SkyMapIndex;
	sky->MatTransform = MathHelps::Identity;
	Materials[L"sky"] = *sky.get();
	SkyMaterialByTexturePath[D3DWindowAssetHelpers::NormalizeAssetPath(L"DATA/HDRIs/scythian_tombs_2_4k.png")] = L"sky";
}

void D3DWindow::BuildLight()
{
	AmbientColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	ClearLights();
}

void D3DWindow::ClearLights()
{
	Lights.clear();
	LightsCache.clear();
	LightsCacheNames.clear();
	LightShadowMapIndices.clear();
	CurrentShadowPoolPlan = {};
	CurrentShadowUpdateRequests.clear();
	ShadowLightRuntimeStates.clear();
	ShadowRenderEntries.clear();
	RotatedLightDirections.clear();
	ShadowTransform.clear();
	ShadowTransform.push_back(MathHelps::Identity);
	MainPassCB.LightConst = 0;
	FreshenAllLight = true;
}

void D3DWindow::SetAmbientColor(const DirectX::XMFLOAT4& ambientColor)
{
	AmbientColor = ambientColor;
	FreshenAllLight = true;
}

void D3DWindow::AddLight(Light* light)
{
	if (light == nullptr)
		return;

	light->LitCBIndex = static_cast<int>(Lights.size());
	Lights[light->GetName()] = *light;
	MainPassCB.LightConst = static_cast<UINT>(Lights.size());
	FreshenAllLight = true;
}

void D3DWindow::AddRenderItem(std::wstring renderItemName, ObjectCollection* objectCollection, const std::wstring& geometryName,
	UINT renderLayerIndex, const DirectX::XMFLOAT4X4* worldTransform,
	const DirectX::XMFLOAT4X4* texTransform, const std::wstring* materialName,
	const DirectX::BoundingBox* localBounds,
	SceneEntityType sceneType,
	bool rebuildOpaqueBatches)
{
	if (objectCollection == nullptr)
		return;

	// 几何必须已经先注册到 Geometries 中，否则该渲染项无效。
	auto geometryIt = Geometries.find(geometryName);
	if (geometryIt == Geometries.end())
	{
		return;
	}

	if (materialName != nullptr && !materialName->empty())
	{
		// 若指定了材质名，则优先绑定该材质。
		auto materialIt = Materials.find(*materialName);
		if (materialIt != Materials.end())
			objectCollection->Material = &materialIt->second;
	}

	// 未显式指定材质时回退到默认材质，避免出现空材质引用。
	if (objectCollection->Material == nullptr)
		objectCollection->Material = &Materials[L"autoMat"];

	UINT resolvedRenderLayerIndex = renderLayerIndex;
	if (objectCollection->Material != nullptr)
		resolvedRenderLayerIndex = ResolveRenderLayerIndexByMaterial(renderLayerIndex, objectCollection->Material->GetName());

	auto existingRenderItemIt = AllRitems.find(renderItemName);
	const bool replacingExistingItem = existingRenderItemIt != AllRitems.end();
	UINT existingObjectCBIndex = ObjCBCount;
	if (replacingExistingItem)
	{
		existingObjectCBIndex = existingRenderItemIt->second.ObjCBIndex;

		// 同名渲染项重建时，先把旧的层索引引用清掉，
		// 避免同一对象残留在多个渲染层或保留旧指针导致重复绘制/闪烁。
		EraseRenderItemFromAllLayers(renderItemName);

		DirtyObjectCBItems.erase(renderItemName);
	}

	RenderItem renderItem;
	if (worldTransform != nullptr)
		renderItem.WorldTransform = *worldTransform;
	else
		// 历史默认行为：没有给定矩阵时使用一个固定缩放的初始矩阵。
		XMStoreFloat4x4(&renderItem.WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.0f, 0.0f));

	if (texTransform != nullptr)
		renderItem.TexTransform = *texTransform;
	else
		XMStoreFloat4x4(&renderItem.TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));

	renderItem.ObjCBIndex = replacingExistingItem ? existingObjectCBIndex : ObjCBCount;
	renderItem.NumFramesDirty = SwapChainBufferCount;
	renderItem.Obj = objectCollection;
	renderItem.Geo = &geometryIt->second;
	renderItem.PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderItem.SceneType = sceneType;
	if (localBounds != nullptr)
	{
		renderItem.LocalBounds = *localBounds;
		renderItem.HasLocalBounds = true;
	}

	// AllRitems 保存所有渲染项，RitemLayer 只保存各通道的引用索引。
	AllRitems[renderItemName] = renderItem;
	RitemLayer[resolvedRenderLayerIndex][renderItemName] = &AllRitems[renderItemName];
	DirtyObjectCBItems.insert(renderItemName);
	if (!replacingExistingItem)
		ObjCBCount++;
	if (rebuildOpaqueBatches)
		RebuildOpaqueThreadBatches();
}

void D3DWindow::EraseRenderItemFromAllLayers(const std::wstring& renderItemName)
{
	for (UINT layerIndex = 0; layerIndex < (UINT)渲染项目计数; ++layerIndex)
		RitemLayer[layerIndex].erase(renderItemName);
}

void D3DWindow::RefreshRenderItemCachesAfterStructuralChange(bool reindexObjectCBIndices)
{
	// 结构性变更后，渲染项索引、线程分片和 FrameResource 容量视图都可能发生变化。
	// 统一收口到这里，避免各入口遗漏其中某一步导致渲染缓存失配。
	if (reindexObjectCBIndices)
		ReindexRenderItemObjectCBIndices();
	RebuildOpaqueThreadBatches();
	CreateFrameResources();
}

void D3DWindow::RemoveRenderItem(std::wstring renderItemName, UINT renderLayerIndex)
{
	(void)renderLayerIndex;
	// 统一按名称从所有渲染层移除，避免层索引变化后残留悬空指针。
	EraseRenderItemFromAllLayers(renderItemName);
	DirtyObjectCBItems.erase(renderItemName);
	if (AllRitems.erase(renderItemName) > 0 && ObjCBCount > 0)
		ObjCBCount--;

	RebuildOpaqueThreadBatches();
}

void D3DWindow::ClearRenderItems()
{
	// 仅清空渲染项汇总，不处理 ECS 实体本身。
	for (UINT layerIndex = 0; layerIndex < (UINT)渲染项目计数; ++layerIndex)
	{
		RitemLayer[layerIndex].clear();
	}

	AllRitems.clear();
	DrawSetObjectCollectionCache.clear();
	DrawSetAggrObjectCache.clear();
	DirtyObjectCBItems.clear();
	ObjCBCount = 0;

	for (UINT threadIndex = 0; threadIndex < NumContexts; ++threadIndex)
	{
		OpaqueThreadBatches[threadIndex].clear();
		TransparentThreadBatches[threadIndex].clear();
	}
}

void D3DWindow::ReindexRenderItemObjectCBIndices()
{
	UINT objectIndex = 0;
	for (auto& renderItemPair : AllRitems)
	{
		renderItemPair.second.ObjCBIndex = objectIndex++;
		renderItemPair.second.SkinningCBIndex = renderItemPair.second.ObjCBIndex;
	}
	ObjCBCount = objectIndex;
	FreshenObjectCBs();
}

void D3DWindow::CollectLayerRenderItemsForThreadBatch(
	const std::map<std::wstring, RenderItem*>& sourceLayer,
	std::vector<std::pair<std::wstring, RenderItem*>>& outItems) const
{
	outItems.clear();
	outItems.reserve(sourceLayer.size());

	for (const auto& item : sourceLayer)
	{
		if (item.second != nullptr)
			outItems.push_back(item);
	}
}

void D3DWindow::RebuildOpaqueThreadBatches()
{
	// 将渲染项按线程重建分片：
	// - 不透明项由工作线程并行录制；
	// - 透明项不再做 CPU 排序，统一走 OIT 累积/合成路径。
	for (UINT threadIndex = 0; threadIndex < NumContexts; ++threadIndex)
	{
		OpaqueThreadBatches[threadIndex].clear();
		TransparentThreadBatches[threadIndex].clear();
	}

	std::vector<std::pair<std::wstring, RenderItem*>> opaqueItems;
	CollectLayerRenderItemsForThreadBatch(RitemLayer[不透明物体渲染项目], opaqueItems);

	std::vector<std::pair<std::wstring, RenderItem*>> transparentItems;
	CollectLayerRenderItemsForThreadBatch(RitemLayer[透明物体渲染项目], transparentItems);

	SplitRenderItemsToThreadBatches(opaqueItems, OpaqueThreadBatches);
	SplitRenderItemsToThreadBatches(transparentItems, TransparentThreadBatches);
}

// 创建帧资源
void D3DWindow::CreateFrameResources()
{
	const UINT requiredObjectCount = std::max<UINT>(1u, static_cast<UINT>(AllRitems.size()));
	const UINT requiredMaterialCount = std::max<UINT>(1u, static_cast<UINT>(Materials.size()));
	const UINT requiredPassCount = 1u + ShadowConfig.MaxShadowMapCount * 6u;

	// 尽量避免频繁重建 FrameResource。
	// 旧版这里按“精确数量”重建，会立刻释放旧 UploadBuffer，
	// 容易与仍在飞行中的命令列表形成资源生命周期竞争。
	if (FrameResourceObjectCapacity >= requiredObjectCount &&
		FrameResourceMaterialCapacity >= requiredMaterialCount &&
		!mFrameResources.empty())
	{
		FreshenObjectCBs();
		FreshenMaterialCBs();
		FreshenLightCBs();
		return;
	}

	// 旧 FrameResource 中的 ObjectCB / MaterialCB / PassCB 仍可能被上一批命令列表引用。
	// 真正需要扩容时，先等待 GPU 完全消费旧命令，避免 UploadBuffer 提前析构。
	FlushCommandQueue();

	// 编辑器导入模型时可能一次性创建大量渲染项/材质。
	// 这里提高初始容量，尽量避免导入阶段触发 FrameResource 频繁重建。
	FrameResourceObjectCapacity = std::max<UINT>(requiredObjectCount, std::max<UINT>(FrameResourceObjectCapacity * 2u, 4096u));
	FrameResourceMaterialCapacity = std::max<UINT>(requiredMaterialCount, std::max<UINT>(FrameResourceMaterialCapacity * 2u, 1024u));

	for (UINT i = 0; i < SwapChainBufferCount; ++i)
	{
		mFrameResources[i].Create(d3dDevice.Get(),
			requiredPassCount,
			FrameResourceObjectCapacity,
			FrameResourceMaterialCapacity);
	}

	// 新建/重建 FrameResource 后，新的上传缓冲内容是空的，
	// 需要把当前场景中的对象、材质、灯光常量重新整帧回灌一次。
	FreshenObjectCBs();
	FreshenMaterialCBs();
	FreshenLightCBs();
}

std::wstring D3DWindow::GetMaterialName(std::wstring renderItemName)
{
	RenderItem* renderItem = GetRenderItem(renderItemName);
	if (renderItem == nullptr || renderItem->Obj == nullptr || renderItem->Obj->Material == nullptr)
		return L"";

	return renderItem->Obj->Material->GetName();
}

UINT D3DWindow::ResolveRenderLayerIndexByMaterial(UINT currentRenderLayerIndex, const std::wstring& materialName) const
{
	if (currentRenderLayerIndex == 天空渲染项目 || currentRenderLayerIndex == debugrt)
		return currentRenderLayerIndex;
	if (materialName.empty())
		return currentRenderLayerIndex;

	auto materialIt = Materials.find(materialName);
	if (materialIt == Materials.end())
		return currentRenderLayerIndex;

	const Material& material = materialIt->second;
	constexpr float kTransparentAlphaThreshold = 0.999f;
	const bool hasAlphaBlend = material.Properties.Opacity < kTransparentAlphaThreshold;
	const bool hasOpacityTexture = material.OpacityTexture != nullptr;
	return (hasAlphaBlend || hasOpacityTexture) ? 透明物体渲染项目 : 不透明物体渲染项目;
}

void D3DWindow::MoveRenderItemToLayer(const std::wstring& renderItemName, UINT targetRenderLayerIndex)
{
	if (targetRenderLayerIndex >= (UINT)渲染项目计数)
		return;

	auto renderItemIt = AllRitems.find(renderItemName);
	if (renderItemIt == AllRitems.end())
		return;

	UINT currentLayerIndex = (UINT)渲染项目计数;
	for (UINT layerIndex = 0; layerIndex < (UINT)渲染项目计数; ++layerIndex)
	{
		if (RitemLayer[layerIndex].find(renderItemName) != RitemLayer[layerIndex].end())
		{
			currentLayerIndex = layerIndex;
			break;
		}
	}

	if (currentLayerIndex == targetRenderLayerIndex)
		return;

	EraseRenderItemFromAllLayers(renderItemName);
	RitemLayer[targetRenderLayerIndex][renderItemName] = &renderItemIt->second;
	RebuildOpaqueThreadBatches();
}

void D3DWindow::SetMaterial(std::wstring renderItemName, std::wstring materialName)
{
	RenderItem* renderItem = GetRenderItem(renderItemName);
	if (renderItem == nullptr || renderItem->Obj == nullptr)
	{
#ifdef _DEBUG
		EngineHelpers::AddLog((L"[D3DWindow] SetMaterial 跳过：缺少渲染项目 -> " + renderItemName).c_str());
#endif
		return;
	}

	auto materialIt = Materials.find(materialName);
	if (materialIt == Materials.end())
	{
		auto autoMaterialIt = Materials.find(L"autoMat");
		if (autoMaterialIt == Materials.end())
		{
#ifdef _DEBUG
			EngineHelpers::AddLog((L"[D3DWindow] SetMaterial 跳过：缺少材质 -> " + materialName).c_str());
#endif
			return;
		}

		renderItem->Obj->Material = &autoMaterialIt->second;
	}
	else
	{
		renderItem->Obj->Material = &materialIt->second;
	}

	UINT currentLayerIndex = (UINT)渲染项目计数;
	for (UINT layerIndex = 0; layerIndex < (UINT)渲染项目计数; ++layerIndex)
	{
		if (RitemLayer[layerIndex].find(renderItemName) != RitemLayer[layerIndex].end())
		{
			currentLayerIndex = layerIndex;
			break;
		}
	}

	if (currentLayerIndex < (UINT)渲染项目计数)
	{
		const UINT resolvedLayerIndex =
			ResolveRenderLayerIndexByMaterial(currentLayerIndex, renderItem->Obj->Material->GetName());
		if (resolvedLayerIndex != currentLayerIndex)
			MoveRenderItemToLayer(renderItemName, resolvedLayerIndex);
	}
}

std::wstring D3DWindow::GetOrCreateMaterialFromWMaterialFile(const std::filesystem::path& materialFilePath)
{
	if (materialFilePath.empty())
		return L"";

	const std::wstring normalizedPath = D3DWindowAssetHelpers::NormalizeAssetPath(materialFilePath.wstring());
	auto existingMaterialIt = MaterialByFilePath.find(normalizedPath);
	if (existingMaterialIt != MaterialByFilePath.end())
	{
		auto materialIt = Materials.find(existingMaterialIt->second);
		if (materialIt != Materials.end())
			return existingMaterialIt->second;
	}

	WMaterialFileData materialFileData;
	if (!WMaterialFile::LoadFromFile(materialFilePath, &materialFileData))
		return L"";

	const ImportedMaterialInfo importedMaterialInfo =
		D3DWindowAssetHelpers::ConvertMaterialFileDataToImportedInfo(materialFileData, materialFilePath);
	const std::wstring materialName = CreateMaterialFromImport(
		importedMaterialInfo.Name.empty() ? materialFilePath.stem().wstring() : importedMaterialInfo.Name,
		importedMaterialInfo);
	if (!materialName.empty())
	{
		MaterialByFilePath[normalizedPath] = materialName;
		auto materialIt = Materials.find(materialName);
		if (materialIt != Materials.end())
		{
			materialIt->second.Properties.UseDiffuseTexture = !materialFileData.DiffuseTexture.empty() ? 1u : 0u;
			materialIt->second.Properties.UseNormalTexture = materialFileData.UseNormalTexture ? 1u : 0u;
			materialIt->second.Properties.UseSpecularTexture = materialFileData.UseSpecularTexture ? 1u : 0u;
			materialIt->second.Properties.UseMetallicTexture = materialFileData.UseMetallicTexture ? 1u : 0u;
			materialIt->second.Properties.UseRoughnessTexture = materialFileData.UseRoughnessTexture ? 1u : 0u;
			materialIt->second.Properties.FresnelR0 = materialFileData.FresnelR0;
			const bool useDiffuseAlphaAsOpacity =
				materialFileData.UseOpacityTexture || D3DWindowAssetHelpers::IsLikelyAlphaCarrierTexturePath(importedMaterialInfo.DiffuseTexture.Path);
			materialIt->second.OpacityTexture = useDiffuseAlphaAsOpacity ? materialIt->second.DiffuseTexture : nullptr;
			materialIt->second.NumFramesDirty = SwapChainBufferCount;
			FreshenMaterialCBs();
		}
	}

	return materialName;
}

std::wstring D3DWindow::GetMaterialFilePathByMaterialName(const std::wstring& MaterialName) const
{
	for (const auto& materialPair : MaterialByFilePath)
	{
		if (materialPair.second == MaterialName)
			return materialPair.first;
	}

	return L"";
}

std::wstring D3DWindow::GetSkyTexturePathByMaterialName(const std::wstring& MaterialName) const
{
	for (const auto& materialPair : SkyMaterialByTexturePath)
	{
		if (materialPair.second == MaterialName)
			return materialPair.first;
	}

	return L"";
}

std::wstring D3DWindow::GetOrCreateSkyMaterial(const std::wstring& skyTexturePath)
{
	const std::wstring resolvedPath = D3DWindowAssetHelpers::NormalizeAssetPath(
		skyTexturePath.empty() ? L"DATA/HDRIs/scythian_tombs_2_4k.png" : skyTexturePath);
	std::filesystem::path resolvedFilePath = std::filesystem::path(resolvedPath);
	if (resolvedFilePath.is_relative())
	{
		std::filesystem::path probe = std::filesystem::current_path();
		while (!probe.empty())
		{
			const std::filesystem::path candidate = probe / resolvedFilePath;
			if (std::filesystem::exists(candidate))
			{
				resolvedFilePath = candidate.lexically_normal();
				break;
			}

			const std::filesystem::path parent = probe.parent_path();
			if (parent == probe)
				break;
			probe = parent;
		}
	}

	auto existingMaterialIt = SkyMaterialByTexturePath.find(resolvedPath);
	if (existingMaterialIt != SkyMaterialByTexturePath.end())
	{
		auto materialIt = Materials.find(existingMaterialIt->second);
		if (materialIt != Materials.end() && materialIt->second.DiffuseTexture != nullptr)
		{
			SkyTexHeapIndex = materialIt->second.DiffuseTexture->GetIndex();
			SkyMapIndex = SkyTexHeapIndex;
		}
		return existingMaterialIt->second;
	}

	if (!std::filesystem::exists(resolvedFilePath))
	{
		SkyTexHeapIndex = TextureGroups[L"skyMap"][0].GetIndex();
		SkyMapIndex = SkyTexHeapIndex;
		return L"sky";
	}

	const std::wstring baseName = std::filesystem::path(resolvedPath).stem().wstring();
	const std::wstring materialName = D3DWindowAssetHelpers::MakeUniqueName(Materials, baseName.empty() ? L"sky" : (L"sky_" + baseName));
	const std::wstring textureGroupName = materialName + L"_TextureGroup";

	UINT descriptorIndex = 0;
	if (!TryReserveSrvDescriptorSlots(1, L"GetOrCreateSkyMaterial", &descriptorIndex))
	{
		SkyTexHeapIndex = TextureGroups[L"skyMap"][0].GetIndex();
		SkyMapIndex = SkyTexHeapIndex;
		return L"sky";
	}

	ResourceUploadBatch resourceUpload(d3dDevice.Get());
	resourceUpload.Begin();

	Texture skyTexture(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		materialName + L"_SkyTexture",
		resolvedFilePath.wstring(),
		D3DWindowAssetHelpers::ResolveTextureTypeFromPath(resolvedPath),
		descriptorIndex);

	auto uploadResourcesFinished = resourceUpload.End(CommandQueue.Get());
	uploadResourcesFinished.wait();

	TextureGroups[textureGroupName].resize(1);
	TextureGroups[textureGroupName][0] = skyTexture;

	Material material;
	material.SetName(materialName);
	material.MatCBIndex = static_cast<int>(Materials.size());
	material.DiffuseTexture = &TextureGroups[textureGroupName][0];
	material.MatTransform = MathHelps::Identity;
	material.NumFramesDirty = SwapChainBufferCount;

	Materials[materialName] = material;
	SkyMaterialByTexturePath[resolvedPath] = materialName;
	SkyTexHeapIndex = TextureGroups[textureGroupName][0].GetIndex();
	SkyMapIndex = SkyTexHeapIndex;
	FreshenMaterialCBs();

	return materialName;
}

Material* D3DWindow::GetMaterialByMaterialName(const std::wstring& MaterialName)
{
	auto materialIt = Materials.find(MaterialName);
	if (materialIt == Materials.end())
		return nullptr;

	return &materialIt->second;
}

const Material* D3DWindow::GetMaterialByMaterialName(const std::wstring& MaterialName) const
{
	auto materialIt = Materials.find(MaterialName);
	if (materialIt == Materials.end())
		return nullptr;

	return &materialIt->second;
}

bool D3DWindow::ApplyMaterialPbrTexturesFromWMaterialData(
	const std::wstring& MaterialName,
	const std::filesystem::path& materialFilePath,
	const WMaterialFileData& materialData)
{
	if (MaterialName.empty() || materialFilePath.empty())
		return false;

	auto materialIt = Materials.find(MaterialName);
	if (materialIt == Materials.end())
		return false;

	auto defaultTextureGroupIt = TextureGroups.find(L"Diffuse");
	if (defaultTextureGroupIt == TextureGroups.end() || defaultTextureGroupIt->second.size() < 5)
		return false;

	Texture* fallbackDiffuse = &defaultTextureGroupIt->second[0];
	Texture* fallbackNormal = &defaultTextureGroupIt->second[1];
	Texture* fallbackSpecular = &defaultTextureGroupIt->second[0];
	Texture* fallbackMetallic = &defaultTextureGroupIt->second[3];
	Texture* fallbackRoughness = &defaultTextureGroupIt->second[4];

	const ImportedMaterialInfo importedMaterialInfo =
		D3DWindowAssetHelpers::ConvertMaterialFileDataToImportedInfo(materialData, materialFilePath);

	const std::wstring textureGroupName = MaterialName + L"_TextureGroup";
	auto existingTextureGroupIt = TextureGroups.find(textureGroupName);
	std::vector<Texture>* existingTextureGroup = nullptr;
	if (existingTextureGroupIt != TextureGroups.end())
		existingTextureGroup = &existingTextureGroupIt->second;

	std::vector<Texture> textureGroup(5);
	ResourceUploadBatch resourceUpload(d3dDevice.Get());
	resourceUpload.Begin();

	CreateMaterialTextureSlot(textureGroup, 0, importedMaterialInfo.DiffuseTexture, fallbackDiffuse, MaterialName, L"Diffuse", resourceUpload, existingTextureGroup);
	CreateMaterialTextureSlot(textureGroup, 1, importedMaterialInfo.NormalTexture, fallbackNormal, MaterialName, L"Normal", resourceUpload, existingTextureGroup);
	CreateMaterialTextureSlot(textureGroup, 2, importedMaterialInfo.SpecularTexture, fallbackSpecular, MaterialName, L"Specular", resourceUpload, existingTextureGroup);
	CreateMaterialTextureSlot(textureGroup, 3, importedMaterialInfo.MetallicTexture, fallbackMetallic, MaterialName, L"Metallic", resourceUpload, existingTextureGroup);
	CreateMaterialTextureSlot(textureGroup, 4, importedMaterialInfo.RoughnessTexture, fallbackRoughness, MaterialName, L"Roughness", resourceUpload, existingTextureGroup);

	auto uploadResourcesFinished = resourceUpload.End(CommandQueue.Get());
	uploadResourcesFinished.wait();

	TextureGroups[textureGroupName] = textureGroup;

	Material& material = materialIt->second;
	material.DiffuseTexture = &TextureGroups[textureGroupName][0];
	material.NormalTexture = &TextureGroups[textureGroupName][1];
	material.SpecularTexture = &TextureGroups[textureGroupName][2];
	material.MetallicTexture = &TextureGroups[textureGroupName][3];
	material.RoughnessTexture = &TextureGroups[textureGroupName][4];

	material.Properties.UseDiffuseTexture = !materialData.DiffuseTexture.empty() ? 1u : 0u;
	material.Properties.UseNormalTexture = (materialData.UseNormalTexture && !materialData.NormalTexture.empty()) ? 1u : 0u;
	material.Properties.UseMetallicTexture = (materialData.UseMetallicTexture && !materialData.MetallicTexture.empty()) ? 1u : 0u;
	material.Properties.UseRoughnessTexture = (materialData.UseRoughnessTexture && !materialData.RoughnessTexture.empty()) ? 1u : 0u;
	material.Properties.UseSpecularTexture = 0u;

	const bool useDiffuseAlphaAsOpacity =
		materialData.UseOpacityTexture ||
		D3DWindowAssetHelpers::IsLikelyAlphaCarrierTexturePath(importedMaterialInfo.OpacityTexture.Path) ||
		D3DWindowAssetHelpers::IsLikelyAlphaCarrierTexturePath(importedMaterialInfo.DiffuseTexture.Path);
	material.OpacityTexture = useDiffuseAlphaAsOpacity ? material.DiffuseTexture : nullptr;

	const std::wstring normalizedPath = D3DWindowAssetHelpers::NormalizeAssetPath(materialFilePath.wstring());
	MaterialByFilePath[normalizedPath] = MaterialName;

	NotifyMaterialChanged(MaterialName);
	return true;
}

void D3DWindow::NotifyMaterialChanged(const std::wstring& MaterialName)
{
	auto materialIt = Materials.find(MaterialName);
	if (materialIt == Materials.end())
		return;

	materialIt->second.NumFramesDirty = SwapChainBufferCount;
	FreshenMaterialCBs();

	bool layerChanged = false;
	for (auto& renderItemPair : AllRitems)
	{
		const std::wstring& renderItemName = renderItemPair.first;
		RenderItem& renderItem = renderItemPair.second;
		if (renderItem.Obj == nullptr || renderItem.Obj->Material == nullptr)
			continue;
		if (renderItem.Obj->Material->GetName() != MaterialName)
			continue;

		UINT currentLayerIndex = (UINT)渲染项目计数;
		for (UINT layerIndex = 0; layerIndex < (UINT)渲染项目计数; ++layerIndex)
		{
			if (RitemLayer[layerIndex].find(renderItemName) != RitemLayer[layerIndex].end())
			{
				currentLayerIndex = layerIndex;
				break;
			}
		}

		if (currentLayerIndex >= (UINT)渲染项目计数)
			continue;

		const UINT resolvedLayerIndex = ResolveRenderLayerIndexByMaterial(currentLayerIndex, MaterialName);
		if (resolvedLayerIndex == currentLayerIndex)
			continue;

		RitemLayer[currentLayerIndex].erase(renderItemName);
		RitemLayer[resolvedLayerIndex][renderItemName] = &renderItem;
		layerChanged = true;
	}

	if (layerChanged)
		RebuildOpaqueThreadBatches();
}

std::vector<std::wstring> D3DWindow::GetMaterialNameList()
{
	std::vector<std::wstring> NameList(Materials.size());
	UINT i = 0;
	for (auto mat = Materials.begin(); mat != Materials.end(); mat++)
	{
		NameList[i] = mat->first;
		i++;
	}
	return NameList;
}

ID3D12Resource* D3DWindow::GetRenderTargetBuffer()
{
	return SwapChainBuffer->Get();
}

ID3D12Resource* D3DWindow::GetDepthStencilBuffer()
{
	return DepthStencilBuffer.Get();
}

ID3D12Device* D3DWindow::GetDevice()
{
	return d3dDevice.Get();
}

IDXGISwapChain* D3DWindow::GetSwapChain()
{
	return SwapChain.Get();
}

ID3D12CommandQueue* D3DWindow::GetCommandQueue()
{
	return CommandQueue.Get();
}

ID3D12GraphicsCommandList* D3DWindow::GetCommandList()
{
	return MainCommandList.Get();
}

ID3D12GraphicsCommandList* D3DWindow::GetThreadCommandList(int threadIndex)
{
	return CurrFrameResource->threadCommandLists[threadIndex].Get();
}

ID3D12CommandAllocator* D3DWindow::GetWorkerCommandAllocator(UINT workerPhaseIndex, int threadIndex)
{
	switch (workerPhaseIndex)
	{
	case 阴影工作阶段:
		return CurrFrameResource->shadowThreadCommandAllocators[threadIndex].Get();
	case 法线工作阶段:
		return CurrFrameResource->normalThreadCommandAllocators[threadIndex].Get();
	case 半透明工作阶段:
		return CurrFrameResource->translucentThreadCommandAllocators[threadIndex].Get();
	case 透明工作阶段:
		return CurrFrameResource->transparentThreadCommandAllocators[threadIndex].Get();
	case 不透明工作阶段:
	default:
		return CurrFrameResource->threadCommandAllocators[threadIndex].Get();
	}
}

ID3D12GraphicsCommandList* D3DWindow::GetWorkerCommandList(UINT workerPhaseIndex, int threadIndex)
{
	switch (workerPhaseIndex)
	{
	case 阴影工作阶段:
		return CurrFrameResource->shadowThreadCommandLists[threadIndex].Get();
	case 法线工作阶段:
		return CurrFrameResource->normalThreadCommandLists[threadIndex].Get();
	case 半透明工作阶段:
		return CurrFrameResource->translucentThreadCommandLists[threadIndex].Get();
	case 透明工作阶段:
		return CurrFrameResource->transparentThreadCommandLists[threadIndex].Get();
	case 不透明工作阶段:
	default:
		return CurrFrameResource->threadCommandLists[threadIndex].Get();
	}
}

void D3DWindow::EnsureShadowMapResources(UINT requiredShadowMapCount)
{
	requiredShadowMapCount = (std::min)(requiredShadowMapCount, ShadowConfig.MaxShadowMapCount);
	(void)requiredShadowMapCount;
	assert(CurrentShadowPoolPlan.DirectionalPool.UsedSlotCount <= ShadowPoolLimits::DirectionalTextureCount);
	assert(CurrentShadowPoolPlan.SpotPool.UsedSlotCount <= ShadowPoolLimits::SpotTextureCount);
	assert(CurrentShadowPoolPlan.PointPool.UsedSlotCount <= ShadowPoolLimits::PointCubeTextureCount);

	const UINT directionalLightType = static_cast<UINT>(std::lround(ShadowConfig.DirectionalLightType));
	const UINT pointLightType = static_cast<UINT>(std::lround(ShadowConfig.PointLightType));
	const UINT spotLightType = static_cast<UINT>(std::lround(ShadowConfig.SpotLightType));
	const UINT directionalCascadeCount = std::clamp(ShadowConfig.DirectionalCascadeCount, 1u, 3u);

	// 资源布局必须按“逻辑 shadow slot”来构建，而不是按灯遍历顺序。
	// 否则 ShadowPoolPlanner 分配出来的 BaseShadowMapIndex 会和真实资源顺序错位，
	// 表现就是：某类阴影（尤其是 Spot）能写入，但主场景采样到的不是同一张图。
	UINT texture2DSlotCount = 0;
	UINT pointLightCubeCount = 0;
	for (UINT lightIndex = 0; lightIndex < LightsCache.size(); ++lightIndex)
	{
		if (lightIndex >= LightShadowMapIndices.size())
			continue;
		const int shadowBaseIndex = LightShadowMapIndices[lightIndex];
		if (shadowBaseIndex < 0)
			continue;

		const UINT resolvedLightType = static_cast<UINT>(std::lround(LightsCache[lightIndex].Type));

		if (resolvedLightType == directionalLightType)
		{
			texture2DSlotCount = (std::max)(texture2DSlotCount, static_cast<UINT>(shadowBaseIndex) + directionalCascadeCount);
		}
		else if (resolvedLightType == pointLightType)
		{
			pointLightCubeCount = (std::max)(pointLightCubeCount, static_cast<UINT>(shadowBaseIndex + 1));
		}
		else if (resolvedLightType == spotLightType)
		{
			texture2DSlotCount = (std::max)(texture2DSlotCount, static_cast<UINT>(shadowBaseIndex + 1));
		}
	}

	// Point pool 的 BaseShadowMapIndex 是全局逻辑槽位，需要减去前面的 2D 段。
	if (pointLightCubeCount > texture2DSlotCount)
		pointLightCubeCount -= texture2DSlotCount;
	else
		pointLightCubeCount = 0;

	std::vector<UINT> texture2DSizes(texture2DSlotCount, (std::max)(ShadowConfig.ShadowMapSize, 1u));
	std::vector<UINT> pointLightCubeSizes(pointLightCubeCount, (std::max)(ShadowConfig.ShadowMapSize, 1u));

	for (UINT lightIndex = 0; lightIndex < LightsCache.size(); ++lightIndex)
	{
		if (lightIndex >= LightShadowMapIndices.size())
			continue;
		const int shadowBaseIndex = LightShadowMapIndices[lightIndex];
		if (shadowBaseIndex < 0)
			continue;

		const UINT resolvedLightType = static_cast<UINT>(std::lround(LightsCache[lightIndex].Type));
		if (resolvedLightType == directionalLightType)
		{
			for (UINT cascadeIndex = 0; cascadeIndex < directionalCascadeCount; ++cascadeIndex)
			{
				const UINT shadowSlotIndex = static_cast<UINT>(shadowBaseIndex) + cascadeIndex;
				if (shadowSlotIndex >= texture2DSizes.size())
					continue;
				const UINT cascadeConfigIndex = (std::min)(cascadeIndex, 2u);
				texture2DSizes[shadowSlotIndex] =
					(std::max)(ShadowConfig.DirectionalCascadeShadowMapSizes[cascadeConfigIndex], 1u);
			}
		}
		else if (resolvedLightType == spotLightType)
		{
			const UINT shadowSlotIndex = static_cast<UINT>(shadowBaseIndex);
			if (shadowSlotIndex < texture2DSizes.size())
				texture2DSizes[shadowSlotIndex] = (std::max)(ShadowConfig.ShadowMapSize, 1u);
		}
		else if (resolvedLightType == pointLightType)
		{
			const UINT globalSlotIndex = static_cast<UINT>(shadowBaseIndex);
			if (globalSlotIndex < texture2DSlotCount)
				continue;
			const UINT cubeIndex = globalSlotIndex - texture2DSlotCount;
			if (cubeIndex < pointLightCubeSizes.size())
				pointLightCubeSizes[cubeIndex] = (std::max)(ShadowConfig.ShadowMapSize, 1u);
		}
	}

	const bool needRebuildTexture2D = !shadowMapPass.MatchesLayout(texture2DSizes);

	// 先判断 point cube 是否需要重建。
	// 注意：旧 cube 的 DSV 槽位和新 2D shadow 的 DSV 槽位可能复用同一段 heap。
	// 因此必须先清掉旧 cube/旧 2D 的 descriptor，再创建新资源；
	// 否则“后清理”会把刚创建好的新 DSV 再刷成 null。
	bool needRebuildPointLightCubes =
		!shadowMapPass.MatchesLayout(texture2DSizes) ||
		(pointLightShadowCubePool.GetCubeCount() != pointLightCubeCount);
	if (!needRebuildPointLightCubes)
	{
		for (UINT cubeIndex = 0; cubeIndex < pointLightCubeCount; ++cubeIndex)
		{
			if (!pointLightShadowCubePool.HasCube(cubeIndex))
			{
				needRebuildPointLightCubes = true;
				break;
			}
		}
	}

	const bool needRebuildStaticShadowCaches =
		needRebuildTexture2D ||
		needRebuildPointLightCubes ||
		!StaticShadowCacheResourcesMatchLayout(
			StaticShadowCache2DResources,
			texture2DSizes,
			StaticPointLightShadowCubeResources,
			pointLightCubeSizes);

	if (needRebuildTexture2D || needRebuildPointLightCubes || needRebuildStaticShadowCaches)
	{
		if (shadowMapPass.GetHeapIndexSize() > 0 ||
			pointLightShadowCubePool.GetCubeCount() > 0 ||
			!StaticShadowCache2DResources.empty() ||
			!StaticPointLightShadowCubeResources.empty())
		{
			FlushCommandQueue();
		}
	}

	if (needRebuildPointLightCubes)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nullCubeSrvDesc = {};
		nullCubeSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullCubeSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		nullCubeSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		nullCubeSrvDesc.TextureCube.MostDetailedMip = 0;
		nullCubeSrvDesc.TextureCube.MipLevels = 1;
		nullCubeSrvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		D3D12_DEPTH_STENCIL_VIEW_DESC nullCubeDsvDesc = {};
		nullCubeDsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		nullCubeDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		nullCubeDsvDesc.Format = DepthStencilFormat;
		nullCubeDsvDesc.Texture2DArray.MipSlice = 0;
		nullCubeDsvDesc.Texture2DArray.FirstArraySlice = 0;
		nullCubeDsvDesc.Texture2DArray.ArraySize = 1;

		for (UINT cubeIndex = 0; cubeIndex < pointLightShadowCubePool.GetCubeCount(); ++cubeIndex)
		{
			const auto* cubeEntry = pointLightShadowCubePool.GetCubeEntry(cubeIndex);
			if (cubeEntry == nullptr)
				continue;

			CD3DX12_CPU_DESCRIPTOR_HANDLE cubeSrvHandle(
				SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
				cubeEntry->SrvHeapIndex,
				CbvSrvUavDescriptorSize);
			d3dDevice->CreateShaderResourceView(nullptr, &nullCubeSrvDesc, cubeSrvHandle);

			for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex)
				d3dDevice->CreateDepthStencilView(nullptr, &nullCubeDsvDesc, cubeEntry->DsvHandles[faceIndex]);
		}

		pointLightShadowCubePool.Clear();
	}

	// 创建/重建 Texture2D 阴影贴图
	if (needRebuildTexture2D)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nullShadowSrvDesc = {};
		nullShadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullShadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		nullShadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullShadowSrvDesc.Texture2D.MostDetailedMip = 0;
		nullShadowSrvDesc.Texture2D.MipLevels = 1;
		nullShadowSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		nullShadowSrvDesc.Texture2D.PlaneSlice = 0;

		D3D12_DEPTH_STENCIL_VIEW_DESC nullShadowDsvDesc = {};
		nullShadowDsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		nullShadowDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		nullShadowDsvDesc.Format = DepthStencilFormat;
		nullShadowDsvDesc.Texture2D.MipSlice = 0;

		for (UINT shadowIndex = 0; shadowIndex < shadowMapPass.GetHeapIndexSize(); ++shadowIndex)
		{
			const UINT shadowSrvIndex = shadowMapPass.GetHeapIndex(shadowIndex);
			CD3DX12_CPU_DESCRIPTOR_HANDLE shadowSrvHandle(
				SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
				shadowSrvIndex,
				CbvSrvUavDescriptorSize);
			d3dDevice->CreateShaderResourceView(nullptr, &nullShadowSrvDesc, shadowSrvHandle);

			const D3D12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle = shadowMapPass.GetDsvHandle(shadowIndex);
			d3dDevice->CreateDepthStencilView(nullptr, &nullShadowDsvDesc, shadowDsvHandle);
		}

		shadowMapPass.Clear();
		for (UINT slotIndex = 0; slotIndex < texture2DSizes.size(); ++slotIndex)
		{
			const UINT slotSize = (std::max)(texture2DSizes[slotIndex], 1u);
			CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart(), 1 + slotIndex, DsvDescriptorSize);
			UINT shadowSrvIndex = ShadowMapHeapStartIndex + slotIndex;
			if (slotIndex >= CurrentShadowPoolPlan.SpotPool.StartSlot)
			{
				const UINT localSpotIndex = slotIndex - CurrentShadowPoolPlan.SpotPool.StartSlot;
				shadowSrvIndex = ShadowMapHeapStartIndex + ShadowPoolLimits::DirectionalTextureCount + localSpotIndex;
			}
			CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
				SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
				shadowSrvIndex,
				CbvSrvUavDescriptorSize);
			shadowMapPass.AddShadowMap(
				L"shadowmap" + std::to_wstring(slotIndex),
				DepthStencilFormat,
				dsvHandle,
				srvHandle,
				shadowSrvIndex,
				slotSize,
				slotSize);
		}
	}

	if (needRebuildPointLightCubes)
	{
		// 点光源 cubemap 的 DSV 从 Texture2D 槽位之后开始
		const UINT pointLightDsvStartIndex = 1 + texture2DSlotCount;
		// 点光源 cubemap 的 SRV 放到独立的 cube 池描述符区。
		const UINT pointLightSrvStartIndex = ShadowMapHeapStartIndex + ShadowPoolLimits::Combined2DTextureCount;

		for (UINT cubeIndex = 0; cubeIndex < pointLightCubeCount; ++cubeIndex)
		{
			const UINT faceSize = (std::max)(pointLightCubeSizes[cubeIndex], 1u);
			pointLightShadowCubePool.AddCube(
				L"pointLightShadowCube" + std::to_wstring(cubeIndex),
				DepthStencilFormat,
				pointLightDsvStartIndex + cubeIndex * 6,
				pointLightSrvStartIndex + cubeIndex,
				DsvHeap->GetCPUDescriptorHandleForHeapStart(),
				DsvDescriptorSize,
				SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
				CbvSrvUavDescriptorSize,
				faceSize);
		}

		// 即使当前没有点光阴影，也把表起点刷新到本帧合法的 shadow SRV 区间内，
		// 避免后续仍保留旧场景的 cube 描述符起点。
		pointLightShadowCubeDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(
			SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			pointLightSrvStartIndex,
			CbvSrvUavDescriptorSize);
	}

	if (needRebuildStaticShadowCaches)
	{
		StaticShadowCache2DResources.clear();
		StaticPointLightShadowCubeResources.clear();

		StaticShadowCache2DResources.reserve(texture2DSizes.size());
		for (UINT slotIndex = 0; slotIndex < texture2DSizes.size(); ++slotIndex)
		{
			const UINT slotSize = (std::max)(texture2DSizes[slotIndex], 1u);
			D3D12_RESOURCE_DESC texDesc = {};
			texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			texDesc.Alignment = 0;
			texDesc.Width = slotSize;
			texDesc.Height = slotSize;
			texDesc.DepthOrArraySize = 1;
			texDesc.MipLevels = 1;
			texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			D3D12_CLEAR_VALUE clearValue = {};
			clearValue.Format = DepthStencilFormat;
			clearValue.DepthStencil.Depth = 1.0f;
			clearValue.DepthStencil.Stencil = 0;

			ShadowCache2DResource cacheResource;
			cacheResource.Width = slotSize;
			cacheResource.Height = slotSize;
			cacheResource.State = D3D12_RESOURCE_STATE_GENERIC_READ;

			D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			ThrowIfFailed(d3dDevice->CreateCommittedResource(
				&heapProperties,
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				cacheResource.State,
				&clearValue,
				IID_PPV_ARGS(cacheResource.Resource.GetAddressOf())));

			StaticShadowCache2DResources.push_back(std::move(cacheResource));
		}

		StaticPointLightShadowCubeResources.reserve(pointLightCubeSizes.size());
		for (UINT cubeIndex = 0; cubeIndex < pointLightCubeSizes.size(); ++cubeIndex)
		{
			const UINT faceSize = (std::max)(pointLightCubeSizes[cubeIndex], 1u);
			D3D12_RESOURCE_DESC texDesc = {};
			texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			texDesc.Alignment = 0;
			texDesc.Width = faceSize;
			texDesc.Height = faceSize;
			texDesc.DepthOrArraySize = 6;
			texDesc.MipLevels = 1;
			texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			texDesc.SampleDesc.Count = 1;
			texDesc.SampleDesc.Quality = 0;
			texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			D3D12_CLEAR_VALUE clearValue = {};
			clearValue.Format = DepthStencilFormat;
			clearValue.DepthStencil.Depth = 1.0f;
			clearValue.DepthStencil.Stencil = 0;

			ShadowCacheCubeResource cacheResource;
			cacheResource.FaceSize = faceSize;
			cacheResource.State = D3D12_RESOURCE_STATE_GENERIC_READ;

			D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			ThrowIfFailed(d3dDevice->CreateCommittedResource(
				&heapProperties,
				D3D12_HEAP_FLAG_NONE,
				&texDesc,
				cacheResource.State,
				&clearValue,
				IID_PPV_ARGS(cacheResource.Resource.GetAddressOf())));

			StaticPointLightShadowCubeResources.push_back(std::move(cacheResource));
		}
	}

	WorkingShadowMapStates.assign(texture2DSizes.size(), D3D12_RESOURCE_STATE_GENERIC_READ);
	WorkingPointLightShadowCubeStates.assign(pointLightCubeSizes.size(), D3D12_RESOURCE_STATE_GENERIC_READ);
}

bool D3DWindow::ResolvePointLightCubeIndex(
	UINT firstPointCubeSlot,
	UINT shadowSlotIndex,
	UINT cubeCount,
	UINT* outCubeIndex)
{
	if (outCubeIndex == nullptr)
		return false;
	if (shadowSlotIndex < firstPointCubeSlot)
		return false;

	const UINT cubeIndex = shadowSlotIndex - firstPointCubeSlot;
	if (cubeIndex >= cubeCount)
		return false;

	*outCubeIndex = cubeIndex;
	return true;
}

void D3DWindow::BeginWorkerPass(UINT workerPhaseIndex)
{
	// 主线程按“工作阶段”广播开始事件，工作线程据此录制对应命令列表。
	for (UINT threadIndex = 0; threadIndex < NumContexts; ++threadIndex)
	{
		if (!ResetEvent(workerFinishedRecordCommand[threadIndex]))
		{
			const DWORD errorCode = GetLastError();
			ThrowIfFailed(HRESULT_FROM_WIN32(errorCode != 0 ? errorCode : ERROR_INVALID_HANDLE));
		}
		if (!SetEvent(workerBeginRecordCommand[workerPhaseIndex][threadIndex]))
		{
			const DWORD errorCode = GetLastError();
			ThrowIfFailed(HRESULT_FROM_WIN32(errorCode != 0 ? errorCode : ERROR_INVALID_HANDLE));
		}
	}
}

UINT D3DWindow::ResolveWorkerPhaseIndexFromWaitResult(DWORD waitResult) const
{
	switch (waitResult)
	{
	case WAIT_OBJECT_0 + 0:
		return 阴影工作阶段;
	case WAIT_OBJECT_0 + 1:
		return 不透明工作阶段;
	case WAIT_OBJECT_0 + 2:
		return 半透明工作阶段;
	case WAIT_OBJECT_0 + 3:
		return 透明工作阶段;
	case WAIT_OBJECT_0 + 4:
		return 法线工作阶段;
	default:
		return 工作阶段计数;
	}
}

void D3DWindow::BindWorkerScenePassCommonState(
	ID3D12GraphicsCommandList* workerCommandList,
	ID3D12DescriptorHeap* const* srvDescriptorHeaps,
	UINT srvHeapCount)
{
	workerCommandList->SetGraphicsRootSignature(RootSignature.Get());
	workerCommandList->SetDescriptorHeaps(srvHeapCount, srvDescriptorHeaps);
	workerCommandList->SetGraphicsRootConstantBufferView(1, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
	workerCommandList->SetGraphicsRootConstantBufferView(2, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
	workerCommandList->SetGraphicsRootDescriptorTable(5, skyTexDescriptor);
	workerCommandList->SetGraphicsRootDescriptorTable(6, otherTexDescriptor);
	workerCommandList->SetGraphicsRootDescriptorTable(7, shadow2DDescriptorTable);
	workerCommandList->SetGraphicsRootDescriptorTable(8, pointLightShadowCubeDescriptor);
	workerCommandList->SetGraphicsRootDescriptorTable(9, ambientOcclusionDescriptor);
	workerCommandList->SetGraphicsRootDescriptorTable(10, directionalShadowMaskDescriptor);
	workerCommandList->RSSetViewports(1, &m_viewport);
	workerCommandList->RSSetScissorRects(1, &m_scissorRect);
}

void D3DWindow::RecordWorkerShadow2DPassEntries(
	int threadIndex,
	ID3D12GraphicsCommandList* workerCommandList,
	const std::vector<RenderItem*>& staticShadowItems,
	const std::vector<RenderItem*>& dynamicShadowItems,
	const std::vector<ShadowRenderEntry>& shadowEntries,
	const ShadowMapPass::ShadowMapLayout& shadowLayout,
	int directionalLightType)
{
	for (const ShadowRenderEntry& shadowEntry : shadowEntries)
	{
		if (shadowEntry.ViewKind == ShadowViewKind::PointFace)
			continue;

		ComPtr<ID3D12PipelineState> shadowPipeline = PipelineState[阴影管道];
		if (shadowEntry.LightIndex < LightsCache.size())
		{
			const int entryLightType = static_cast<int>(std::lround(LightsCache[shadowEntry.LightIndex].Type));
			if (entryLightType == directionalLightType)
			{
				const UINT cascadeIndex = (std::min)(shadowEntry.ViewIndex, 3u);
				if (DirectionalCascadeShadowPipelineState[cascadeIndex] != nullptr)
					shadowPipeline = DirectionalCascadeShadowPipelineState[cascadeIndex];
			}
		}

		ShadowUpdateRequestType updateType =
			ShadowRuntimeHelpers::ResolveShadowEntryUpdateType(CurrentShadowUpdateRequests, shadowEntry.LightIndex);

		const UINT shadowSlotIndex = shadowEntry.ShadowSlotIndex;
		if (shadowSlotIndex >= CurrentShadowPoolPlan.PointPool.StartSlot)
			continue;
		if (!ShadowRuntimeHelpers::IsShadowSlotOwnedByThread(shadowSlotIndex, threadIndex))
			continue;

		ComPtr<ID3D12Resource> workingShadowResource = shadowMapPass.GetResource(shadowSlotIndex);
		if (workingShadowResource == nullptr || shadowSlotIndex >= WorkingShadowMapStates.size())
			continue;

		const bool hasStaticCache =
			shadowSlotIndex < StaticShadowCache2DResources.size() &&
			StaticShadowCache2DResources[shadowSlotIndex].Resource != nullptr;
		if (!ShadowRuntimeHelpers::SupportsShadowStaticCache(
			shadowEntry.LightIndex < CurrentShadowUpdateRequests.size()
				? CurrentShadowUpdateRequests[shadowEntry.LightIndex].Pool
				: ShadowPoolPlanner::PoolKind::None))
		{
			updateType = ShadowUpdateRequestType::FullUpdate;
		}
		if (updateType == ShadowUpdateRequestType::DynamicOnlyUpdate && !hasStaticCache)
			updateType = ShadowUpdateRequestType::FullUpdate;
		if (updateType == ShadowUpdateRequestType::None)
			continue;

		const std::vector<RenderItem*>* staticShadowItemsForEntry = &staticShadowItems;
		const std::vector<RenderItem*>* dynamicShadowItemsForEntry = &dynamicShadowItems;
		std::vector<RenderItem*> culledStaticShadowItems;
		std::vector<RenderItem*> culledDynamicShadowItems;

		// 方向光 CSM 暂不启用 CPU 侧 caster culling。
		// 旧逻辑用 receiver 的 light-space 投影盒裁剪 caster，会漏掉投影盒外但能沿光照方向投进当前 cascade 的物体，
		// 表现为镜头远离场景中心时阴影被柔和切断或局部错误变亮。
		// 后续如果要恢复性能优化，应改成“receiver 沿光照方向外扩/挤出”的保守体，而不是复用 receiver box。
		bool allowDirectionalCascadeCasterCulling = false;
		if (allowDirectionalCascadeCasterCulling &&
			shadowEntry.ViewKind == ShadowViewKind::DirectionalCascade &&
			shadowEntry.HasLightSpaceCullBounds)
		{
			culledStaticShadowItems.reserve(staticShadowItems.size());
			for (RenderItem* renderItem : staticShadowItems)
			{
				if (renderItem != nullptr &&
					RenderInfluenceHelpers::IntersectsDirectionalCascadeCullBounds(shadowEntry, *renderItem))
				{
					culledStaticShadowItems.push_back(renderItem);
				}
			}

			culledDynamicShadowItems.reserve(dynamicShadowItems.size());
			for (RenderItem* renderItem : dynamicShadowItems)
			{
				if (renderItem != nullptr &&
					RenderInfluenceHelpers::IntersectsDirectionalCascadeCullBounds(shadowEntry, *renderItem))
				{
					culledDynamicShadowItems.push_back(renderItem);
				}
			}

			staticShadowItemsForEntry = &culledStaticShadowItems;
			dynamicShadowItemsForEntry = &culledDynamicShadowItems;
		}

		shadowMapPass.SetRenderTargets(workerCommandList, shadowSlotIndex);
		if (updateType == ShadowUpdateRequestType::FullUpdate)
		{
			const D3D12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle = shadowMapPass.GetDsvHandle(shadowSlotIndex);
			TransitionTrackedResourceState(
				workerCommandList,
				workingShadowResource.Get(),
				WorkingShadowMapStates[shadowSlotIndex],
				D3D12_RESOURCE_STATE_DEPTH_WRITE);
			workerCommandList->ClearDepthStencilView(shadowDsvHandle,
				D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
			DrawRenderItems(workerCommandList, *staticShadowItemsForEntry, shadowPipeline, 阴影管道, shadowEntry.PassCBIndex);

			if (hasStaticCache)
			{
				ShadowCache2DResource& cacheResource = StaticShadowCache2DResources[shadowSlotIndex];
				TransitionTrackedResourceState(
					workerCommandList,
					workingShadowResource.Get(),
					WorkingShadowMapStates[shadowSlotIndex],
					D3D12_RESOURCE_STATE_COPY_SOURCE);
				TransitionTrackedResourceState(
					workerCommandList,
					cacheResource.Resource.Get(),
					cacheResource.State,
					D3D12_RESOURCE_STATE_COPY_DEST);
				workerCommandList->CopyResource(cacheResource.Resource.Get(), workingShadowResource.Get());
				TransitionTrackedResourceState(
					workerCommandList,
					cacheResource.Resource.Get(),
					cacheResource.State,
					D3D12_RESOURCE_STATE_COPY_SOURCE);
				TransitionTrackedResourceState(
					workerCommandList,
					workingShadowResource.Get(),
					WorkingShadowMapStates[shadowSlotIndex],
					D3D12_RESOURCE_STATE_DEPTH_WRITE);
			}
		}
		else if (updateType == ShadowUpdateRequestType::DynamicOnlyUpdate)
		{
			ShadowCache2DResource& cacheResource = StaticShadowCache2DResources[shadowSlotIndex];
			TransitionTrackedResourceState(
				workerCommandList,
				cacheResource.Resource.Get(),
				cacheResource.State,
				D3D12_RESOURCE_STATE_COPY_SOURCE);
			TransitionTrackedResourceState(
				workerCommandList,
				workingShadowResource.Get(),
				WorkingShadowMapStates[shadowSlotIndex],
				D3D12_RESOURCE_STATE_COPY_DEST);
			workerCommandList->CopyResource(workingShadowResource.Get(), cacheResource.Resource.Get());
			TransitionTrackedResourceState(
				workerCommandList,
				workingShadowResource.Get(),
				WorkingShadowMapStates[shadowSlotIndex],
				D3D12_RESOURCE_STATE_DEPTH_WRITE);
			shadowMapPass.SetRenderTargets(workerCommandList, shadowSlotIndex);
		}

		DrawRenderItems(workerCommandList, *dynamicShadowItemsForEntry, shadowPipeline, 阴影管道, shadowEntry.PassCBIndex);
	}
}

void D3DWindow::RecordWorkerPointLightShadowCubePassEntries(
	int threadIndex,
	ID3D12GraphicsCommandList* workerCommandList,
	const std::vector<RenderItem*>& staticShadowItems,
	const std::vector<RenderItem*>& dynamicShadowItems,
	const std::vector<ShadowRenderEntry>& shadowEntries,
	const ShadowMapPass::ShadowMapLayout& shadowLayout)
{
	struct PointCubeEntryGroup
	{
		UINT CubeIndex = 0;
		ShadowUpdateRequestType UpdateType = ShadowUpdateRequestType::None;
		std::vector<ShadowRenderEntry> FaceEntries;
	};

	std::vector<PointCubeEntryGroup> cubeGroups;
	cubeGroups.reserve(pointLightShadowCubePool.GetCubeCount());
	for (const ShadowRenderEntry& shadowEntry : shadowEntries)
	{
		if (shadowEntry.ViewKind != ShadowViewKind::PointFace)
			continue;

		UINT cubeIndex = 0;
		if (!ResolvePointLightCubeIndex(
			CurrentShadowPoolPlan.PointPool.StartSlot,
			shadowEntry.ShadowSlotIndex,
			pointLightShadowCubePool.GetCubeCount(),
			&cubeIndex))
		{
			continue;
		}

		if (cubeIndex >= cubeGroups.size())
			cubeGroups.resize(cubeIndex + 1);

		PointCubeEntryGroup& group = cubeGroups[cubeIndex];
		group.CubeIndex = cubeIndex;
		group.UpdateType = ShadowRuntimeHelpers::MergeShadowUpdateRequestType(
			group.UpdateType,
			ShadowRuntimeHelpers::ResolveShadowEntryUpdateType(CurrentShadowUpdateRequests, shadowEntry.LightIndex));
		group.FaceEntries.push_back(shadowEntry);
	}

	for (PointCubeEntryGroup& group : cubeGroups)
	{
		if (group.FaceEntries.empty())
			continue;

		UINT cubeIndex = group.CubeIndex;
		const UINT shadowSlotIndex = CurrentShadowPoolPlan.PointPool.StartSlot + cubeIndex;
		if (!ShadowRuntimeHelpers::IsShadowSlotOwnedByThread(shadowSlotIndex, threadIndex))
			continue;

		ShadowUpdateRequestType updateType = group.UpdateType;
		ComPtr<ID3D12Resource> workingCubeResource = pointLightShadowCubePool.GetCubeResource(cubeIndex);
		if (workingCubeResource == nullptr || cubeIndex >= WorkingPointLightShadowCubeStates.size())
			continue;

		const bool hasStaticCache =
			cubeIndex < StaticPointLightShadowCubeResources.size() &&
			StaticPointLightShadowCubeResources[cubeIndex].Resource != nullptr;
		if (!ShadowRuntimeHelpers::SupportsShadowStaticCache(
			group.FaceEntries.front().LightIndex < CurrentShadowUpdateRequests.size()
				? CurrentShadowUpdateRequests[group.FaceEntries.front().LightIndex].Pool
				: ShadowPoolPlanner::PoolKind::None))
		{
			updateType = ShadowUpdateRequestType::FullUpdate;
		}
		if (updateType == ShadowUpdateRequestType::DynamicOnlyUpdate && !hasStaticCache)
			updateType = ShadowUpdateRequestType::FullUpdate;
		if (updateType == ShadowUpdateRequestType::None)
			continue;

		if (updateType == ShadowUpdateRequestType::FullUpdate)
		{
			TransitionTrackedResourceState(
				workerCommandList,
				workingCubeResource.Get(),
				WorkingPointLightShadowCubeStates[cubeIndex],
				D3D12_RESOURCE_STATE_DEPTH_WRITE);

			for (const ShadowRenderEntry& faceEntry : group.FaceEntries)
			{
				const D3D12_VIEWPORT& cubeViewport = pointLightShadowCubePool.GetCubeViewport(cubeIndex);
				const D3D12_RECT& cubeScissorRect = pointLightShadowCubePool.GetCubeScissorRect(cubeIndex);
				const D3D12_CPU_DESCRIPTOR_HANDLE pointCubeDsv = pointLightShadowCubePool.GetFaceDsv(cubeIndex, faceEntry.ViewIndex);
				workerCommandList->RSSetViewports(1, &cubeViewport);
				workerCommandList->RSSetScissorRects(1, &cubeScissorRect);
				workerCommandList->OMSetRenderTargets(0, nullptr, false, &pointCubeDsv);
				workerCommandList->ClearDepthStencilView(pointCubeDsv,
					D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
				DrawRenderItems(workerCommandList, staticShadowItems, PipelineState[阴影管道], 阴影管道, faceEntry.PassCBIndex);
			}

			if (hasStaticCache)
			{
				ShadowCacheCubeResource& cacheResource = StaticPointLightShadowCubeResources[cubeIndex];
				TransitionTrackedResourceState(
					workerCommandList,
					workingCubeResource.Get(),
					WorkingPointLightShadowCubeStates[cubeIndex],
					D3D12_RESOURCE_STATE_COPY_SOURCE);
				TransitionTrackedResourceState(
					workerCommandList,
					cacheResource.Resource.Get(),
					cacheResource.State,
					D3D12_RESOURCE_STATE_COPY_DEST);
				workerCommandList->CopyResource(cacheResource.Resource.Get(), workingCubeResource.Get());
				TransitionTrackedResourceState(
					workerCommandList,
					cacheResource.Resource.Get(),
					cacheResource.State,
					D3D12_RESOURCE_STATE_COPY_SOURCE);
				TransitionTrackedResourceState(
					workerCommandList,
					workingCubeResource.Get(),
					WorkingPointLightShadowCubeStates[cubeIndex],
					D3D12_RESOURCE_STATE_DEPTH_WRITE);
			}
		}
		else if (updateType == ShadowUpdateRequestType::DynamicOnlyUpdate)
		{
			ShadowCacheCubeResource& cacheResource = StaticPointLightShadowCubeResources[cubeIndex];
			TransitionTrackedResourceState(
				workerCommandList,
				cacheResource.Resource.Get(),
				cacheResource.State,
				D3D12_RESOURCE_STATE_COPY_SOURCE);
			TransitionTrackedResourceState(
				workerCommandList,
				workingCubeResource.Get(),
				WorkingPointLightShadowCubeStates[cubeIndex],
				D3D12_RESOURCE_STATE_COPY_DEST);
			workerCommandList->CopyResource(workingCubeResource.Get(), cacheResource.Resource.Get());
			TransitionTrackedResourceState(
				workerCommandList,
				workingCubeResource.Get(),
				WorkingPointLightShadowCubeStates[cubeIndex],
				D3D12_RESOURCE_STATE_DEPTH_WRITE);
		}

		for (const ShadowRenderEntry& faceEntry : group.FaceEntries)
		{
			const D3D12_VIEWPORT& cubeViewport = pointLightShadowCubePool.GetCubeViewport(cubeIndex);
			const D3D12_RECT& cubeScissorRect = pointLightShadowCubePool.GetCubeScissorRect(cubeIndex);
			const D3D12_CPU_DESCRIPTOR_HANDLE pointCubeDsv =
				pointLightShadowCubePool.GetFaceDsv(cubeIndex, faceEntry.ViewIndex);
			workerCommandList->RSSetViewports(1, &cubeViewport);
			workerCommandList->RSSetScissorRects(1, &cubeScissorRect);
			workerCommandList->OMSetRenderTargets(0, nullptr, false, &pointCubeDsv);
			DrawRenderItems(workerCommandList, dynamicShadowItems, PipelineState[阴影管道], 阴影管道, faceEntry.PassCBIndex);
		}
	}
}

void D3DWindow::RecordWorkerShadowPass(
	int threadIndex,
	ID3D12GraphicsCommandList* workerCommandList,
	const std::vector<RenderItem*>& opaqueRenderBatch,
	const std::vector<RenderItem*>& transparentRenderBatch,
	const std::vector<RenderItem*>& staticShadowCasterRenderItems,
	const std::vector<RenderItem*>& dynamicShadowCasterRenderItems,
	ID3D12DescriptorHeap* const* srvDescriptorHeaps,
	UINT srvHeapCount)
{
	// 阴影阶段沿用独立的 pass 常量索引与 shadow map 目标，因此不复用常规场景 pass 的 CBV 绑定。
	workerCommandList->SetGraphicsRootSignature(RootSignature.Get());
	workerCommandList->SetDescriptorHeaps(srvHeapCount, srvDescriptorHeaps);
	workerCommandList->SetGraphicsRootDescriptorTable(5, skyTexDescriptor);
	workerCommandList->SetGraphicsRootDescriptorTable(6, otherTexDescriptor);
	workerCommandList->SetGraphicsRootDescriptorTable(7, shadow2DDescriptorTable);
	workerCommandList->SetGraphicsRootDescriptorTable(8, pointLightShadowCubeDescriptor);
	workerCommandList->SetGraphicsRootDescriptorTable(9, ambientOcclusionDescriptor);
	workerCommandList->SetGraphicsRootDescriptorTable(10, directionalShadowMaskDescriptor);

	const int directionalLightType = static_cast<int>(std::lround(ShadowConfig.DirectionalLightType));
	const ShadowMapPass::ShadowMapLayout shadowLayout = shadowMapPass.GetLayout();

	std::vector<ShadowRenderEntry> shadow2DEntries;
	std::vector<ShadowRenderEntry> pointCubeEntries;
	shadow2DEntries.reserve(ShadowRenderEntries.size());
	pointCubeEntries.reserve(ShadowRenderEntries.size());
	for (const ShadowRenderEntry& entry : ShadowRenderEntries)
	{
		if (!ShadowRuntimeHelpers::ShouldRecordShadowUpdateRequest(CurrentShadowUpdateRequests, entry.LightIndex))
			continue;
		if (entry.ViewKind == ShadowViewKind::PointFace)
			pointCubeEntries.push_back(entry);
		else
			shadow2DEntries.push_back(entry);
	}

	const std::vector<RenderItem*>* staticShadowItems = &opaqueRenderBatch;
	const std::vector<RenderItem*>* dynamicShadowItems = &transparentRenderBatch;
	if (!staticShadowCasterRenderItems.empty() || !dynamicShadowCasterRenderItems.empty())
	{
		staticShadowItems = &staticShadowCasterRenderItems;
		dynamicShadowItems = &dynamicShadowCasterRenderItems;
	}

	RecordWorkerShadow2DPassEntries(
		threadIndex,
		workerCommandList,
		*staticShadowItems,
		*dynamicShadowItems,
		shadow2DEntries,
		shadowLayout,
		directionalLightType);

	RecordWorkerPointLightShadowCubePassEntries(
		threadIndex,
		workerCommandList,
		*staticShadowItems,
		*dynamicShadowItems,
		pointCubeEntries,
		shadowLayout);

	// 并行安全约束：
	// - shadow entry 先按逻辑 slot/cube ownership 分发给线程；
	// - 每个线程只切换并写入自己拥有的 working/cache 资源状态；
	// - point light 的 6 个面按整 cube 归属同一线程，避免跨线程改同一资源。
	// 因此这里可以在本线程独立把自己写过的 working slice 切回采样态。
	{
		for (UINT shadowIndex = 0; shadowIndex < shadowMapPass.GetHeapIndexSize(); ++shadowIndex)
		{
			if (!ShadowRuntimeHelpers::IsShadowSlotOwnedByThread(shadowIndex, threadIndex))
				continue;
			if (!ShadowRuntimeHelpers::WasShadowSlotWrittenThisFrame(ShadowRenderEntries, CurrentShadowUpdateRequests, shadowIndex))
				continue;

			auto shadowResource = shadowMapPass.GetResource(shadowIndex);
			if (shadowResource != nullptr && shadowIndex < WorkingShadowMapStates.size())
			{
				TransitionTrackedResourceState(
					workerCommandList,
					shadowResource.Get(),
					WorkingShadowMapStates[shadowIndex],
					D3D12_RESOURCE_STATE_GENERIC_READ);
			}
		}

		for (UINT cubeIndex = 0; cubeIndex < pointLightShadowCubePool.GetCubeCount(); ++cubeIndex)
		{
			const UINT shadowSlotIndex = CurrentShadowPoolPlan.PointPool.StartSlot + cubeIndex;
			if (!ShadowRuntimeHelpers::IsShadowSlotOwnedByThread(shadowSlotIndex, threadIndex))
				continue;
			bool cubeWasWritten = false;
			cubeWasWritten = ShadowRuntimeHelpers::WasShadowSlotWrittenThisFrame(pointCubeEntries, CurrentShadowUpdateRequests, shadowSlotIndex);

			if (cubeWasWritten)
			{
				auto cubeResource = pointLightShadowCubePool.GetCubeResource(cubeIndex);
				if (cubeResource != nullptr && cubeIndex < WorkingPointLightShadowCubeStates.size())
				{
					TransitionTrackedResourceState(
						workerCommandList,
						cubeResource.Get(),
						WorkingPointLightShadowCubeStates[cubeIndex],
						D3D12_RESOURCE_STATE_GENERIC_READ);
				}
			}
		}
	}
}

void D3DWindow::RecordWorkerNormalPass(
	int threadIndex,
	ID3D12GraphicsCommandList* workerCommandList,
	const std::vector<RenderItem*>& opaqueRenderBatch,
	const std::vector<RenderItem*>& transparentRenderBatch,
	ID3D12DescriptorHeap* const* srvDescriptorHeaps,
	UINT srvHeapCount)
{
	sharedNormalPrepass.BeginPass(
		workerCommandList,
		RootSignature.Get(),
		CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
		CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress(),
		skyTexDescriptor,
		otherTexDescriptor,
		shadow2DDescriptorTable,
		pointLightShadowCubeDescriptor,
		ambientOcclusionDescriptor,
		directionalShadowMaskDescriptor,
		srvDescriptorHeaps,
		srvHeapCount,
		threadIndex == 0);

	DrawRenderItems(workerCommandList, opaqueRenderBatch, PipelineState[法线绘制管道], 法线绘制管道);
	DrawRenderItems(workerCommandList, transparentRenderBatch, PipelineState[法线绘制管道], 法线绘制管道);

	if (threadIndex == NumContexts - 1)
		sharedNormalPrepass.EndPass(workerCommandList);
}

void D3DWindow::RecordWorkerOpaquePass(
	ID3D12GraphicsCommandList* workerCommandList,
	const std::vector<RenderItem*>& opaqueRenderBatch,
	const CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
	const CD3DX12_CPU_DESCRIPTOR_HANDLE& dsvHandle,
	ID3D12DescriptorHeap* const* srvDescriptorHeaps,
	UINT srvHeapCount)
{
	workerCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
	BindWorkerScenePassCommonState(workerCommandList, srvDescriptorHeaps, srvHeapCount);
	DrawRenderItems(workerCommandList, opaqueRenderBatch, PipelineState[不透明物体管道], 不透明物体管道);
}

void D3DWindow::RecordWorkerTranslucentPass(
	ID3D12GraphicsCommandList* workerCommandList,
	const std::vector<RenderItem*>& transparentRenderBatch,
	const CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
	const CD3DX12_CPU_DESCRIPTOR_HANDLE& dsvHandle,
	ID3D12DescriptorHeap* const* srvDescriptorHeaps,
	UINT srvHeapCount)
{
	workerCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
	BindWorkerScenePassCommonState(workerCommandList, srvDescriptorHeaps, srvHeapCount);
	DrawRenderItems(workerCommandList, transparentRenderBatch, PipelineState[半透明物体管道], 半透明物体管道);
}

void D3DWindow::RecordWorkerTransparentPass(
	ID3D12GraphicsCommandList* workerCommandList,
	const std::vector<RenderItem*>& transparentRenderBatch,
	const CD3DX12_CPU_DESCRIPTOR_HANDLE& dsvHandle,
	ID3D12DescriptorHeap* const* srvDescriptorHeaps,
	UINT srvHeapCount)
{
	const UINT transparentOitRtvStartIndex = SwapChainBufferCount + 3 + CurrBackBufferIndex * 2;
	CD3DX12_CPU_DESCRIPTOR_HANDLE transparentOitAccumRtv(
		RtvHeap->GetCPUDescriptorHandleForHeapStart(),
		transparentOitRtvStartIndex,
		RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE transparentOitRevealRtv = transparentOitAccumRtv;
	transparentOitRevealRtv.Offset(1, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE transparentOitRtvs[2] =
	{
		transparentOitAccumRtv,
		transparentOitRevealRtv
	};

	workerCommandList->OMSetRenderTargets(2, transparentOitRtvs, false, &dsvHandle);
	BindWorkerScenePassCommonState(workerCommandList, srvDescriptorHeaps, srvHeapCount);
	DrawRenderItems(workerCommandList, transparentRenderBatch, PipelineState[透明物体管道], 透明物体管道);
}

void D3DWindow::WaitForWorkerPass()
{
	// 等待所有工作线程完成本阶段录制，再由主线程统一提交该阶段命令列表。
	const DWORD waitResult = WaitForMultipleObjects(NumContexts, workerFinishedRecordCommand, TRUE, INFINITE);
	if (waitResult != WAIT_OBJECT_0)
	{
		const DWORD errorCode = GetLastError();
		ThrowIfFailed(HRESULT_FROM_WIN32(errorCode != 0 ? errorCode : ERROR_INVALID_HANDLE));
	}
}

void D3DWindow::ExecuteWorkerPassAndSubmit(UINT workerPhaseIndex)
{
	BeginWorkerPass(workerPhaseIndex);
	WaitForWorkerPass();

	ID3D12CommandList* phaseCommandLists[NumContexts] = { nullptr };
	for (UINT threadIndex = 0; threadIndex < NumContexts; ++threadIndex)
	{
		phaseCommandLists[threadIndex] = GetWorkerCommandList(workerPhaseIndex, threadIndex);
	}
	CommandQueue->ExecuteCommandLists(_countof(phaseCommandLists), phaseCommandLists);
}

ID3D12GraphicsCommandList* D3DWindow::GetCurrFrameResourceCommandList()
{
	return CurrFrameResource->EndCommandList.Get();
}

CD3DX12_VIEWPORT D3DWindow::GetViewport()
{
	return m_viewport;
}

void D3DWindow::BeginWorkerThreads()
{
	struct threadwrapper
	{
		static unsigned int WINAPI thunk(LPVOID lpParameter)
		{
			ThreadParameter* parameter = reinterpret_cast<ThreadParameter*>(lpParameter);
			D3DWindow::s_app->WorkerThread(parameter->threadIndex);
			return 0;
		}
	};

	// 子线程现在按工作阶段分段工作。
	for (int threadIndex = 0; threadIndex < NumContexts; ++threadIndex)
	{
		for (UINT workerPhaseIndex = 0; workerPhaseIndex < 工作阶段计数; ++workerPhaseIndex)
		{
			workerBeginRecordCommand[workerPhaseIndex][threadIndex] = CreateAutoResetEventHandle();
		}

		workerFinishedRecordCommand[threadIndex] = CreateManualResetEventHandle();

		threadParameters[threadIndex].threadIndex = threadIndex;

		threadHandles[threadIndex] = reinterpret_cast<HANDLE>(_beginthreadex(
			nullptr,
			0,
			threadwrapper::thunk,
			reinterpret_cast<LPVOID>(&threadParameters[threadIndex]),
			0,
			nullptr));

		for (UINT workerPhaseIndex = 0; workerPhaseIndex < 工作阶段计数; ++workerPhaseIndex)
			assert(workerBeginRecordCommand[workerPhaseIndex][threadIndex] != NULL);
		assert(workerFinishedRecordCommand[threadIndex] != NULL);
		assert(threadHandles[threadIndex] != NULL);
	}
}

void D3DWindow::UpdateCamera()
{
	mCamera.UpdateViewMatrix();
}

void D3DWindow::UpdateObjectCBs()
{
	UpdateBillboardRenderItemsForCamera();

	auto currObjectCB = CurrFrameResource->ObjectCB.get();

	if (FreshenAllObject)
	{
		DirtyObjectCBItems.clear();
		for (auto& renderItemPair : AllRitems)
		{
			renderItemPair.second.NumFramesDirty = SwapChainBufferCount;
			DirtyObjectCBItems.insert(renderItemPair.first);
		}
		FreshenAllObject = false;
	}

	if (DirtyObjectCBItems.empty())
		return;

	std::unordered_set<std::wstring> nextDirtyItems;
	nextDirtyItems.reserve(DirtyObjectCBItems.size());

	for (const std::wstring& renderItemName : DirtyObjectCBItems)
	{
		auto renderItemIt = AllRitems.find(renderItemName);
		if (renderItemIt == AllRitems.end())
			continue;

		RenderItem& renderItem = renderItemIt->second;
		if (renderItem.NumFramesDirty > 0)
		{
			if (!IsFiniteRenderMatrix(renderItem.WorldTransform) || !IsFiniteRenderMatrix(renderItem.TexTransform))
			{
				EmitRenderTransformDebugMessage(
					L"UpdateObjectCBs_InvalidMatrix",
					renderItemName,
					mLastExternalECS,
					renderItem.SourceEntity,
					renderItem.WorldTransform,
					renderItem.TexTransform);
				renderItem.WorldTransform = MathHelps::Identity;
				renderItem.TexTransform = MathHelps::Identity;
			}

			XMMATRIX WorldTransform = XMLoadFloat4x4(&renderItem.WorldTransform);
			XMMATRIX texTransform = XMLoadFloat4x4(&renderItem.TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.WorldTransform, XMMatrixTranspose(WorldTransform));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			currObjectCB->CopyData(renderItem.ObjCBIndex, objConstants);

			// 下一帧资源也需要更新。
			renderItem.NumFramesDirty--;
			if (renderItem.NumFramesDirty > 0)
				nextDirtyItems.insert(renderItemName);
		}
	}

	DirtyObjectCBItems.swap(nextDirtyItems);
}

void D3DWindow::UpdateBillboardRenderItemsForCamera()
{
	if (AllRitems.empty())
		return;

	const DirectX::XMMATRIX view = mCamera.GetView();
	const DirectX::XMMATRIX proj = mCamera.GetProj();
	const DirectX::XMFLOAT3 cameraPosition = mCamera.GetPosition3f();
	const float renderTargetHeight = static_cast<float>(std::max((int)WinInfo.Height, 1));

	for (auto& renderItemPair : AllRitems)
	{
		RenderItem& renderItem = renderItemPair.second;
		if (!renderItem.IsBillboard)
			continue;

		const DirectX::XMFLOAT4X4 resolvedWorld = BuildBillboardWorldTransform(
			renderItem.BillboardAnchorTransform,
			renderItem.Billboard,
			cameraPosition,
			view,
			proj,
			renderTargetHeight);

		const float* lhs = &renderItem.WorldTransform._11;
		const float* rhs = &resolvedWorld._11;
		bool changed = false;
		for (int i = 0; i < 16; ++i)
		{
			if (std::abs(lhs[i] - rhs[i]) > 1e-5f)
			{
				changed = true;
				break;
			}
		}

		if (!changed)
			continue;

		renderItem.WorldTransform = resolvedWorld;
		renderItem.NumFramesDirty = SwapChainBufferCount;
		DirtyObjectCBItems.insert(renderItemPair.first);
	}
}

void D3DWindow::FreshenObjectCBs()
{
	FreshenAllObject = true;
}

void D3DWindow::FreshenObjectCBs(const std::wstring& renderItemName)
{
	auto renderItemIt = AllRitems.find(renderItemName);
	if (renderItemIt == AllRitems.end())
		return;

	renderItemIt->second.NumFramesDirty = SwapChainBufferCount;
	DirtyObjectCBItems.insert(renderItemName);
}

void D3DWindow::UpdateSkinningCBs()
{
	if (CurrFrameResource == nullptr || CurrFrameResource->SkinningCB == nullptr || mLastExternalECS == nullptr)
		return;

	auto currSkinningCB = CurrFrameResource->SkinningCB.get();
	SkinningConstants skinningConstants = {};

	for (auto& renderItemPair : AllRitems)
	{
		RenderItem& renderItem = renderItemPair.second;
		if (renderItem.SourceEntity == nullptr)
			continue;

		SkinnedMeshComponent* skinnedMeshComponent =
			mLastExternalECS->GetComponent<SkinnedMeshComponent>(renderItem.SourceEntity);
		if (skinnedMeshComponent == nullptr)
		{
			renderItem.IsSkinned = false;
			continue;
		}

		// RenderItem 是派生缓存，SkinnedMeshComponent 才是 ECS 中的权威状态。
		// 防止资源路径为空或旧缓存状态把有效蒙皮网格错误降级成静态渲染项。
		renderItem.IsSkinned = true;
		if (renderItem.SkinningCBIndex == UINT(-1))
			continue;

		SceneEntityBase* runtimeOwnerEntity = renderItem.SourceEntity;
		SkinningRuntimeComponent* runtimeComponent =
			mLastExternalECS->GetComponent<SkinningRuntimeComponent>(renderItem.SourceEntity);
		if (runtimeComponent == nullptr)
		{
			for (SceneEntityBase* parentEntity = mLastExternalECS->GetParentEntity(renderItem.SourceEntity);
				parentEntity != nullptr && runtimeComponent == nullptr;
				parentEntity = mLastExternalECS->GetParentEntity(parentEntity))
			{
				runtimeComponent = mLastExternalECS->GetComponent<SkinningRuntimeComponent>(parentEntity);
				if (runtimeComponent != nullptr)
					runtimeOwnerEntity = parentEntity;
			}
		}
		if (runtimeComponent == nullptr)
			continue;

		ResetSkinningConstantsToIdentity(&skinningConstants);
		const auto& palette = runtimeComponent->GetPalette();
		const UINT boneCount = (std::min)(
			static_cast<UINT>(palette.FinalBoneMatrices.size()),
			static_cast<UINT>(MaxSkinBonesPerDraw));
		bool hasInvalidBoneMatrix = false;
		for (UINT boneIndex = 0; boneIndex < boneCount; ++boneIndex)
		{
			if (!IsFiniteSkinningMatrix(palette.FinalBoneMatrices[boneIndex]))
			{
				hasInvalidBoneMatrix = true;
				break;
			}

			const DirectX::XMMATRIX boneMatrix = DirectX::XMLoadFloat4x4(&palette.FinalBoneMatrices[boneIndex]);
			DirectX::XMStoreFloat4x4(
				&skinningConstants.BoneMatrices[boneIndex],
				DirectX::XMMatrixTranspose(boneMatrix));
		}

		if (hasInvalidBoneMatrix)
		{
			wchar_t debugText[256] = {};
			swprintf_s(
				debugText,
				L"[Skinning] 检测到非法骨骼矩阵，已回退为 identity palette：renderItem=%s boneCount=%u\n",
				renderItemPair.first.c_str(),
				boneCount);
			::OutputDebugStringW(debugText);
			ResetSkinningConstantsToIdentity(&skinningConstants);
		}

		currSkinningCB->CopyData(renderItem.SkinningCBIndex, skinningConstants);
	}
}

std::filesystem::path D3DWindow::ResolveProjectAssetPath(const std::wstring& assetPath)
{
	if (assetPath.empty())
		return {};

	const std::wstring normalizedAssetPath = D3DWindowAssetHelpers::NormalizeAssetPath(assetPath);
	std::filesystem::path candidatePath(normalizedAssetPath);
	if (candidatePath.is_absolute())
		return candidatePath.lexically_normal();

	const std::filesystem::path currentDir = std::filesystem::current_path();
	const std::filesystem::path projectDir = std::filesystem::path(EngineUtils::GetProjectDirPath());
	const std::array<std::filesystem::path, 4> probeRoots =
	{
		currentDir,
		currentDir / L"WitchcraftEngine",
		projectDir,
		projectDir.parent_path()
	};

	for (const std::filesystem::path& probeRoot : probeRoots)
	{
		if (probeRoot.empty())
			continue;

		const std::filesystem::path resolvedPath = (probeRoot / candidatePath).lexically_normal();
		if (std::filesystem::exists(resolvedPath))
			return resolvedPath;
	}

	return (projectDir / candidatePath).lexically_normal();
}

bool D3DWindow::EnsureSkinnedDeformCacheEntry(RenderItem& renderItem, const std::wstring& renderItemName)
{
	if (!kEnableComputeSkinning)
		return false;

	if (!renderItem.IsSkinned || renderItem.SourceEntity == nullptr || mLastExternalECS == nullptr)
		return false;

	SkinnedMeshComponent* skinnedMeshComponent =
		mLastExternalECS->GetComponent<SkinnedMeshComponent>(renderItem.SourceEntity);
	if (skinnedMeshComponent == nullptr)
		return false;

	const std::wstring normalizedMeshAssetPath =
		D3DWindowAssetHelpers::NormalizeAssetPath(skinnedMeshComponent->GetSkinnedMeshAssetPath());
	if (normalizedMeshAssetPath.empty())
		return false;

	SceneEntityBase* runtimeOwnerEntity =
		ResolveSkinningRuntimeOwnerEntity(mLastExternalECS, renderItem.SourceEntity);
	if (runtimeOwnerEntity == nullptr)
		runtimeOwnerEntity = renderItem.SourceEntity;

	SkinnedDeformCacheEntry& cacheEntry = mSkinnedDeformCache[runtimeOwnerEntity];
	if (cacheEntry.SkinnedMeshAssetPath != normalizedMeshAssetPath ||
		cacheEntry.RuntimeOwnerEntity != runtimeOwnerEntity)
	{
		cacheEntry = {};
		cacheEntry.RenderItemName = renderItemName;
		cacheEntry.SkinnedMeshAssetPath = normalizedMeshAssetPath;
		cacheEntry.SourceEntity = renderItem.SourceEntity;
		cacheEntry.RuntimeOwnerEntity = runtimeOwnerEntity;
	}
	else if (cacheEntry.RenderItemName.empty())
	{
		cacheEntry.RenderItemName = renderItemName;
	}

	cacheEntry.SourceEntity = renderItem.SourceEntity;
	if (renderItem.SkinningCBIndex != UINT(-1))
		cacheEntry.SkinningCBIndex = renderItem.SkinningCBIndex;

	if (!cacheEntry.AssetLoaded)
	{
		EngineHelpers::AddLog(
			L"[SkinningCompute] 旧 .wskin 读取链已移除，暂不创建 compute skinning cache：renderItem=%s asset=%s",
			renderItemName.c_str(),
			normalizedMeshAssetPath.c_str());
		return false;
	}

	if (cacheEntry.AssetLoaded &&
		cacheEntry.SourceVertexBuffer == nullptr &&
		cacheEntry.VertexCount > 0)
	{
		if (!CreateUploadStructuredBuffer(
			d3dDevice.Get(),
			cacheEntry.SourceMeshData.Vertices.data(),
			static_cast<UINT64>(sizeof(Witchcraft::Animation::SkinnedVertex)) * cacheEntry.VertexCount,
			cacheEntry.SourceVertexBuffer.GetAddressOf()))
		{
			EngineHelpers::AddLog(
				L"[SkinningCompute] 创建 source vertex buffer 失败：renderItem=%s vertexCount=%u",
				renderItemName.c_str(),
				cacheEntry.VertexCount);
			return false;
		}

		cacheEntry.SourceVertexBuffer->SetName((L"SkinningSourceVB_" + renderItemName).c_str());
	}

	if (cacheEntry.AssetLoaded &&
		cacheEntry.SourceVertexBuffer != nullptr &&
		cacheEntry.VertexBufferByteSize > 0)
	{
		for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
		{
			if (cacheEntry.DeformedVertexBuffers[frameIndex] != nullptr)
				continue;

			if (!CreateUnorderedAccessVertexBuffer(
				d3dDevice.Get(),
				cacheEntry.VertexBufferByteSize,
				cacheEntry.DeformedVertexBuffers[frameIndex].GetAddressOf()))
			{
				EngineHelpers::AddLog(
					L"[SkinningCompute] 创建 deformed vertex buffer 失败：renderItem=%s frame=%u bytes=%u",
					renderItemName.c_str(),
					frameIndex,
					cacheEntry.VertexBufferByteSize);
				return false;
			}

			cacheEntry.DeformedVertexBuffers[frameIndex]->SetName(
				(L"SkinningDeformedVB_" + renderItemName + L"_" + std::to_wstring(frameIndex)).c_str());
			cacheEntry.DeformedVertexBufferViews[frameIndex].BufferLocation =
				cacheEntry.DeformedVertexBuffers[frameIndex]->GetGPUVirtualAddress();
			cacheEntry.DeformedVertexBufferViews[frameIndex].StrideInBytes = sizeof(Vertex);
			cacheEntry.DeformedVertexBufferViews[frameIndex].SizeInBytes = cacheEntry.VertexBufferByteSize;
			cacheEntry.DeformedVertexBufferStates[frameIndex] = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
	}

	SkinningRuntimeComponent* runtimeComponent =
		mLastExternalECS->GetComponent<SkinningRuntimeComponent>(runtimeOwnerEntity);

	if (runtimeComponent != nullptr)
	{
		const std::uint64_t paletteRevision = runtimeComponent->GetPalette().Revision;
		if (cacheEntry.LastPaletteRevision != paletteRevision)
		{
			cacheEntry.LastPaletteRevision = paletteRevision;
			cacheEntry.GpuResourcesReady = false;
		}
	}

	return cacheEntry.AssetLoaded &&
		cacheEntry.SourceVertexBuffer != nullptr &&
		cacheEntry.VertexCount > 0 &&
		CurrBackBufferIndex < cacheEntry.DeformedVertexBuffers.size() &&
		cacheEntry.DeformedVertexBuffers[CurrBackBufferIndex] != nullptr;
}

void D3DWindow::UpdateSkinnedDeformationCaches()
{
	if (!kEnableComputeSkinning)
	{
		mSkinnedDeformCache.clear();
		return;
	}

	if (mLastExternalECS == nullptr)
	{
		mSkinnedDeformCache.clear();
		return;
	}

	std::unordered_set<std::wstring> activeRenderItems;
	std::unordered_set<SceneEntityBase*> activeEntities;
	activeEntities.reserve(AllRitems.size());

	for (auto& renderItemPair : AllRitems)
	{
		const std::wstring& renderItemName = renderItemPair.first;
		RenderItem& renderItem = renderItemPair.second;
		if (!renderItem.IsSkinned)
			continue;

		SceneEntityBase* runtimeOwnerEntity =
			ResolveSkinningRuntimeOwnerEntity(mLastExternalECS, renderItem.SourceEntity);
		if (runtimeOwnerEntity != nullptr)
			activeEntities.insert(runtimeOwnerEntity);
		(void)EnsureSkinnedDeformCacheEntry(renderItem, renderItemName);
	}

	for (auto it = mSkinnedDeformCache.begin(); it != mSkinnedDeformCache.end();)
	{
		if (activeEntities.find(it->first) == activeEntities.end())
			it = mSkinnedDeformCache.erase(it);
		else
			++it;
	}
}

void D3DWindow::DispatchSkinnedDeformationPass(ID3D12GraphicsCommandList* cmdList)
{
	if (!kEnableComputeSkinning)
		return;

	const UINT skinningCBByteSize = CalculateConstantBufferByteSize(sizeof(SkinningConstants));
	bool hasAnyDispatch = false;

	for (auto& cacheEntryPair : mSkinnedDeformCache)
	{
		SkinnedDeformCacheEntry& cacheEntry = cacheEntryPair.second;
		if (!cacheEntry.AssetLoaded ||
			cacheEntry.RuntimeOwnerEntity == nullptr ||
			cacheEntry.SourceVertexBuffer == nullptr ||
			cacheEntry.VertexCount == 0 ||
			CurrBackBufferIndex >= cacheEntry.DeformedVertexBuffers.size() ||
			cacheEntry.DeformedVertexBuffers[CurrBackBufferIndex] == nullptr)
		{
			continue;
		}

		UINT skinningCBIndex = cacheEntry.SkinningCBIndex;
		if (skinningCBIndex == UINT(-1))
		{
			for (auto& renderItemPair : AllRitems)
			{
				RenderItem& candidate = renderItemPair.second;
				if (!candidate.IsSkinned || candidate.SkinningCBIndex == UINT(-1))
				{
					continue;
				}

				SceneEntityBase* candidateRuntimeOwner =
					ResolveSkinningRuntimeOwnerEntity(mLastExternalECS, candidate.SourceEntity);
				if (candidateRuntimeOwner != cacheEntry.RuntimeOwnerEntity)
					continue;

				skinningCBIndex = candidate.SkinningCBIndex;
				cacheEntry.SkinningCBIndex = skinningCBIndex;
				cacheEntry.RenderItemName = renderItemPair.first;
				break;
			}
		}

		if (skinningCBIndex == UINT(-1))
			continue;

		TransitionTrackedResourceState(
			cmdList,
			cacheEntry.DeformedVertexBuffers[CurrBackBufferIndex].Get(),
			cacheEntry.DeformedVertexBufferStates[CurrBackBufferIndex],
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		if (!hasAnyDispatch)
		{
			cmdList->SetComputeRootSignature(mSkinningComputePass.GetRootSignature());
			cmdList->SetPipelineState(mSkinningComputePass.GetPipelineState());
			hasAnyDispatch = true;
		}

		const D3D12_GPU_VIRTUAL_ADDRESS sourceVertexBufferAddress =
			cacheEntry.SourceVertexBuffer->GetGPUVirtualAddress();
		const D3D12_GPU_VIRTUAL_ADDRESS deformedVertexBufferAddress =
			cacheEntry.DeformedVertexBuffers[CurrBackBufferIndex]->GetGPUVirtualAddress();
		const D3D12_GPU_VIRTUAL_ADDRESS skinningCBAddress =
			CurrFrameResource->SkinningCB->Resource()->GetGPUVirtualAddress() +
			static_cast<UINT64>(skinningCBIndex) * skinningCBByteSize;
		const UINT dispatchConstants[4] = { cacheEntry.VertexCount, 0u, 0u, 0u };
		const UINT threadGroupCount = (cacheEntry.VertexCount + 63u) / 64u;

		cmdList->SetComputeRootShaderResourceView(0, sourceVertexBufferAddress);
		cmdList->SetComputeRootUnorderedAccessView(1, deformedVertexBufferAddress);
		cmdList->SetComputeRootConstantBufferView(2, skinningCBAddress);
		cmdList->SetComputeRoot32BitConstants(3, 4, dispatchConstants, 0);
		cmdList->Dispatch(threadGroupCount, 1, 1);

		const D3D12_RESOURCE_BARRIER uavBarrier =
			CD3DX12_RESOURCE_BARRIER::UAV(cacheEntry.DeformedVertexBuffers[CurrBackBufferIndex].Get());
		cmdList->ResourceBarrier(1, &uavBarrier);

		TransitionTrackedResourceState(
			cmdList,
			cacheEntry.DeformedVertexBuffers[CurrBackBufferIndex].Get(),
			cacheEntry.DeformedVertexBufferStates[CurrBackBufferIndex],
			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

		cacheEntry.GpuResourcesReady = true;
	}
}

void D3DWindow::UpdateMaterialCBs()
{
	auto currMaterialCB = CurrFrameResource->MaterialCB.get();

	if (FreshenAllMaterial)
	{
		for (auto& materialPair : Materials)
		{
			materialPair.second.NumFramesDirty = SwapChainBufferCount;
		}
	}

	for (auto& M : Materials)
	{
		Material* Mat = &M.second;
		if (Mat->NumFramesDirty > 0 && Mat->MatCBIndex!=-1)
		{
			currMaterialCB->CopyData(Mat->MatCBIndex, Mat->Properties);

			Mat->NumFramesDirty--;
		}
	}
	FreshenAllMaterial = false;
}

void D3DWindow::FreshenMaterialCBs()
{
	FreshenAllMaterial = true;
}

void D3DWindow::UpdateLightCBs()
{
	// 汇总编辑器/场景中的灯光对象，生成一份按 shader 布局排列的 LightConstants。
	// 这里同时建立 LightsCache，供后续阴影矩阵计算复用，避免再遍历一次 Lights。
	LightConstants lightConstants = {};
	lightConstants.AmbientColor = AmbientColor;

	const UINT maxLightCount = _countof(lightConstants.Lights);

	LightsCache.clear();
	LightsCache.reserve((std::min)(static_cast<UINT>(Lights.size()), maxLightCount));
	LightsCacheNames.clear();
	LightsCacheNames.reserve((std::min)(static_cast<UINT>(Lights.size()), maxLightCount));
	LightShadowMapIndices.clear();
	LightShadowMapIndices.reserve((std::min)(static_cast<UINT>(Lights.size()), maxLightCount));
	RotatedLightDirections.clear();
	RotatedLightDirections.reserve((std::min)(static_cast<UINT>(Lights.size()), maxLightCount));

	UINT lightCount = 0;
	for (auto& lightPair : Lights)
	{
		if (lightCount >= maxLightCount)
			break;

		Light& light = lightPair.second;
		if (FreshenAllLight)
			light.NumFramesDirty = SwapChainBufferCount;
		else if (light.NumFramesDirty > 0)
			light.NumFramesDirty--;

		LightsCache.push_back(light);
		LightsCacheNames.push_back(lightPair.first);
		RotatedLightDirections.push_back(light.Direction);

		++lightCount;
	}

	CurrentShadowPoolPlan = ShadowPoolPlanner::BuildShadowPoolPlan(
		ShadowConfig,
		LightsCache,
		ShadowConfig.MaxShadowMapCount);
	LightShadowMapIndices.assign(LightsCache.size(), -1);
	CurrentShadowUpdateRequests.assign(LightsCache.size(), {});

	bool hasDynamicShadowCasters = false;
	for (const auto& renderItemPair : AllRitems)
	{
		const RenderItem& renderItem = renderItemPair.second;
		if (renderItem.Obj == nullptr || renderItem.Geo == nullptr)
			continue;
		if (ShadowRuntimeHelpers::IsDynamicShadowSceneType(renderItem.SceneType))
		{
			hasDynamicShadowCasters = true;
			break;
		}
	}

	std::unordered_set<std::wstring> activeShadowLightNames;
	activeShadowLightNames.reserve(LightsCacheNames.size());

	for (UINT cachedLightIndex = 0; cachedLightIndex < LightsCache.size(); ++cachedLightIndex)
	{
		const Light& light = LightsCache[cachedLightIndex];
		const std::wstring& lightName = LightsCacheNames[cachedLightIndex];
		const ShadowPoolPlanner::LightAssignment& assignment =
			CurrentShadowPoolPlan.LightAssignments[cachedLightIndex];
		ShadowUpdateRequest& updateRequest = CurrentShadowUpdateRequests[cachedLightIndex];
		LightData& lightData = lightConstants.Lights[cachedLightIndex];
		lightData.Type = light.Type;
		lightData.Color = light.Color;
		lightData.Direction = light.Direction;
		lightData.Position = light.Position;
		lightData.Up = light.Up;
		lightData.Power = light.Power;
		lightData.VolumetricEnable = light.EnableVolumetric ? 1.0f : 0.0f;
		lightData.VolumetricIntensity = light.EnableVolumetric
			? std::clamp(light.VolumetricIntensity, 0.0f, 8.0f)
			: 0.0f;
		lightData.VolumetricAttenuationDistance = light.EnableVolumetric
			? std::clamp(light.VolumetricAttenuationDistance, 0.1f, 500.0f)
			: 0.0f;
		lightData.SpotRange = std::clamp(light.SpotRange, 0.1f, 500.0f);
		lightData.PointRange = std::clamp(light.VolumetricAttenuationDistance, 0.1f, 500.0f);
		const float spotInnerAngleDegrees = std::clamp(light.SpotInnerAngleDegrees, 1.0f, 85.0f);
		const float spotOuterAngleDegrees = std::clamp(
			(std::max)(light.SpotOuterAngleDegrees, spotInnerAngleDegrees + 0.5f),
			1.0f,
			89.0f);
		lightData.SpotInnerCos = std::cos(DirectX::XMConvertToRadians(spotInnerAngleDegrees));
		lightData.SpotOuterCos = std::cos(DirectX::XMConvertToRadians(spotOuterAngleDegrees));
		lightData.ShadowTextureIndex = -1.0f;
		lightData.ShadowTransformIndex = -1.0f;
		lightData.ShadowSamplingMode = static_cast<float>(ShadowSamplingMode::None);
		lightData.ShadowNearPlane = 0.1f;
		lightData.ShadowFarPlane = 100.0f;
		lightData.ShadowSoftnessScale = 1.0f;
		lightData.ShadowBiasScale = 1.0f;

		if (assignment.BaseShadowMapIndex >= 0)
		{
			LightShadowMapIndices[cachedLightIndex] = assignment.BaseShadowMapIndex;

			if (assignment.Pool == ShadowPoolPlanner::PoolKind::Directional)
			{
				lightData.ShadowTextureIndex = static_cast<float>(
					assignment.BaseShadowMapIndex - static_cast<int>(CurrentShadowPoolPlan.DirectionalPool.StartSlot));
				lightData.ShadowTransformIndex = static_cast<float>(assignment.BaseShadowMapIndex);
				lightData.ShadowSamplingMode = static_cast<float>(ShadowSamplingMode::DirectionalCascade);
			}
			else if (assignment.Pool == ShadowPoolPlanner::PoolKind::Spot)
			{
				lightData.ShadowTextureIndex = static_cast<float>(
					assignment.BaseShadowMapIndex - static_cast<int>(CurrentShadowPoolPlan.SpotPool.StartSlot));
				lightData.ShadowTransformIndex = static_cast<float>(assignment.BaseShadowMapIndex);
				lightData.ShadowSamplingMode = static_cast<float>(ShadowSamplingMode::SpotMap);
				lightData.ShadowNearPlane = 0.1f;
				lightData.ShadowFarPlane = (std::max)(lightData.SpotRange, lightData.ShadowNearPlane + 1.0f);
			}
			else if (assignment.Pool == ShadowPoolPlanner::PoolKind::Point)
			{
				if (assignment.BaseShadowMapIndex >= static_cast<int>(CurrentShadowPoolPlan.PointPool.StartSlot))
				{
					lightData.ShadowTextureIndex = static_cast<float>(
						assignment.BaseShadowMapIndex - static_cast<int>(CurrentShadowPoolPlan.PointPool.StartSlot));
				}
				else
				{
					lightData.ShadowTextureIndex = -1.0f;
				}
				lightData.ShadowSamplingMode = static_cast<float>(ShadowSamplingMode::PointCube);
			}
		}

		updateRequest.Pool = assignment.Pool;
		updateRequest.BaseShadowMapIndex = assignment.BaseShadowMapIndex;
		updateRequest.SlotCount = assignment.SlotCount;
		updateRequest.Type = ShadowUpdateRequestType::None;

		if (assignment.BaseShadowMapIndex >= 0 && ShadowRuntimeHelpers::SupportsShadowDirtyScheduling(assignment.Pool))
		{
			activeShadowLightNames.insert(lightName);
			const std::uint64_t lightHash = ShadowHashUtils::BuildShadowRelevantLightHash(light);
			const std::uint64_t staticCasterHash = BuildStaticShadowCasterHashForLight(light);
			ShadowLightRuntimeState& runtimeState = ShadowLightRuntimeStates[lightName];
			const bool shadowAssignmentChanged =
				!runtimeState.HasShadowParameterHash ||
				runtimeState.BaseShadowMapIndex != assignment.BaseShadowMapIndex ||
				runtimeState.SlotCount != assignment.SlotCount ||
				runtimeState.Pool != assignment.Pool;
			const bool shadowLightChanged = runtimeState.ShadowParameterHash != lightHash;
			const bool staticShadowCastersChanged =
				!runtimeState.HasStaticCasterHash ||
				runtimeState.StaticCasterHash != staticCasterHash;

			if (shadowAssignmentChanged || shadowLightChanged || staticShadowCastersChanged)
			{
				updateRequest.Type = ShadowUpdateRequestType::FullUpdate;
			}
			else if (hasDynamicShadowCasters)
			{
				updateRequest.Type = ShadowUpdateRequestType::DynamicOnlyUpdate;
			}
			runtimeState.ShadowParameterHash = lightHash;
			runtimeState.StaticCasterHash = staticCasterHash;
			runtimeState.HasShadowParameterHash = true;
			runtimeState.HasStaticCasterHash = true;
			runtimeState.BaseShadowMapIndex = assignment.BaseShadowMapIndex;
			runtimeState.SlotCount = assignment.SlotCount;
			runtimeState.Pool = assignment.Pool;
		}
	}

	for (auto it = ShadowLightRuntimeStates.begin(); it != ShadowLightRuntimeStates.end();)
	{
		if (activeShadowLightNames.find(it->first) == activeShadowLightNames.end())
			it = ShadowLightRuntimeStates.erase(it);
		else
			++it;
	}

	MainPassCB.LightConst = lightCount;
	// 灯光数量变化后，阴影贴图数组大小也可能变化，这里统一确保资源数量匹配。
	EnsureShadowMapResources(CurrentShadowPoolPlan.TotalAssignedShadowSlotCount);
	for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
	{
		auto lightCB = mFrameResources[frameIndex].LightCB.get();
		if (lightCB != nullptr)
			lightCB->CopyData(0, lightConstants);
	}
	FreshenAllLight = false;
}

void D3DWindow::FreshenLightCBs()
{
	FreshenAllLight = true;
}

std::uint64_t D3DWindow::BuildStaticShadowCasterHashForLight(const Light& light) const
{
	std::uint64_t hash = 0;
	std::vector<std::pair<std::wstring, const RenderItem*>> staticShadowCasters;
	staticShadowCasters.reserve(AllRitems.size());
	const int pointLightType = static_cast<int>(std::lround(ShadowConfig.PointLightType));
	const int spotLightType = static_cast<int>(std::lround(ShadowConfig.SpotLightType));
	const int resolvedLightType = static_cast<int>(std::lround(light.Type));

	for (const auto& renderItemPair : AllRitems)
	{
		const std::wstring& renderItemName = renderItemPair.first;
		const RenderItem& renderItem = renderItemPair.second;
		if (renderItem.Obj == nullptr || renderItem.Geo == nullptr)
			continue;
		if (ShadowRuntimeHelpers::IsDynamicShadowSceneType(renderItem.SceneType))
			continue;
		if (RitemLayer[天空渲染项目].find(renderItemName) != RitemLayer[天空渲染项目].end())
			continue;
		if (RitemLayer[debugrt].find(renderItemName) != RitemLayer[debugrt].end())
			continue;
		if (resolvedLightType == pointLightType)
		{
			if (!RenderInfluenceHelpers::IntersectsPointLightInfluence(light, renderItem))
				continue;
		}
		else if (resolvedLightType == spotLightType)
		{
			if (!RenderInfluenceHelpers::IntersectsSpotLightInfluence(light, renderItem))
				continue;
		}

		staticShadowCasters.emplace_back(renderItemName, &renderItem);
	}

	std::sort(
		staticShadowCasters.begin(),
		staticShadowCasters.end(),
		CompareShadowCasterEntryByName);

	for (const auto& casterEntry : staticShadowCasters)
	{
		const std::wstring& renderItemName = casterEntry.first;
		const RenderItem& renderItem = *casterEntry.second;

		ShadowHashUtils::HashCombine(hash, renderItemName);
		ShadowHashUtils::HashCombine(hash, static_cast<UINT>(renderItem.SceneType));
		ShadowHashUtils::HashCombine(hash, static_cast<UINT>(renderItem.PrimitiveType));
		ShadowHashUtils::HashCombine(hash, static_cast<UINT64>(renderItem.ObjCBIndex));
		ShadowHashUtils::HashCombine(hash, static_cast<UINT64>(renderItem.Geo->vertexBufferView.BufferLocation));
		ShadowHashUtils::HashCombine(hash, static_cast<UINT64>(renderItem.Geo->indexBufferView.BufferLocation));
		ShadowHashUtils::HashCombine(hash, static_cast<UINT64>(renderItem.Obj->AggrObject != nullptr ? renderItem.Obj->AggrObject->IndexCount : 0));
		ShadowHashUtils::HashCombine(hash, static_cast<UINT64>(renderItem.Obj->AggrObject != nullptr ? renderItem.Obj->AggrObject->StartIndexLocation : 0));
		ShadowHashUtils::HashCombine(hash, static_cast<INT64>(renderItem.Obj->AggrObject != nullptr ? renderItem.Obj->AggrObject->BaseVertexLocation : 0));

		const float* worldElements = &renderItem.WorldTransform._11;
		for (int elementIndex = 0; elementIndex < 16; ++elementIndex)
			ShadowHashUtils::HashFloat(hash, worldElements[elementIndex]);
	}

	return hash;
}

void D3DWindow::UpdateMainPassCBs()
{
	// 主 pass 常量描述“当前相机看到的这一帧”：
	// 包括视图矩阵、投影矩阵、屏幕尺寸、阴影变换以及 AO 开关等全局参数。
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();
	{
		// 缓存一份“本帧实时剔除参考矩阵”，供多线程绘制阶段安全读取。
		std::lock_guard<std::mutex> lock(mFrustumCullingReferenceMutex);
		XMStoreFloat4x4(&mLiveFrustumCullingView, view);
		XMStoreFloat4x4(&mLiveFrustumCullingProj, proj);
		mHasLiveFrustumCullingReference = true;
	}

	XMVECTOR DeterminantView(XMMatrixDeterminant(view));
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&DeterminantView, view);
	XMVECTOR DeterminantProj(XMMatrixDeterminant(proj));
	XMMATRIX invProj = XMMatrixInverse(&DeterminantProj, proj);
	XMVECTOR DeterminantViewProj(XMMatrixDeterminant(viewProj));
	XMMATRIX invViewProj = XMMatrixInverse(&DeterminantViewProj, viewProj);

	// 将 NDC 空间 [-1,+1]^2 变换到纹理空间 [0,1]^2
	DirectX::XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	DirectX::XMMATRIX viewProjTex = DirectX::XMMatrixMultiply(viewProj, T);

	XMStoreFloat4x4(&MainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&MainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&MainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&MainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&MainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&MainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&MainPassCB.ViewProjTex, XMMatrixTranspose(viewProjTex));
	// ShadowTransform 现在按“阴影贴图槽位”存储，而不是按“灯数量”存储。
	// 因此这里要先把整块数组清成 Identity，再把本帧实际用到的 shadow transform 写进去。
	for (UINT i = 0; i < _countof(MainPassCB.ShadowTransform); ++i)
		MainPassCB.ShadowTransform[i] = MathHelps::Identity;
	for (UINT i = 0; i < ShadowTransform.size() && i < _countof(MainPassCB.ShadowTransform); ++i)
		XMStoreFloat4x4(&MainPassCB.ShadowTransform[i], XMMatrixTranspose(XMLoadFloat4x4(&ShadowTransform[i])));
	{
		const DirectX::SimpleMath::Vector3 cameraPosition = mCamera.GetCamPosition();
		MainPassCB.EyePosW = DirectX::XMFLOAT3(cameraPosition.x, cameraPosition.y, cameraPosition.z);
	}
	MainPassCB.RenderTargetSize = XMFLOAT2(
		(float)WinInfo.Width,
		(float)WinInfo.Height);
	MainPassCB.AOSettings = XMFLOAT2(
		AOConfig.Enabled ? 1.0f : 0.0f,
		std::clamp(AOConfig.Strength, 0.0f, 1.0f));
	MainPassCB.ShadowMaskSettings = XMFLOAT4(
		(ShadowMaskConfig.Enabled && ShadowMaskConfig.UseInMainPbr && AOConfig.Enabled) ? 1.0f : 0.0f,
		(std::max)(ShadowMaskConfig.Cascade0BlurRadius, 0.0f),
		(std::max)(ShadowMaskConfig.Cascade1BlurRadius, 0.0f),
		(std::max)(ShadowMaskConfig.Cascade2BlurRadius, 0.0f));
	// FXAA 目前主要由后处理常量使用，但屏幕尺寸信息与主 pass 同步更新最稳妥。
	MainPostProcessCB.RenderTargetSize = MainPassCB.RenderTargetSize;

	for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex].PassCB.get();
		if (passCB != nullptr)
			passCB->CopyData(0, MainPassCB);
	}
}

void D3DWindow::UpdateShadowTransform()
{
	const ShadowMapPass::ShadowMapLayout shadowLayout = shadowMapPass.GetLayout();
	const UINT totalShadowSlotCount = shadowLayout.Texture2DCount + pointLightShadowCubePool.GetCubeCount();
	const ShadowFrameBuildResult shadowFrame = ShadowFrameBuilder::BuildShadowFrame(
		ShadowConfig,
		mCamera,
		LightsCache,
		LightShadowMapIndices,
		totalShadowSlotCount,
		&mStableDirectionalCascadeStates);

	MainPassCB.DirectionalShadowCascadeSplits = shadowFrame.DirectionalShadowCascadeSplits;
	MainPassCB.DirectionalShadowCascadeSettings = shadowFrame.DirectionalShadowCascadeSettings;
	MainPassCB.DirectionalShadowCascadeWorldTexelSize = shadowFrame.DirectionalShadowCascadeWorldTexelSize;
	MainPassCB.DirectionalShadowCascadeDepthScale = shadowFrame.DirectionalShadowCascadeDepthScale;
	ShadowTransform = shadowFrame.ShadowTransforms;
	ShadowRenderEntries = shadowFrame.ShadowRenderEntries;
	CurrentPointLightCubeParams = shadowFrame.PointLightCubes;
}

void D3DWindow::UpdateShadowPassCBs()
{
	// 每张 shadow map 都有自己独立的 pass 常量。
	// 主 pass 占用 PassCB[0]，所以阴影 pass 从 1 开始顺延写入。
	for (UINT shadowIndex = 0; shadowIndex < ShadowRenderEntries.size(); ++shadowIndex)
	{
		const ShadowRenderEntry& entry = ShadowRenderEntries[shadowIndex];
		const UINT shadowSlotIndex = entry.ShadowSlotIndex;

		ComPtr<ID3D12Resource> shadowResource = nullptr;
		if (entry.ViewKind == ShadowViewKind::PointFace)
		{
			UINT cubeIndex = 0;
			if (ResolvePointLightCubeIndex(
				CurrentShadowPoolPlan.PointPool.StartSlot,
				shadowSlotIndex,
				pointLightShadowCubePool.GetCubeCount(),
				&cubeIndex))
			{
				shadowResource = pointLightShadowCubePool.GetCubeResource(cubeIndex);
			}
		}
		else
		{
			shadowResource = shadowMapPass.GetResource(shadowSlotIndex);
		}

		const PassConstants shadowPassCB = ShadowFrameBuilder::BuildShadowPassConstants(
			entry,
			shadowResource.Get(),
			ShadowConfig.ShadowMapSize,
			MainPassCB);

		for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
		{
			auto passCB = mFrameResources[frameIndex].PassCB.get();
			if (passCB != nullptr)
				passCB->CopyData(entry.PassCBIndex, shadowPassCB);
		}
	}
}

void D3DWindow::UpdateAOCB()
{
	const AOConstants AOCB = ambientOcclusion.BuildConstants(
		mCamera.GetProj(),
		MainPassCB.Proj,
		MainPassCB.InvProj,
		MainPassCB.InvView,
		static_cast<float>(ambientOcclusion.GetRenderWidth()),
		static_cast<float>(ambientOcclusion.GetRenderHeight()),
		AOConfig.BlurSigma,
		AOConfig.Radius,
		AOConfig.FadeStart,
		AOConfig.FadeEnd,
		AOConfig.SurfaceEpsilon);

	for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
	{
		auto ssaoCB = mFrameResources[frameIndex].AOCB.get();
		if (ssaoCB != nullptr)
		{
			for (int mirrorIndex = 0; mirrorIndex < 4; ++mirrorIndex)
				ssaoCB->CopyData(mirrorIndex, AOCB);
		}
	}
}

void D3DWindow::UpdatePostProcessCBs()
{
	for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
	{
		auto postProcessCB = mFrameResources[frameIndex].PostProcessCB.get();
		if (postProcessCB != nullptr)
			postProcessCB->CopyData(0, MainPostProcessCB);
	}
}

void D3DWindow::UpdateFrameDescriptors()
{
	UINT resolvedSkyTexHeapIndex = SkyTexHeapIndex;
	const auto defaultSkyGroupIt = TextureGroups.find(L"skyMap");
	const auto defaultDiffuseGroupIt = TextureGroups.find(L"Diffuse");
	const bool hasSkyRenderItems = !RitemLayer[天空渲染项目].empty();
	const bool hasDefaultSkyTexture =
		defaultSkyGroupIt != TextureGroups.end() &&
		!defaultSkyGroupIt->second.empty() &&
		defaultSkyGroupIt->second[0].GetResource() != nullptr;
	const bool hasDefaultDiffuseTexture =
		defaultDiffuseGroupIt != TextureGroups.end() &&
		!defaultDiffuseGroupIt->second.empty() &&
		defaultDiffuseGroupIt->second[0].GetResource() != nullptr;

	UINT defaultSkyTexHeapIndex = NullTextureHeapIndex;
	if (hasDefaultSkyTexture)
	{
		defaultSkyTexHeapIndex = defaultSkyGroupIt->second[0].GetIndex();
	}
	else if (hasDefaultDiffuseTexture)
	{
		defaultSkyTexHeapIndex = defaultDiffuseGroupIt->second[0].GetIndex();
	}

	if (defaultSkyTexHeapIndex >= SrvDescriptorHeapCapacity)
	{
		defaultSkyTexHeapIndex = (NullTextureHeapIndex < SrvDescriptorHeapCapacity) ? NullTextureHeapIndex : 0u;
	}

	// 无天空实体时优先绑定“真实可采样贴图”（默认天空，其次默认漫反射），
	// 避免部分驱动在长期采样 null SRV 时出现资源访问异常。
	const UINT resolvedFallbackSkyTexHeapIndex = defaultSkyTexHeapIndex;
	if (!hasSkyRenderItems)
		resolvedSkyTexHeapIndex = resolvedFallbackSkyTexHeapIndex;

	if (resolvedSkyTexHeapIndex >= SrvDescriptorHeapCapacity)
	{
		resolvedSkyTexHeapIndex = resolvedFallbackSkyTexHeapIndex;
		if (resolvedSkyTexHeapIndex >= SrvDescriptorHeapCapacity)
			resolvedSkyTexHeapIndex = (NullTextureHeapIndex < SrvDescriptorHeapCapacity) ? NullTextureHeapIndex : 0u;
	}

	SkyTexHeapIndex = resolvedSkyTexHeapIndex;
	SkyMapIndex = resolvedSkyTexHeapIndex;

	// 统一缓存本帧会用到的 SRV 描述符表起点，Render 阶段直接复用。
	skyTexDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	skyTexDescriptor.Offset(resolvedSkyTexHeapIndex, CbvSrvUavDescriptorSize);

	otherTexDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	shadow2DDescriptorTable = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	shadow2DDescriptorTable.Offset(ShadowMapHeapStartIndex, CbvSrvUavDescriptorSize);
	pointLightShadowCubeDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	pointLightShadowCubeDescriptor.Offset(ShadowMapHeapStartIndex + ShadowPoolLimits::Combined2DTextureCount, CbvSrvUavDescriptorSize);
	ambientOcclusionDescriptor = ambientOcclusion.mhAmbientMap0GpuSrv;
	directionalShadowMaskDescriptor = mDirectionalShadowMaskPass.GetMaskSrv();

	postProcessSceneColorDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	postProcessSceneColorDescriptor.Offset(PostProcessSceneColorHeapStartIndex + CurrBackBufferIndex, CbvSrvUavDescriptorSize);

	interactionOutlineMaskDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	interactionOutlineMaskDescriptor.Offset(InteractionOutlineMaskHeapStartIndex + CurrBackBufferIndex, CbvSrvUavDescriptorSize);

	transparentOitAccumDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	transparentOitAccumDescriptor.Offset(TransparentOitHeapStartIndex + CurrBackBufferIndex * 2, CbvSrvUavDescriptorSize);

	transparentOitRevealDescriptor = transparentOitAccumDescriptor;
	transparentOitRevealDescriptor.Offset(1, CbvSrvUavDescriptorSize);
}

void D3DWindow::UpdateDebugText()
{
	if (!renderFPS)
		return;

	static int frameCnt = 0;
	static float timeElapsed = 0.0f;
	frameCnt++;

	// 调试文字继续按 1 秒一次刷新，避免每帧都重建 FPS 字符串。
	if ((mTimer->TotalTime() - timeElapsed) >= 1.0f)
	{
		float fps = static_cast<float>(frameCnt);
		float mspf = 1000.0f / fps;

		std::wstring fpsStr = std::to_wstring(fps);
		std::wstring mspfStr = std::to_wstring(mspf);

		Text =
			L"帧率：" + fpsStr +
			L"   MSPF: " + mspfStr + L'\n';

		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}

void D3DWindow::BuildRenderFramePlan()
{
	// 在正式录制本帧命令前，先生成一份“Render 侧快照”：
	// 1) 统一判定本帧各个 pass 是否应该执行；
	// 2) 统一收集会被多个渲染阶段重复使用的 RenderItem 列表；
	// 3) 让 RenderB / RenderE 消费同一份结果，避免同帧不同阶段各自临时收集，
	//    导致 AO、描边、debug、天空等通道看到的场景快照不一致。
	//
	// 这一步故意放在 RenderB 开头，而不是 Update()：
	// - 放进 Update() 虽然看起来更“整洁”，但会把“场景快照采样时刻”和“真正提交 draw 的时刻”拉开；
	// - 我们前面的排查已经证明，这类 draw-list / 选择集 / pass 输入快照一旦过早缓存，
	//   就更容易重新引入闪烁或时序错位；
	// - 因此这里只在 Render 阶段内做一次集中组织，不跨越 Update/Render 边界。
	//
	// 边界也要保持克制：
	// - 这里只做 CPU 侧判断与列表收集；
	// - 不做资源状态切换、不写描述符、不上传常量、不录制命令。
	FrameSkyRenderItems = CollectRenderItems(RitemLayer[天空渲染项目]);
	FrameDebugRenderItems = CollectRenderItems(RitemLayer[debugrt]);
	FrameSelectedOutlineRenderItems = CollectSelectedRenderItems();
	FrameStaticShadowCasterRenderItems.clear();
	FrameDynamicShadowCasterRenderItems.clear();

	for (auto& renderItemPair : AllRitems)
	{
		const std::wstring& renderItemName = renderItemPair.first;
		RenderItem& renderItem = renderItemPair.second;
		if (renderItem.Obj == nullptr || renderItem.Geo == nullptr)
			continue;

		bool isSkyRenderItem = RitemLayer[天空渲染项目].find(renderItemName) != RitemLayer[天空渲染项目].end();
		bool isDebugRenderItem = RitemLayer[debugrt].find(renderItemName) != RitemLayer[debugrt].end();
		if (isSkyRenderItem || isDebugRenderItem)
			continue;

		if (ShadowRuntimeHelpers::IsDynamicShadowSceneType(renderItem.SceneType))
			FrameDynamicShadowCasterRenderItems.push_back(&renderItem);
		else
			FrameStaticShadowCasterRenderItems.push_back(&renderItem);
	}

	const bool hasSkyRenderItems = !FrameSkyRenderItems.empty();
	const bool hasOpaqueRenderItems = !RitemLayer[不透明物体渲染项目].empty();
	const bool hasTransparentRenderItems = !RitemLayer[透明物体渲染项目].empty();
	const bool allowAoInCurrentScene =
		AOConfig.Enabled &&
		DepthStencilBuffer != nullptr &&
		CurrFrameResource != nullptr &&
		sharedNormalPrepass.GetNormalMapResource() != nullptr &&
		sharedNormalPrepass.GetDepthMapResource() != nullptr &&
		ambientOcclusion.AmbientMap().Get() != nullptr;
	const bool allowOitInCurrentScene = true;
	const bool allowShadowInCurrentScene = true;
	const bool hasAoRenderItems = allowAoInCurrentScene &&
		(hasOpaqueRenderItems || hasTransparentRenderItems);
	const bool hasStaticShadowCasterRenderItems = !FrameStaticShadowCasterRenderItems.empty();
	const bool hasDynamicShadowCasterRenderItems = !FrameDynamicShadowCasterRenderItems.empty();
	const bool hasShadowCasterRenderItems = allowShadowInCurrentScene &&
		(hasStaticShadowCasterRenderItems || hasDynamicShadowCasterRenderItems);
	auto transparentOitAccumResource = CurrFrameResource != nullptr ? CurrFrameResource->mTransparentOitAccum.Get() : nullptr;
	auto transparentOitRevealResource = CurrFrameResource != nullptr ? CurrFrameResource->mTransparentOitReveal.Get() : nullptr;
	const bool hasOitResources = allowOitInCurrentScene &&
		hasTransparentRenderItems &&
		transparentOitAccumResource != nullptr &&
		transparentOitRevealResource != nullptr;

	CurrentRenderFramePlan.Valid = true;
	CurrentRenderFramePlan.BackBufferIndex = CurrBackBufferIndex;
	CurrentRenderFramePlan.HasSkyRenderItems = hasSkyRenderItems;
	CurrentRenderFramePlan.HasOpaqueRenderItems = hasOpaqueRenderItems;
	CurrentRenderFramePlan.HasTransparentRenderItems = hasTransparentRenderItems;
	CurrentRenderFramePlan.HasAoRenderItems = hasAoRenderItems;
	CurrentRenderFramePlan.HasShadowCasterRenderItems = hasShadowCasterRenderItems;
	CurrentRenderFramePlan.HasStaticShadowCasterRenderItems = hasStaticShadowCasterRenderItems;
	CurrentRenderFramePlan.HasDynamicShadowCasterRenderItems = hasDynamicShadowCasterRenderItems;
	CurrentRenderFramePlan.HasOitResources = hasOitResources;
}

void D3DWindow::Update()
{
	//BOOL isFull = false;
	//ThrowIfFailed(SwapChain->GetFullscreenState(&isFull, nullptr));
	//WinInfo.fullscreenState = isFull;

	UpdateCamera();

	// 以交换链的真实 current back buffer 为准，避免手动递增与实际呈现顺序失同步。
	CurrBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();

	// 循环遍历图形框架资源数组。
	CurrFrameResource = &mFrameResources[CurrBackBufferIndex];

	// GPU是否已完成对当前帧资源的命令的处理？
	// 如果没有，请等到GPU完成命令直到该防护点为止。
	if (CurrFrameResource->Fence != 0 && fence->GetCompletedValue() < CurrFrameResource->Fence)
	{
		ThrowIfFailed(fence->SetEventOnCompletion(CurrFrameResource->Fence, fenceEvent));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	UpdateObjectCBs();
	UpdateSkinningCBs();
	UpdateSkinnedDeformationCaches();
	UpdateMaterialCBs();
	UpdateLightCBs();
	UpdateShadowTransform();
	UpdateMainPassCBs();
	UpdateShadowPassCBs();
	UpdateAOCB();
	UpdatePostProcessCBs();
	UpdateFrameDescriptors();
	UpdateDebugText();
}

void D3DWindow::RenderB()
{
	BuildRenderFramePlan();

	// 为 SkinWeightVizPass 注入本帧依赖
	if (CurrFrameResource != nullptr && CurrFrameResource->PassCB != nullptr)
		mSkinWeightVizPass.SetFrameResourcePassCB(CurrFrameResource->PassCB.get());

	const bool hasSkyRenderItems = CurrentRenderFramePlan.HasSkyRenderItems;
	const bool hasAoRenderItems = CurrentRenderFramePlan.HasAoRenderItems;
	const bool hasShadowCasterRenderItems = CurrentRenderFramePlan.HasShadowCasterRenderItems;
	const bool hasOitResources = CurrentRenderFramePlan.HasOitResources;
	auto transparentOitAccumResource = CurrFrameResource != nullptr ? CurrFrameResource->mTransparentOitAccum.Get() : nullptr;
	auto transparentOitRevealResource = CurrFrameResource != nullptr ? CurrFrameResource->mTransparentOitReveal.Get() : nullptr;
	auto beginCommandList = CurrFrameResource->BeginCommandList.Get();
	auto midCommandList = CurrFrameResource->MidCommandList.Get();

	// RenderB 负责“录制准备阶段”：
	// 1) 重置并开启 Begin/Mid/End 命令列表；
	// 2) 处理本帧前置资源状态切换与清屏；
	// 3) 录制天空通道（若存在）；
	// 4) 关闭 Begin/Mid，等待 RenderE 统一提交。

	// 重用与命令记录相关的内存。
	// 只有当关联的命令列表在 GPU 上执行完毕后，
	// 我们才能重置，不进行重置则会导致内存溢出。
	ThrowIfFailed(CurrFrameResource->BeginCommandAllocator->Reset());
	ThrowIfFailed(CurrFrameResource->MidCommandAllocator->Reset());
	ThrowIfFailed(CurrFrameResource->EndCommandAllocator->Reset());

	// 命令列表可以在通过ExecuteCommandList添加到命令队列后重置。
	// 重复使用命令列表会重复使用内存。
	ThrowIfFailed(beginCommandList->Reset(CurrFrameResource->BeginCommandAllocator.Get(), nullptr));
	ThrowIfFailed(midCommandList->Reset(CurrFrameResource->MidCommandAllocator.Get(), nullptr));

	// 刷权重可视化几何体初始构建（首次加载时）
	mSkinWeightVizPass.TryRebuildFromEditor();

	// 重置线程工作命令分配器和列表。
	ThrowIfFailed(CurrFrameResource->EndCommandList->Reset(CurrFrameResource->EndCommandAllocator.Get(), nullptr));

	// 阴影 working/cache 资源的状态切换现在跟随具体 shadow entry 在工作线程里完成：
	// - FullUpdate: clear working -> draw static -> copy to static cache -> draw dynamic
	// - DynamicOnlyUpdate: copy static cache -> draw dynamic
	// 这里不再在 RenderB 统一预切 shadow 资源状态，避免和 copy/draw 交织时失配。
	D3D12_RESOURCE_BARRIER Barriers;

	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	beginCommandList->ResourceBarrier(1, &Barriers);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());
	const float clearColor[] = { 0.120f, 0.345f, 0.935f, 1.00f };
	beginCommandList->ClearRenderTargetView(rtvHandle, (float*)&clearColor, 0, nullptr);
	beginCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	if (!hasAoRenderItems)
	{
		ambientOcclusion.ClearAmbientMapsToNeutral(midCommandList);
		mDirectionalShadowMaskPass.ClearToNeutral(midCommandList);
	}

	DispatchSkinnedDeformationPass(midCommandList);

	if (hasSkyRenderItems)
	{
		ID3D12DescriptorHeap* srvDescriptorHeaps[] = { SrvDescriptorHeap.Get() };

		// 中段命令列表负责主线程固定通道：天空、共享根参数等。
		midCommandList->SetGraphicsRootSignature(RootSignature.Get());
		midCommandList->SetDescriptorHeaps(_countof(srvDescriptorHeaps), srvDescriptorHeaps);
		midCommandList->SetGraphicsRootConstantBufferView(1, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
		midCommandList->SetGraphicsRootConstantBufferView(2, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
		midCommandList->SetGraphicsRootDescriptorTable(5, skyTexDescriptor);
		midCommandList->SetGraphicsRootDescriptorTable(6, otherTexDescriptor);
		midCommandList->SetGraphicsRootDescriptorTable(7, shadow2DDescriptorTable);
		midCommandList->SetGraphicsRootDescriptorTable(8, pointLightShadowCubeDescriptor);
		midCommandList->SetGraphicsRootDescriptorTable(9, ambientOcclusionDescriptor);
		midCommandList->SetGraphicsRootDescriptorTable(10, directionalShadowMaskDescriptor);
		midCommandList->RSSetViewports(1, &m_viewport);
		midCommandList->RSSetScissorRects(1, &m_scissorRect);
		midCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
		DrawRenderItems(midCommandList, FrameSkyRenderItems, PipelineState[天空管道], 天空管道);
	}

	if (hasOitResources)
	{
		// 透明 OIT 目标在主线程预切到 RT 并清空，后续由工作线程并行录制 Draw。
		D3D12_RESOURCE_BARRIER oitToRenderTargetBarriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(transparentOitAccumResource,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
			CD3DX12_RESOURCE_BARRIER::Transition(transparentOitRevealResource,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		};
		midCommandList->ResourceBarrier(_countof(oitToRenderTargetBarriers), oitToRenderTargetBarriers);

		const UINT transparentOitRtvStartIndex = SwapChainBufferCount + 3 + CurrBackBufferIndex * 2;
		CD3DX12_CPU_DESCRIPTOR_HANDLE transparentOitAccumRtv(
			RtvHeap->GetCPUDescriptorHandleForHeapStart(),
			transparentOitRtvStartIndex,
			RtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE transparentOitRevealRtv = transparentOitAccumRtv;
		transparentOitRevealRtv.Offset(1, RtvDescriptorSize);
		const float transparentOitAccumClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
		const float transparentOitRevealClearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		midCommandList->ClearRenderTargetView(transparentOitAccumRtv, transparentOitAccumClearColor, 0, nullptr);
		midCommandList->ClearRenderTargetView(transparentOitRevealRtv, transparentOitRevealClearColor, 0, nullptr);
	}

	ThrowIfFailed(beginCommandList->Close());
	ThrowIfFailed(midCommandList->Close());
}

void D3DWindow::RenderE()
{
	const bool fallbackHasSkyRenderItems = !RitemLayer[天空渲染项目].empty();
	const bool fallbackHasOpaqueRenderItems = !RitemLayer[不透明物体渲染项目].empty();
	const bool fallbackHasTransparentRenderItems = !RitemLayer[透明物体渲染项目].empty();
	const bool fallbackAllowAoInCurrentScene =
		AOConfig.Enabled &&
		DepthStencilBuffer != nullptr &&
		CurrFrameResource != nullptr &&
		sharedNormalPrepass.GetNormalMapResource() != nullptr &&
		sharedNormalPrepass.GetDepthMapResource() != nullptr &&
		ambientOcclusion.AmbientMap().Get() != nullptr;
	const bool fallbackHasAoRenderItems = fallbackAllowAoInCurrentScene &&
		(fallbackHasOpaqueRenderItems || fallbackHasTransparentRenderItems);
	const bool fallbackHasShadowCasterRenderItems = fallbackHasOpaqueRenderItems || fallbackHasTransparentRenderItems;
	auto transparentOitAccumResource = CurrFrameResource != nullptr ? CurrFrameResource->mTransparentOitAccum.Get() : nullptr;
	auto transparentOitRevealResource = CurrFrameResource != nullptr ? CurrFrameResource->mTransparentOitReveal.Get() : nullptr;
	const bool fallbackHasOitResources = fallbackHasTransparentRenderItems &&
		transparentOitAccumResource != nullptr &&
		transparentOitRevealResource != nullptr;
	const bool useRecordedFramePlan =
		CurrentRenderFramePlan.Valid &&
		CurrentRenderFramePlan.BackBufferIndex == CurrBackBufferIndex;
	const bool hasSkyRenderItems = useRecordedFramePlan ?
		CurrentRenderFramePlan.HasSkyRenderItems :
		fallbackHasSkyRenderItems;
	const bool hasOpaqueRenderItems = useRecordedFramePlan ?
		CurrentRenderFramePlan.HasOpaqueRenderItems :
		fallbackHasOpaqueRenderItems;
	const bool hasTransparentRenderItems = useRecordedFramePlan ?
		CurrentRenderFramePlan.HasTransparentRenderItems :
		fallbackHasTransparentRenderItems;
	const bool hasAoRenderItems = useRecordedFramePlan ?
		CurrentRenderFramePlan.HasAoRenderItems :
		fallbackHasAoRenderItems;
	const bool hasShadowCasterRenderItems = useRecordedFramePlan ?
		CurrentRenderFramePlan.HasShadowCasterRenderItems :
		fallbackHasShadowCasterRenderItems;
	const bool hasOitResources = useRecordedFramePlan ?
		CurrentRenderFramePlan.HasOitResources :
		fallbackHasOitResources;
	const bool hasMainSceneGeometry = hasOpaqueRenderItems || hasTransparentRenderItems;
	const bool enableVolumetricLightPass =
		hasMainSceneGeometry &&
		CurrFrameResource != nullptr &&
		MainPassCB.LightConst > 0u &&
		DepthStencilBuffer != nullptr &&
		AOSceneInputDescriptorsInitialized;
	const bool allowNoSkyPostProcessTail = true;
	const bool allowNoSkyMainGeometrySubmission = true;
	const bool useNoSkyMinimalUiTail = false;

	// RenderE 负责“提交与收尾阶段”：
	// 1) 依次提交 Begin/Mid 命令列表；
	// 2) 驱动工作线程录制并提交 阴影/不透明/半透明/透明(OIT) 阶段；
	// 3) 录制 EndCommandList（OIT 合成、描边、体积光、FXAA、文字、UI）；
	// 4) 提交 EndCommandList；
	// 5) Present + Signal 围栏。

	// 阶段 1：提交 Begin 命令列表（状态切换、清屏等前置命令）。
	ID3D12CommandList* beginPhaseCommandLists[] =
	{
		CurrFrameResource->BeginCommandList.Get()
	};
	CommandQueue->ExecuteCommandLists(_countof(beginPhaseCommandLists), beginPhaseCommandLists);

	// 阶段 2：提交 Mid 命令列表（天空 + AO 预计算 + OIT 目标预清理）。
	ID3D12CommandList* midPhaseCommandLists[] =
	{
		CurrFrameResource->MidCommandList.Get()
	};
	CommandQueue->ExecuteCommandLists(_countof(midPhaseCommandLists), midPhaseCommandLists);

	// 阶段 3：驱动阴影阶段的多线程录制并提交。
	if (hasShadowCasterRenderItems)
	{
		ExecuteWorkerPassAndSubmit(阴影工作阶段);
	}

	// 阶段 4：共享法线前置通道先由 worker 线程录制，再执行 AO/Blur 全屏通道。
	if (hasAoRenderItems)
	{
		ExecuteWorkerPassAndSubmit(法线工作阶段);

		auto aoCommandList = CurrFrameResource->AoCommandList.Get();
		ThrowIfFailed(CurrFrameResource->AoCommandAllocator->Reset());
		ThrowIfFailed(aoCommandList->Reset(CurrFrameResource->AoCommandAllocator.Get(), nullptr));

		const D3D12_GPU_VIRTUAL_ADDRESS aoCBAddress = CurrFrameResource->AOCB->Resource()->GetGPUVirtualAddress();
		ambientOcclusion.RecordSsaoPasses(
			aoCommandList,
			SrvDescriptorHeap.Get(),
			PipelineState[环境遮蔽管道].Get(),
			PipelineState[遮蔽模糊管道].Get(),
			aoCBAddress,
			sharedNormalPrepass.GetNormalDepthSrv());
		if (ShadowMaskConfig.Enabled && ShadowMaskConfig.UseInMainPbr && hasShadowCasterRenderItems)
		{
			mDirectionalShadowMaskPass.RecordPasses(
				aoCommandList,
				SrvDescriptorHeap.Get(),
				mDirectionalShadowMaskPass.GetMaskPipelineState(),
				mDirectionalShadowMaskPass.GetBlurPipelineState(),
				CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
				CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress(),
				sharedNormalPrepass.GetNormalDepthSrv(),
				shadow2DDescriptorTable);
		}
		else
		{
			mDirectionalShadowMaskPass.ClearToNeutral(aoCommandList);
		}

		ThrowIfFailed(aoCommandList->Close());
		ID3D12CommandList* aoPhaseCommandLists[] = { aoCommandList };
		CommandQueue->ExecuteCommandLists(_countof(aoPhaseCommandLists), aoPhaseCommandLists);
	}

	// 阶段 5：不透明物体绘制改为工作线程录制。
	if (allowNoSkyMainGeometrySubmission && hasOpaqueRenderItems)
	{
		ExecuteWorkerPassAndSubmit(不透明工作阶段);
	}

	// 阶段 6：半透明（近不透明）绘制改为工作线程录制。
	if (allowNoSkyMainGeometrySubmission && hasTransparentRenderItems)
	{
		ExecuteWorkerPassAndSubmit(半透明工作阶段);
	}

	// 阶段 7：透明 OIT 累积绘制改为工作线程录制。
	if (allowNoSkyMainGeometrySubmission && hasOitResources)
	{
		ExecuteWorkerPassAndSubmit(透明工作阶段);
	}

	// 阶段 8：录制 EndCommandList，负责收尾与后处理。
	auto endCommandList = CurrFrameResource->EndCommandList.Get();
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());
	ID3D12DescriptorHeap* srvDescriptorHeaps[] = { SrvDescriptorHeap.Get() };

	if (useNoSkyMinimalUiTail)
	{
		endCommandList->RSSetViewports(1, &m_viewport);
		endCommandList->RSSetScissorRects(1, &m_scissorRect);
		endCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

		if (renderFPS)
		{
			endCommandList->SetPipelineState(PipelineState[文字管道].Get());
			textR->DXDrawText(endCommandList, Text, DirectX::XMFLOAT2(0.32f, 0.25f), DirectX::XMFLOAT4{ 1.0f,1.0f,1.0f,1.0f }, CurrBackBufferIndex);
		}

		if (mEditor && renderEditor)
			mEditor->Render();

		D3D12_RESOURCE_BARRIER renderTargetToPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
			SwapChainBuffer[CurrBackBufferIndex].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT);
		endCommandList->ResourceBarrier(1, &renderTargetToPresentBarrier);

		ThrowIfFailed(endCommandList->Close());

		ID3D12CommandList* postPhaseCommandLists[] = { endCommandList };
		CommandQueue->ExecuteCommandLists(_countof(postPhaseCommandLists), postPhaseCommandLists);

		ThrowIfFailed(SwapChain->Present(0, 0));
		CurrBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();
		CurrFrameResource->Fence = ++fenceValue;
		ThrowIfFailed(CommandQueue->Signal(fence.Get(), fenceValue));
		DrainDeferredReleasesByCompletedFence();
		CurrentRenderFramePlan.Valid = false;
		return;
	}

	endCommandList->SetGraphicsRootSignature(RootSignature.Get());
	endCommandList->SetDescriptorHeaps(_countof(srvDescriptorHeaps), srvDescriptorHeaps);
	endCommandList->SetGraphicsRootConstantBufferView(1, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
	endCommandList->SetGraphicsRootConstantBufferView(2, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
	endCommandList->SetGraphicsRootDescriptorTable(5, skyTexDescriptor);
	endCommandList->SetGraphicsRootDescriptorTable(6, otherTexDescriptor);
	endCommandList->SetGraphicsRootDescriptorTable(7, shadow2DDescriptorTable);
	endCommandList->SetGraphicsRootDescriptorTable(8, pointLightShadowCubeDescriptor);
	endCommandList->SetGraphicsRootDescriptorTable(9, ambientOcclusionDescriptor);
	endCommandList->SetGraphicsRootDescriptorTable(10, directionalShadowMaskDescriptor);
	endCommandList->RSSetViewports(1, &m_viewport);
	endCommandList->RSSetScissorRects(1, &m_scissorRect);
	endCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	std::vector<RenderItem*> fallbackDebugRenderItems;
	if (!useRecordedFramePlan)
		fallbackDebugRenderItems = CollectRenderItems(RitemLayer[debugrt]);
	const std::vector<RenderItem*>& debugRenderItems = useRecordedFramePlan ?
		FrameDebugRenderItems :
		fallbackDebugRenderItems;
	DrawRenderItems(endCommandList, debugRenderItems, debugPipelineState, 不透明物体管道);

	endCommandList->RSSetViewports(1, &m_viewport);
	endCommandList->RSSetScissorRects(1, &m_scissorRect);
	endCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	// 保持单命令列表收尾路径，避免状态切换分散导致的设备移除。
	auto postCommandList = endCommandList;
	postCommandList->RSSetViewports(1, &m_viewport);
	postCommandList->RSSetScissorRects(1, &m_scissorRect);
	postCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	if (hasOitResources)
	{
		D3D12_RESOURCE_BARRIER oitToShaderReadBarriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(transparentOitAccumResource,
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(transparentOitRevealResource,
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		postCommandList->ResourceBarrier(_countof(oitToShaderReadBarriers), oitToShaderReadBarriers);

		// --- OIT 合成通道 ---
		// 先把当前主颜色（已绘制 opaque + 近不透明透明）拷贝到 sceneColorResource，供全屏合成采样。
		auto sceneColorResource = CurrFrameResource->mPostProcessSceneColor.Get();
		if (sceneColorResource != nullptr)
		{
			D3D12_RESOURCE_BARRIER oitCompositePreBarriers[2] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(sceneColorResource,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
				CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE)
			};
			postCommandList->ResourceBarrier(_countof(oitCompositePreBarriers), oitCompositePreBarriers);
			postCommandList->CopyResource(sceneColorResource, SwapChainBuffer[CurrBackBufferIndex].Get());

			D3D12_RESOURCE_BARRIER oitCompositePostCopyBarriers[2] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
					D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
				CD3DX12_RESOURCE_BARRIER::Transition(sceneColorResource,
					D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
			};
			postCommandList->ResourceBarrier(_countof(oitCompositePostCopyBarriers), oitCompositePostCopyBarriers);

			mOITCompositePass.Draw(
				postCommandList,
				srvDescriptorHeaps,
				static_cast<UINT>(_countof(srvDescriptorHeaps)),
				CurrFrameResource->PostProcessCB->Resource()->GetGPUVirtualAddress(),
				postProcessSceneColorDescriptor,
				transparentOitAccumDescriptor,
				m_viewport,
				m_scissorRect,
				rtvHandle,
				dsvHandle);

			// 恢复默认 OM 绑定，避免后续文字/UI pass 受到影响。
			postCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
		}
	}

	// 选中对象描边（在主场景之后、FXAA 之前绘制，避免与 UI 混合）。
	std::vector<RenderItem*> fallbackSelectedOutlineItems;
	if (!useRecordedFramePlan)
		fallbackSelectedOutlineItems = CollectSelectedRenderItems();
	const std::vector<RenderItem*>& selectedOutlineItems = useRecordedFramePlan ?
		FrameSelectedOutlineRenderItems :
		fallbackSelectedOutlineItems;
	if (allowNoSkyPostProcessTail &&
		!selectedOutlineItems.empty() &&
		CurrFrameResource->mInteractionOutlineMask != nullptr)
	{
		ID3D12Resource* outlineMaskResource = CurrFrameResource->mInteractionOutlineMask.Get();
		CD3DX12_CPU_DESCRIPTOR_HANDLE outlineMaskRtvHandle(
			RtvHeap->GetCPUDescriptorHandleForHeapStart(),
			InteractionOutlineMaskRtvStartIndex + CurrBackBufferIndex,
			RtvDescriptorSize);

		InteractionOutlinePass::DrawCallback drawOutlineItemsCallback = std::bind(
			&D3DWindow::DrawOutlinePassItems,
			this,
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::cref(selectedOutlineItems));

		interactionOutlinePass.Draw(
			postCommandList,
			srvDescriptorHeaps,
			static_cast<UINT>(_countof(srvDescriptorHeaps)),
			CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
			m_viewport,
			m_scissorRect,
			rtvHandle,
			dsvHandle,
			outlineMaskResource,
			outlineMaskRtvHandle,
			interactionOutlineMaskDescriptor,
			drawOutlineItemsCallback);
	}

	// 体积光按“每灯全屏积分”渲染：完全不依赖场景网格，只按光体积数学求交。
	if (enableVolumetricLightPass)
	{
		const D3D12_GPU_DESCRIPTOR_HANDLE volumetricSceneDepthDescriptor =
			sharedNormalPrepass.GetSceneInputDepthSrv(
				AOSceneInputHeapStartIndex,
				CbvSrvUavDescriptorSize,
				CurrBackBufferIndex);
		if (volumetricSceneDepthDescriptor.ptr != 0 && DepthStencilBuffer != nullptr)
		{
			VolumetricLightPass::PrepareDrawCallback prepareVolumetricDrawCallback;
			if (CurrFrameResource != nullptr && CurrFrameResource->VolumetricLightObjectCB != nullptr)
			{
				const UINT objectCbByteSize = CalculateConstantBufferByteSize(sizeof(ObjectConstants));
				const D3D12_GPU_VIRTUAL_ADDRESS objectCbBaseAddress =
					CurrFrameResource->VolumetricLightObjectCB->Resource()->GetGPUVirtualAddress();
				prepareVolumetricDrawCallback = std::bind(
					&D3DWindow::PrepareVolumetricLightDrawObjectCB,
					CurrFrameResource->VolumetricLightObjectCB.get(),
					objectCbBaseAddress,
					objectCbByteSize,
					std::placeholders::_1,
					std::placeholders::_2);
			}

			// 体积光直接采样主场景最终深度。
			// 进入 SRV 读取前先解绑 DSV，再把状态切到 PIXEL_SHADER_RESOURCE。
			postCommandList->OMSetRenderTargets(1, &rtvHandle, true, nullptr);
			TransitionTrackedResourceState(
				postCommandList,
				DepthStencilBuffer.Get(),
				DepthStencilBufferState,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			volumetricLightPass.Draw(
				postCommandList,
				srvDescriptorHeaps,
				static_cast<UINT>(_countof(srvDescriptorHeaps)),
				CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
				CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress(),
				volumetricSceneDepthDescriptor,
				nullptr,
				rtvHandle,
				dsvHandle,
				LightsCache,
				MainPassCB.LightConst,
				ShadowConfig.DirectionalLightType,
				ShadowConfig.PointLightType,
				ShadowConfig.SpotLightType,
				prepareVolumetricDrawCallback);

			TransitionTrackedResourceState(
				postCommandList,
				DepthStencilBuffer.Get(),
				DepthStencilBufferState,
				D3D12_RESOURCE_STATE_DEPTH_WRITE);
			postCommandList->SetGraphicsRootDescriptorTable(8, pointLightShadowCubeDescriptor);
			postCommandList->SetGraphicsRootDescriptorTable(9, ambientOcclusionDescriptor);
			postCommandList->SetGraphicsRootDescriptorTable(10, directionalShadowMaskDescriptor);
			postCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
		}
	}

	// FXAA 在场景主内容绘制后执行，UI/文字仍保持锐利。
	if (allowNoSkyPostProcessTail &&
		IsFXAAEnabled() &&
		CurrFrameResource->PostProcessCB != nullptr &&
		CurrFrameResource->mPostProcessSceneColor != nullptr)
	{
		auto sceneColorResource = CurrFrameResource->mPostProcessSceneColor.Get();

		D3D12_RESOURCE_BARRIER fxaaPreBarriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(sceneColorResource,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE)
		};
		postCommandList->ResourceBarrier(_countof(fxaaPreBarriers), fxaaPreBarriers);

		postCommandList->CopyResource(sceneColorResource, SwapChainBuffer[CurrBackBufferIndex].Get());

		D3D12_RESOURCE_BARRIER fxaaPostBarriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
				D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
			CD3DX12_RESOURCE_BARRIER::Transition(sceneColorResource,
				D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		postCommandList->ResourceBarrier(_countof(fxaaPostBarriers), fxaaPostBarriers);

		mFXAAPass.Draw(
			postCommandList,
			srvDescriptorHeaps,
			static_cast<UINT>(_countof(srvDescriptorHeaps)),
			CurrFrameResource->PostProcessCB->Resource()->GetGPUVirtualAddress(),
			postProcessSceneColorDescriptor,
			m_viewport,
			m_scissorRect,
			rtvHandle,
			dsvHandle);

		// 恢复默认 OM 绑定，避免后续文字/UI pass 受到影响。
		postCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
	}

	DrawSkeletonOverlayPass(postCommandList, rtvHandle, dsvHandle);
	if (mSkinWeightVizPass.IsEnabled())
	{
		mSkinWeightVizPass.SetSrvDescriptorHeap(SrvDescriptorHeap.Get());
		mSkinWeightVizPass.SetOtherTexDescriptor(otherTexDescriptor);
		mSkinWeightVizPass.Draw(postCommandList, rtvHandle, dsvHandle);
	}
	DrawGizmoPass(postCommandList, rtvHandle, dsvHandle);

	if (renderFPS)
	{
		postCommandList->SetPipelineState(PipelineState[文字管道].Get());
		textR->DXDrawText(postCommandList, Text, DirectX::XMFLOAT2(0.32f, 0.25f), DirectX::XMFLOAT4{ 1.0f,1.0f,1.0f,1.0f }, CurrBackBufferIndex);
	}

	if (mEditor && renderEditor)
		mEditor->Render();

	D3D12_RESOURCE_BARRIER Barriers = {};
	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	postCommandList->ResourceBarrier(1, &Barriers);

	ThrowIfFailed(postCommandList->Close());

	// 阶段 8：提交 End 命令列表（包含 OIT 合成、体积光、FXAA/UI/Present 前状态切换）。
	ID3D12CommandList* postPhaseCommandLists[] = { postCommandList };
	CommandQueue->ExecuteCommandLists(_countof(postPhaseCommandLists), postPhaseCommandLists);

	// 阶段 9：Present + 更新背缓冲索引 + Signal 围栏。
	ThrowIfFailed(SwapChain->Present(0, 0));
	CurrBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();

	// 提升围栏值以将命令标记到该围栏点。
	CurrFrameResource->Fence = ++fenceValue;

	// 将指令添加到命令队列以设置新的围栏点。
	// 因为我们在GPU的时间线上，所以在GPU完成此Signal（）之前的所有命令处理之前，
	// 不会设置新的围栏点。
	ThrowIfFailed(CommandQueue->Signal(fence.Get(), fenceValue));

	// 每帧 signal 后尝试按 completed fence 回收延迟资源。
	DrainDeferredReleasesByCompletedFence();
	CurrentRenderFramePlan.Valid = false;
}

void D3DWindow::WorkerThread(int threadIndex)
{
	assert(threadIndex >= 0);
	assert(threadIndex < NumContexts);

	HANDLE passBeginEvents[工作阶段计数] =
	{
		workerBeginRecordCommand[阴影工作阶段][threadIndex],
		workerBeginRecordCommand[不透明工作阶段][threadIndex],
		workerBeginRecordCommand[半透明工作阶段][threadIndex],
		workerBeginRecordCommand[透明工作阶段][threadIndex],
		workerBeginRecordCommand[法线工作阶段][threadIndex]
	};

	// 工作线程常驻循环：
	// 1) 等待主线程唤醒某个阶段；
	// 2) 仅录制该阶段对应命令列表；
	// 3) 通过 finished 事件通知主线程可提交。
	while (threadIndex >= 0 && threadIndex < NumContexts)
	{
		const DWORD waitResult = WaitForMultipleObjects(工作阶段计数, passBeginEvents, FALSE, INFINITE);
		const UINT workerPhaseIndex = ResolveWorkerPhaseIndexFromWaitResult(waitResult);
		if (workerPhaseIndex >= 工作阶段计数)
			continue;

		// 工作线程每次只录制当前阶段对应的那份命令列表。
		auto workerCommandAllocator = GetWorkerCommandAllocator(workerPhaseIndex, threadIndex);
		auto workerCommandList = GetWorkerCommandList(workerPhaseIndex, threadIndex);
		ThrowIfFailed(workerCommandAllocator->Reset());
		ThrowIfFailed(workerCommandList->Reset(workerCommandAllocator, nullptr));

		const auto& opaqueRenderBatch = OpaqueThreadBatches[threadIndex];
		const auto& transparentRenderBatch = TransparentThreadBatches[threadIndex];
		const auto& staticShadowCasterRenderItems = FrameStaticShadowCasterRenderItems;
		const auto& dynamicShadowCasterRenderItems = FrameDynamicShadowCasterRenderItems;
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());
		ID3D12DescriptorHeap* srvDescriptorHeaps[] = { SrvDescriptorHeap.Get() };

		switch (workerPhaseIndex)
		{
		case 阴影工作阶段:
		{
			RecordWorkerShadowPass(
				threadIndex,
				workerCommandList,
				opaqueRenderBatch,
				transparentRenderBatch,
				staticShadowCasterRenderItems,
				dynamicShadowCasterRenderItems,
				srvDescriptorHeaps,
				static_cast<UINT>(_countof(srvDescriptorHeaps)));
			break;
		}
		case 法线工作阶段:
		{
			RecordWorkerNormalPass(
				threadIndex,
				workerCommandList,
				opaqueRenderBatch,
				transparentRenderBatch,
				srvDescriptorHeaps,
				static_cast<UINT>(_countof(srvDescriptorHeaps)));
			break;
		}
		case 半透明工作阶段:
		{
			RecordWorkerTranslucentPass(
				workerCommandList,
				transparentRenderBatch,
				rtvHandle,
				dsvHandle,
				srvDescriptorHeaps,
				static_cast<UINT>(_countof(srvDescriptorHeaps)));
			break;
		}
		case 透明工作阶段:
		{
			RecordWorkerTransparentPass(
				workerCommandList,
				transparentRenderBatch,
				dsvHandle,
				srvDescriptorHeaps,
				static_cast<UINT>(_countof(srvDescriptorHeaps)));
			break;
		}
		case 不透明工作阶段:
		default:
		{
			// 常规不透明绘制阶段直接输出到当前背缓冲。
			RecordWorkerOpaquePass(
				workerCommandList,
				opaqueRenderBatch,
				rtvHandle,
				dsvHandle,
				srvDescriptorHeaps,
				static_cast<UINT>(_countof(srvDescriptorHeaps)));
			break;
		}
		}

		ThrowIfFailed(workerCommandList->Close());
		SetEvent(workerFinishedRecordCommand[threadIndex]);
	}
}

void D3DWindow::DestroyRender()
{
	//确保GPU不再引用将由析构函数清除的资源。
	FlushCommandQueue();
}

void D3DWindow::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& rditems, ComPtr<ID3D12PipelineState> pipelineState, UINT pipelineNumber, UINT passCBIndex)
{
	if (rditems.empty()) return;

	UINT objCBByteSize = CalculateConstantBufferByteSize(sizeof(ObjectConstants));
	UINT skinningCBByteSize = CalculateConstantBufferByteSize(sizeof(SkinningConstants));
	UINT matCBByteSize = CalculateConstantBufferByteSize(sizeof(MaterialConstants));
	UINT passCBByteSize = CalculateConstantBufferByteSize(sizeof(PassConstants));

	auto objectCB = CurrFrameResource->ObjectCB->Resource();
	auto skinningCB = CurrFrameResource->SkinningCB != nullptr ? CurrFrameResource->SkinningCB->Resource() : nullptr;
	auto MatCB = CurrFrameResource->MaterialCB->Resource();
	auto passCB = CurrFrameResource->PassCB->Resource();

	// 主相机视角的常规绘制重新启用 CPU 视锥剔除。
	// 注意：DrawRenderItems 也被 shadow pass 复用，阴影 pass 不能使用主相机视锥剔除，
	// 否则视野外 caster 会被裁掉并再次导致方向光/点光/聚光阴影截断。
	const bool enableFrustumCulling =
		pipelineNumber != 阴影管道 &&
		pipelineNumber != 天空管道;
	DirectX::BoundingFrustum worldFrustum;
	if (enableFrustumCulling)
	{
		DirectX::XMFLOAT4X4 cullingViewStorage = MathHelps::Identity;
		DirectX::XMFLOAT4X4 cullingProjStorage = MathHelps::Identity;
		bool hasValidCullingReference = false;
		{
			std::lock_guard<std::mutex> lock(mFrustumCullingReferenceMutex);
			if (mFrustumCullingReferenceLocked)
			{
				cullingViewStorage = mLockedCullingView;
				cullingProjStorage = mLockedCullingProj;
				hasValidCullingReference = true;
			}
			else if (mHasLiveFrustumCullingReference)
			{
				cullingViewStorage = mLiveFrustumCullingView;
				cullingProjStorage = mLiveFrustumCullingProj;
				hasValidCullingReference = true;
			}
		}

		DirectX::BoundingFrustum viewFrustum;
		if (hasValidCullingReference)
		{
			// 使用锁定或本帧缓存矩阵构建剔除视锥，避免绘制线程直接读相机对象。
			const DirectX::XMMATRIX cullingProj = DirectX::XMLoadFloat4x4(&cullingProjStorage);
			const DirectX::XMMATRIX cullingView = DirectX::XMLoadFloat4x4(&cullingViewStorage);
			DirectX::BoundingFrustum::CreateFromMatrix(viewFrustum, cullingProj);
			const DirectX::XMMATRIX inverseView = DirectX::XMMatrixInverse(nullptr, cullingView);
			viewFrustum.Transform(worldFrustum, inverseView);
		}
		else
		{
			// 极早期兜底：尚未拿到本帧缓存时回退到当前相机矩阵。
			DirectX::BoundingFrustum::CreateFromMatrix(viewFrustum, mCamera.GetProj());
			const DirectX::XMMATRIX inverseView = DirectX::XMMatrixInverse(nullptr, mCamera.GetView());
			viewFrustum.Transform(worldFrustum, inverseView);
		}
	}

	if (pipelineState == nullptr)
		return;
	ID3D12PipelineState* lastBoundPipelineState = nullptr;

	for (auto ritem : rditems)
	{
		if (ritem == nullptr)
			continue;
		if (ritem->Geo == nullptr || ritem->Obj == nullptr || ritem->Obj->AggrObject == nullptr || ritem->Obj->Material == nullptr)
			continue;
		if (ritem->Obj->AggrObject->IndexCount == 0)
			continue;
		if (ritem->Geo->vertexBufferView.BufferLocation == 0 || ritem->Geo->vertexBufferView.SizeInBytes == 0)
			continue;
		if (ritem->Geo->indexBufferView.BufferLocation == 0 || ritem->Geo->indexBufferView.SizeInBytes == 0)
			continue;
		if (enableFrustumCulling && ShouldCullRenderItemByMainCameraFrustum(*ritem, worldFrustum))
			continue;

		ID3D12PipelineState* resolvedPipelineState = ResolvePipelineStateForRenderItem(
			ritem,
			pipelineState.Get(),
			pipelineNumber);
		if (resolvedPipelineState == nullptr)
			continue;
		if (resolvedPipelineState != lastBoundPipelineState)
		{
			cmdList->SetPipelineState(resolvedPipelineState);
			lastBoundPipelineState = resolvedPipelineState;
		}
		const UINT indexElementSizeInBytes = 4u;
		const UINT drawIndexStart = ritem->Obj->AggrObject->StartIndexLocation;
		const UINT drawIndexCount = ritem->Obj->AggrObject->IndexCount;
		const unsigned long long requiredIndexBytes =
			static_cast<unsigned long long>(drawIndexStart) * indexElementSizeInBytes +
			static_cast<unsigned long long>(drawIndexCount) * indexElementSizeInBytes;
		if (requiredIndexBytes > static_cast<unsigned long long>(ritem->Geo->indexBufferView.SizeInBytes))
		{
#ifdef _DEBUG
			EngineHelpers::AddLog(
				L"[DrawGuard] Skip invalid indexed draw: geo=%s objCB=%u skinned=%d pipeline=%u indexBufferBytes=%u startIndex=%u indexCount=%u requiredBytes=%llu baseVertex=%d",
				ritem->Geo->Name.c_str(),
				ritem->ObjCBIndex,
				ritem->IsSkinned ? 1 : 0,
				pipelineNumber,
				ritem->Geo->indexBufferView.SizeInBytes,
				drawIndexStart,
				drawIndexCount,
				requiredIndexBytes,
				ritem->Obj->AggrObject->BaseVertexLocation);
#endif
			continue;
		}

		const D3D12_VERTEX_BUFFER_VIEW* vertexBufferViewToBind = &ritem->Geo->vertexBufferView;
		if (kEnableComputeSkinning && ritem->IsSkinned && ritem->SourceEntity != nullptr)
		{
			SceneEntityBase* runtimeOwnerEntity =
				ResolveSkinningRuntimeOwnerEntity(mLastExternalECS, ritem->SourceEntity);
			const auto cacheEntryIt = mSkinnedDeformCache.find(runtimeOwnerEntity);
			if (cacheEntryIt != mSkinnedDeformCache.end())
			{
				const SkinnedDeformCacheEntry& cacheEntry = cacheEntryIt->second;
				if (cacheEntry.GpuResourcesReady &&
					CurrBackBufferIndex < cacheEntry.DeformedVertexBufferViews.size() &&
					cacheEntry.DeformedVertexBufferViews[CurrBackBufferIndex].BufferLocation != 0 &&
					cacheEntry.DeformedVertexBufferViews[CurrBackBufferIndex].SizeInBytes != 0)
				{
					vertexBufferViewToBind = &cacheEntry.DeformedVertexBufferViews[CurrBackBufferIndex];
				}
			}
		}

		// RenderItem 仅保存聚合缓冲中的绘制范围，实际几何数据在 Geo 中。
		cmdList->IASetVertexBuffers(0, 1, vertexBufferViewToBind);
		cmdList->IASetIndexBuffer(&ritem->Geo->indexBufferView);
		cmdList->IASetPrimitiveTopology(ritem->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE Tex(SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		if (ritem->Obj->Material->DiffuseTexture != nullptr)
			Tex = ritem->Obj->Material->DiffuseTexture->GetGPUTexDescriptor();

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress()
			+ ritem->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		D3D12_GPU_VIRTUAL_ADDRESS skinningCBAddress = 0;
		const bool canBindSkinningCB =
			ritem->IsSkinned && skinningCB != nullptr && ritem->SkinningCBIndex != UINT(-1);
		if (canBindSkinningCB)
		{
			skinningCBAddress = skinningCB->GetGPUVirtualAddress()
				+ ritem->SkinningCBIndex * skinningCBByteSize;
			cmdList->SetGraphicsRootConstantBufferView(4, skinningCBAddress);
		}

		// 法线绘制管道既需要 pass 常量（视图矩阵等），也需要材质/贴图常量（法线贴图开关与采样）。
		const bool requiresPassConstants = (pipelineNumber == 阴影管道 ||
			pipelineNumber == 法线绘制管道 ||
			pipelineNumber == InteractionOutlinePass::InteractionPipelineTag ||
			pipelineNumber == InteractionOutlinePass::OutlinePipelineTag);
		const bool requiresMaterialAndTexture = (pipelineNumber == 天空管道 ||
			pipelineNumber == 不透明物体管道 ||
			pipelineNumber == 透明物体管道 ||
			pipelineNumber == 半透明物体管道 ||
			pipelineNumber == 法线绘制管道);

		if (requiresPassConstants)
		{
			D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress()
				+ passCBIndex * passCBByteSize;
			cmdList->SetGraphicsRootConstantBufferView(1, passCBAddress);
		}

		if (requiresMaterialAndTexture)
		{
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = MatCB->GetGPUVirtualAddress()
				+ ritem->Obj->Material->MatCBIndex * matCBByteSize;
			cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
			cmdList->SetGraphicsRootDescriptorTable(6, Tex);
		}

		cmdList->DrawIndexedInstanced(ritem->Obj->AggrObject->IndexCount, 1, ritem->Obj->AggrObject->StartIndexLocation, ritem->Obj->AggrObject->BaseVertexLocation, 0);
	}
}

void D3DWindow::DrawGizmoPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
	if (!mGizmoPass.IsVisible())
		return;
	if (CurrFrameResource == nullptr || CurrFrameResource->PassCB == nullptr)
		return;

	const GizmoRenderData& renderData = mGizmoPass.GetRenderData();
	const std::wstring* geometryName = &TranslateGizmoGeometryName;
	switch (renderData.Mode)
	{
	case GizmoMode::Rotate:
		geometryName = &RotateGizmoGeometryName;
		break;
	case GizmoMode::Scale:
		geometryName = &ScaleGizmoGeometryName;
		break;
	case GizmoMode::Translate:
	default:
		geometryName = &TranslateGizmoGeometryName;
		break;
	}

	const auto geometryIt = Geometries.find(*geometryName);
	if (geometryIt == Geometries.end())
		return;
	const MeshGeometry& geometry = geometryIt->second;
	if (geometry.VertexBufferGPU == nullptr || geometry.IndexBufferGPU == nullptr)
		return;

	if (GizmoObjectCB == nullptr)
	{
		GizmoObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(d3dDevice.Get(), SwapChainBufferCount, true);
	}

	ObjectConstants gizmoObjectConstants{};
	const XMMATRIX gizmoWorldTransform =
		XMMatrixScaling(renderData.DrawScale, renderData.DrawScale, renderData.DrawScale) *
		XMMatrixTranslation(renderData.OriginWS.x, renderData.OriginWS.y, renderData.OriginWS.z);
	XMStoreFloat4x4(&gizmoObjectConstants.WorldTransform, XMMatrixTranspose(gizmoWorldTransform));
	XMFLOAT4X4 gizmoStateTransform = MathHelps::Identity;
	gizmoStateTransform._11 = static_cast<float>(static_cast<std::uint32_t>(renderData.HoverHandle));
	gizmoStateTransform._22 = static_cast<float>(static_cast<std::uint32_t>(renderData.ActiveHandle));
	XMStoreFloat4x4(&gizmoObjectConstants.TexTransform, XMMatrixTranspose(XMLoadFloat4x4(&gizmoStateTransform)));
	GizmoObjectCB->CopyData(CurrBackBufferIndex, gizmoObjectConstants);

	const UINT gizmoObjectCBByteSize = CalculateConstantBufferByteSize(sizeof(ObjectConstants));
	const D3D12_GPU_VIRTUAL_ADDRESS gizmoObjectCBAddress =
		GizmoObjectCB->Resource()->GetGPUVirtualAddress() +
		static_cast<UINT64>(CurrBackBufferIndex) * gizmoObjectCBByteSize;

	auto aggrObjectIt = AggrObject.find(*geometryName);
	if (aggrObjectIt == AggrObject.end())
		return;

	mGizmoPass.Draw(
		cmdList,
		gizmoObjectCBAddress,
		CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
		m_viewport,
		m_scissorRect,
		rtvHandle,
		dsvHandle,
		geometry.vertexBufferView,
		geometry.indexBufferView,
		aggrObjectIt->second.IndexCount,
		aggrObjectIt->second.StartIndexLocation,
		aggrObjectIt->second.BaseVertexLocation);
}

void D3DWindow::SetSkeletonOverlayRenderData(const SkeletonOverlayRenderData& renderData)
{
	mSkeletonOverlayPass.SetRenderData(renderData);
}

void D3DWindow::ClearSkeletonOverlayRenderData()
{
	mSkeletonOverlayPass.ClearRenderData();
}

void D3DWindow::DrawSkeletonOverlayPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
	if (!mSkeletonOverlayPass.IsVisible())
		return;
	if (CurrFrameResource == nullptr || CurrFrameResource->PassCB == nullptr || d3dDevice == nullptr)
		return;

	const SkeletonOverlayRenderData& overlayData = mSkeletonOverlayPass.GetRenderData();
	const UINT vertexCount = (std::min)(static_cast<UINT>(overlayData.Vertices.size()), MaxSkeletonOverlayVertices);
	const UINT indexCount = (std::min)(static_cast<UINT>(overlayData.Indices.size()), MaxSkeletonOverlayIndices);
	if (vertexCount < 2u || indexCount < 2u)
		return;

	if (SkeletonOverlayObjectCB == nullptr)
	{
		SkeletonOverlayObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(d3dDevice.Get(), SwapChainBufferCount, true);
	}
	if (SkeletonOverlayVertexBuffers[CurrBackBufferIndex] == nullptr)
	{
		SkeletonOverlayVertexBuffers[CurrBackBufferIndex] =
			std::make_unique<UploadBuffer<SkeletonOverlayGpuVertex>>(d3dDevice.Get(), MaxSkeletonOverlayVertices, false);
	}
	if (SkeletonOverlayIndexBuffers[CurrBackBufferIndex] == nullptr)
	{
		SkeletonOverlayIndexBuffers[CurrBackBufferIndex] =
			std::make_unique<UploadBuffer<std::uint32_t>>(d3dDevice.Get(), MaxSkeletonOverlayIndices, false);
	}

	for (UINT vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
	{
		SkeletonOverlayGpuVertex gpuVertex{};
		gpuVertex.Position = overlayData.Vertices[vertexIndex].Position;
		gpuVertex.Color = overlayData.Vertices[vertexIndex].Color;
		SkeletonOverlayVertexBuffers[CurrBackBufferIndex]->CopyData(static_cast<int>(vertexIndex), gpuVertex);
	}
	for (UINT indexIndex = 0; indexIndex < indexCount; ++indexIndex)
	{
		SkeletonOverlayIndexBuffers[CurrBackBufferIndex]->CopyData(
			static_cast<int>(indexIndex),
			overlayData.Indices[indexIndex]);
	}

	ObjectConstants objectConstants{};
	XMStoreFloat4x4(&objectConstants.WorldTransform, XMMatrixTranspose(XMMatrixIdentity()));
	XMStoreFloat4x4(&objectConstants.TexTransform, XMMatrixTranspose(XMMatrixIdentity()));
	SkeletonOverlayObjectCB->CopyData(CurrBackBufferIndex, objectConstants);

	const UINT objectCBByteSize = CalculateConstantBufferByteSize(sizeof(ObjectConstants));
	const D3D12_GPU_VIRTUAL_ADDRESS objectCBAddress =
		SkeletonOverlayObjectCB->Resource()->GetGPUVirtualAddress() +
		static_cast<UINT64>(CurrBackBufferIndex) * objectCBByteSize;

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = SkeletonOverlayVertexBuffers[CurrBackBufferIndex]->Resource()->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = vertexCount * sizeof(SkeletonOverlayGpuVertex);
	vertexBufferView.StrideInBytes = sizeof(SkeletonOverlayGpuVertex);

	D3D12_INDEX_BUFFER_VIEW indexBufferView{};
	indexBufferView.BufferLocation = SkeletonOverlayIndexBuffers[CurrBackBufferIndex]->Resource()->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = indexCount * sizeof(std::uint32_t);
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;

	mSkeletonOverlayPass.Draw(
		cmdList,
		objectCBAddress,
		CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
		m_viewport,
		m_scissorRect,
		rtvHandle,
		dsvHandle,
		vertexBufferView,
		indexBufferView,
		indexCount);
}

void D3DWindow::SetSkinWeightVisualizationEnabled(bool enable)
{
	mSkinWeightVizPass.SetEnabled(enable);
}

bool D3DWindow::IsSkinWeightVisualizationEnabled() const
{
	return mSkinWeightVizPass.IsEnabled();
}

void D3DWindow::SetSkinWeightVisualizationTarget(SceneEntityBase* entity)
{
	mSkinWeightVizPass.SetTargetEntity(entity);
}

SceneEntityBase* D3DWindow::GetSkinWeightVisualizationTarget() const
{
	return mSkinWeightVizPass.GetTargetEntity();
}

void D3DWindow::InvalidateSkinnedDeformCache(SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	auto cacheIt = mSkinnedDeformCache.find(entity);
	if (cacheIt == mSkinnedDeformCache.end())
		return;

	SkinnedDeformCacheEntry& cacheEntry = cacheIt->second;
	cacheEntry.GpuResourcesReady = false;
	cacheEntry.AssetLoaded = false;
	cacheEntry.LastPaletteRevision = 0;
	cacheEntry.VertexCount = 0;
	cacheEntry.VertexBufferByteSize = 0;
	cacheEntry.SourceVertexBuffer.Reset();
	cacheEntry.DeformedVertexBuffers = {};
	cacheEntry.DeformedVertexBufferViews = {};
	cacheEntry.DeformedVertexBufferStates =
	{
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COMMON
	};
}

void D3DWindow::IncrementalUpdateBrushWeightVB(const std::vector<std::tuple<const void*, UINT, Witchcraft::Animation::VertexBoneInfluence4>>& changes)
{
	mSkinWeightVizPass.IncrementalUpdateVB(changes);
}

// 根据刷权重模型数据重建可视化几何体。
// 遍历 .wmodel 的节点层级，将每个 MeshRef 引用的 mesh 顶点
// 按节点的累加本地变换预乘到模型空间，合并为一个统一 VB/IB。
void D3DWindow::RebuildBrushWeightVisualizationGeometry(const std::vector<BrushVizMeshSlice>& meshes, const BrushVizNode& rootNode)
{
	mSkinWeightVizPass.RebuildGeometry(meshes, rootNode);
}

const std::unordered_map<const void*, UINT>& D3DWindow::GetBrushWeightVizMeshOffsets() const
{
	return mSkinWeightVizPass.GetMeshOffsets();
}

void D3DWindow::BuildBrushWeightVisualization()
{
	mSkinWeightVizPass.BuildVisualization();
}

void D3DWindow::DrawSkinWeightVisualizationPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
	mSkinWeightVizPass.SetSrvDescriptorHeap(SrvDescriptorHeap.Get());
	mSkinWeightVizPass.SetOtherTexDescriptor(otherTexDescriptor);
	mSkinWeightVizPass.Draw(cmdList, rtvHandle, dsvHandle);
}

void D3DWindow::SetGizmoRenderData(const GizmoRenderData& renderData)
{
	mGizmoPass.SetRenderData(renderData);
}

void D3DWindow::ClearGizmoRenderData()
{
	mGizmoPass.ClearRenderData();
}

const GizmoPickMeshData& D3DWindow::GetTransformGizmoPickMesh(GizmoMode mode) const
{
	switch (mode)
	{
	case GizmoMode::Rotate:
		return RotateGizmoPickMesh;
	case GizmoMode::Scale:
		return ScaleGizmoPickMesh;
	case GizmoMode::Translate:
	default:
		return TranslateGizmoPickMesh;
	}
}

void D3DWindow::CloseCommandListAndSynchronize()
{
	// 命令列表在记录状态下创建，但是尚无记录。
	// 主循环期望它被关闭，所以现在就关闭它。
	CloseCommandList();

	ID3D12CommandList* ppCommandLists[] = { MainCommandList.Get() };
	CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// 等待命令列表执行；
	// 我们在主循环中重复使用了相同的命令列表，但就目前而言，我们只想等待安装完成后再继续。
	FlushCommandQueue();
}

void D3DWindow::FlushCommandQueue()
{
	if (fence == nullptr)
		return;

	fenceValue++;

	ThrowIfFailed(CommandQueue->Signal(fence.Get(), fenceValue));

	if (fence->GetCompletedValue() < fenceValue)
	{
		ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	DrainDeferredReleasesByCompletedFence();
}

void D3DWindow::ResetCommandList()
{
	// MainCommandList 每次重置前都必须先重置对应的 allocator。
	// 否则在批量导入模型（大量网格上传）时会持续累积 allocator 内存，
	// 最终可能触发驱动异常或设备移除。
	ThrowIfFailed(MainCommandAllocator->Reset());
	ThrowIfFailed(MainCommandList->Reset(MainCommandAllocator.Get(), nullptr));
	CommandListClose = false;
}

void D3DWindow::CloseCommandList()
{
	ThrowIfFailed(MainCommandList->Close());
	CommandListClose = true;
}

bool D3DWindow::IsCommandListClosed()
{
	return CommandListClose;
}

HWND D3DWindow::GetHwnd()
{
	return WinInfo.m_hWnd;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE D3DWindow::GetCpuSrv() const
{
	auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	return srv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE D3DWindow::GetGpuSrv() const
{
	auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	return srv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE D3DWindow::GetDsv() const
{
	auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(DsvHeap->GetCPUDescriptorHandleForHeapStart());
	return dsv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE D3DWindow::GetRtv() const
{
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
	return rtv;
}

UINT D3DWindow::GetRtvDescriptorSize()
{
	return RtvDescriptorSize;
}

UINT D3DWindow::GetDsvDescriptorSize()
{
	return DsvDescriptorSize;
}

UINT D3DWindow::GetCbvSrvUavDescriptorSize()
{
	return CbvSrvUavDescriptorSize;
}

DXGI_FORMAT D3DWindow::GetIndexBufferFormat() const
{
	return IndexBufferFormat;
}

AggregateGraphicObj* D3DWindow::GetAggregateGraphicObj(const std::wstring& geometryName)
{
	auto it = AggrObject.find(geometryName);
	if (it == AggrObject.end())
		return nullptr;

	return &it->second;
}

RenderItem* D3DWindow::GetRenderItem(const std::wstring& name)
{
	auto it = AllRitems.find(name);
	if (it == AllRitems.end())
		return nullptr;

	return &it->second;
}

void D3DWindow::RebindRenderItemGeometry(const std::wstring& renderItemName, ObjectCollection* objectCollection, const std::wstring& geometryName)
{
	if (renderItemName.empty() || objectCollection == nullptr || geometryName.empty())
		return;

	RenderItem* renderItem = GetRenderItem(renderItemName);
	if (renderItem == nullptr)
		return;

	auto geometryIt = Geometries.find(geometryName);
	if (geometryIt == Geometries.end())
		return;

	renderItem->Obj = objectCollection;
	renderItem->Geo = &geometryIt->second;
	renderItem->NumFramesDirty = SwapChainBufferCount;
	FreshenObjectCBs(renderItemName);
}


void D3DWindow::SetFPSRender(bool enable)
{
	renderFPS = enable;
}

bool D3DWindow::IsFPSRender() const
{
	return renderFPS;
}

float D3DWindow::GetShadowOpacity() const
{
	return MainPassCB.ShadowSettings.x;
}

void D3DWindow::SetShadowOpacity(float opacity)
{
	MainPassCB.ShadowSettings.x = std::clamp(opacity, ShadowConfig.MinOpacity, ShadowConfig.MaxOpacity);
}

float D3DWindow::GetShadowSoftness() const
{
	return MainPassCB.ShadowSettings.y;
}

void D3DWindow::SetShadowSoftness(float softness)
{
	MainPassCB.ShadowSettings.y = std::clamp(softness, ShadowConfig.MinSoftness, ShadowConfig.MaxSoftness);
}

bool D3DWindow::IsAOEnabled() const
{
	return AOConfig.Enabled;
}

void D3DWindow::SetAOEnabled(bool enable)
{
	AOConfig.Enabled = enable;
}

float D3DWindow::GetAOStrength() const
{
	return AOConfig.Strength;
}

void D3DWindow::SetAOStrength(float strength)
{
	AOConfig.Strength = std::clamp(strength, 0.0f, 1.0f);
}

float D3DWindow::GetAORadius() const
{
	return AOConfig.Radius;
}

void D3DWindow::SetAORadius(float radius)
{
	AOConfig.Radius = std::clamp(radius, 0.0f, 4.0f);
}

float D3DWindow::GetAOFadeStart() const
{
	return AOConfig.FadeStart;
}

void D3DWindow::SetAOFadeStart(float fadeStart)
{
	AOConfig.FadeStart = std::clamp(fadeStart, 0.0f, 100.0f);
	if (AOConfig.FadeEnd < AOConfig.FadeStart)
		AOConfig.FadeEnd = AOConfig.FadeStart;
}

float D3DWindow::GetAOFadeEnd() const
{
	return AOConfig.FadeEnd;
}

void D3DWindow::SetAOFadeEnd(float fadeEnd)
{
	AOConfig.FadeEnd = std::max(std::clamp(fadeEnd, 0.0f, 100.0f), AOConfig.FadeStart);
}

float D3DWindow::GetAOSurfaceEpsilon() const
{
	return AOConfig.SurfaceEpsilon;
}

void D3DWindow::SetAOSurfaceEpsilon(float epsilon)
{
	AOConfig.SurfaceEpsilon = std::clamp(epsilon, 0.0001f, 1.0f);
}

float D3DWindow::GetAOBlurSigma() const
{
	return AOConfig.BlurSigma;
}

void D3DWindow::SetAOBlurSigma(float sigma)
{
	AOConfig.BlurSigma = std::clamp(sigma, 0.1f, 2.5f);
}

bool D3DWindow::IsFXAAEnabled() const
{
	return MainPostProcessCB.FxaaSettings.x > 0.5f;
}

void D3DWindow::SetFXAAEnabled(bool enable)
{
	MainPostProcessCB.FxaaSettings.x = enable ? 1.0f : 0.0f;
}

float D3DWindow::GetFXAAContrastThreshold() const
{
	return MainPostProcessCB.FxaaSettings.y;
}

void D3DWindow::SetFXAAContrastThreshold(float threshold)
{
	MainPostProcessCB.FxaaSettings.y = std::clamp(threshold, 0.0f, 1.0f);
}

float D3DWindow::GetFXAARelativeThreshold() const
{
	return MainPostProcessCB.FxaaSettings.z;
}

void D3DWindow::SetFXAARelativeThreshold(float threshold)
{
	MainPostProcessCB.FxaaSettings.z = std::clamp(threshold, 0.0f, 1.0f);
}

float D3DWindow::GetFXAASpanMax() const
{
	return MainPostProcessCB.FxaaSettings.w;
}

void D3DWindow::SetFXAASpanMax(float spanMax)
{
	MainPostProcessCB.FxaaSettings.w = std::clamp(spanMax, 1.0f, 32.0f);
}

DirectX::XMFLOAT3 D3DWindow::GetPosition3f() const
{
	return mCamera.GetPosition3f();
}

void D3DWindow::SetPosition3f(DirectX::XMFLOAT3 Position)
{
	mCamera.SetPosition(Position);
}

DirectX::XMFLOAT3 D3DWindow::GetRotation3f() const
{
	return mCamera.GetRotation3f();
}

void D3DWindow::SetRotation3f(DirectX::XMFLOAT3 Rotation)
{
	mCamera.SetRotation(Rotation);
}

float D3DWindow::GetCameraSpeed()
{
	return mCamera.CameraParame.camSpeed;
}

void D3DWindow::SetCameraSpeed(float speed)
{
	mCamera.CameraParame.camSpeed = speed;
}

float D3DWindow::GetNearZ() const
{
	return mCamera.GetNearZ();
}

float D3DWindow::GetFarZ() const
{
	return mCamera.GetFarZ();
}

float D3DWindow::GetFovY() const
{
	return mCamera.GetFovY();
}

float D3DWindow::GetFovX() const
{
	return mCamera.GetFovX();
}

void D3DWindow::SetNearZ(float nearZ)
{
	mCamera.SetNearZ(nearZ);
}

void D3DWindow::SetFarZ(float farZ)
{
	mCamera.SetFarZ(farZ);
}

void D3DWindow::SetFovY(float fovY)
{
	mCamera.SetFovY(fovY);
}

float D3DWindow::GetViewportScale()const
{
	return mCamera.GetViewportScale();
}

void D3DWindow::SetViewportScale(float scale)
{
	mCamera.SetViewportScale(scale);
}

void D3DWindow::RestoreScale()
{
	mCamera.SetViewportScale(static_cast<float>(EngineHelpers::GetContextWidth(WinInfo.m_hWnd)) /
		static_cast<float>(EngineHelpers::GetContextHeight(WinInfo.m_hWnd)));
}

DirectX::XMMATRIX D3DWindow::GetView() const
{
	return mCamera.GetView();
}

DirectX::XMMATRIX D3DWindow::GetProj() const
{
	return mCamera.GetProj();
}

void D3DWindow::ToggleFrustumCullingReferenceLock()
{
	std::lock_guard<std::mutex> lock(mFrustumCullingReferenceMutex);
	if (mFrustumCullingReferenceLocked)
	{
		mFrustumCullingReferenceLocked = false;
		return;
	}

	// 优先锁定到本帧缓存，避免输入线程直接读取相机对象带来的并发风险。
	if (mHasLiveFrustumCullingReference)
	{
		mLockedCullingView = mLiveFrustumCullingView;
		mLockedCullingProj = mLiveFrustumCullingProj;
	}
	else
	{
		DirectX::XMStoreFloat4x4(&mLockedCullingView, mCamera.GetView());
		DirectX::XMStoreFloat4x4(&mLockedCullingProj, mCamera.GetProj());
	}
	mFrustumCullingReferenceLocked = true;
}

bool D3DWindow::IsFrustumCullingReferenceLocked() const
{
	std::lock_guard<std::mutex> lock(mFrustumCullingReferenceMutex);
	return mFrustumCullingReferenceLocked;
}

void D3DWindow::RotateCamera(float DeltaTime, DirectX::XMFLOAT2 angle)
{
	mCamera.RotateCamera(DeltaTime, angle);
}

void D3DWindow::MoveCamera(float DeltaTime, DirectX::XMFLOAT3 distance)
{
	mCamera.MoveCamera(DeltaTime, distance);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 8> D3DWindow::GetStaticSamplers()
{
	// 应用程序通常只需要少量的采样器。
	// 因此，只需预先定义它们，并将其作为根签名的一部分。

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

	// 点光源 cubemap 阴影沿用比较采样，但地址模式要用 clamp，
	// 避免 cube 面边界附近访问落到 border 颜色。
	const CD3DX12_STATIC_SAMPLER_DESC shadowCube(
		7, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp,
		shadow, shadowCube};
}

ComPtr<ID3D12Resource> D3DWindow::CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer)
{
	ComPtr<ID3D12Resource> defaultBuffer;

	// default heap 是最终给 GPU 读取的正式资源。
	D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
	//创建实际的默认缓冲区资源。
	ThrowIfFailed(device->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&Desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

	// upload heap 作为中转缓冲，把 CPU 侧初始化数据拷贝到 default heap。
	HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	//为了将CPU内存数据复制到我们的默认缓冲区中，我们需要创建一个中间上传堆。
	ThrowIfFailed(device->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&Desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


	//描述我们要复制到默认缓冲区的数据。
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	//计划将数据复制到默认缓冲区资源。 在较高级别上，辅助函数UpdateSubresources
	//将CPU内存复制到中间上传堆中。
	//然后，使用ID3D12CommandList::Copy子资源区域，将中间上传堆数据复制到mBuffer。
	D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &Barriers);
	UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(),
		0, 0, 1, &subResourceData);
	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &Barriers);

	//注意：在上述函数调用之后，uploadBuffer必须保持活动状态，因为尚未执行实际复制的命令列表。
	//直到复制已执行后，调用者可以释放uploadBuffer，所以我们要保证这份资源存活并能被调用。
	return defaultBuffer;
}
