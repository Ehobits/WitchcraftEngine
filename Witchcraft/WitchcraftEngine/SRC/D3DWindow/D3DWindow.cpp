#include "D3DWindow.h"
#include "D3DWindowAssetHelpers.h"
#include "D3DWindowShadowHelpers.h"
#include "HELPERS/Helpers.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "ECS/WitchcraECS.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/CameraComponent.h"
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

#include <cmath>
#include <limits>

static constexpr UINT kBrdfLutSize = 256;

static float RadicalInverseVdc(UINT bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

static DirectX::XMFLOAT2 Hammersley(UINT index, UINT sampleCount)
{
	return DirectX::XMFLOAT2(
		static_cast<float>(index) / static_cast<float>(sampleCount),
		RadicalInverseVdc(index));
}

static DirectX::XMVECTOR ImportanceSampleGgx(const DirectX::XMFLOAT2& xi, float roughness)
{
	const float a = roughness * roughness;
	const float phi = 2.0f * MathHelps::Pi * xi.x;
	const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
	const float sinTheta = std::sqrt((std::max)(0.0f, 1.0f - cosTheta * cosTheta));

	return DirectX::XMVectorSet(
		sinTheta * std::cos(phi),
		sinTheta * std::sin(phi),
		cosTheta,
		0.0f);
}

static float GeometrySchlickGgxForIbl(float nDotV, float roughness)
{
	const float a = roughness;
	const float k = (a * a) / 2.0f;
	return nDotV / (nDotV * (1.0f - k) + k);
}

static float GeometrySmithForIbl(float nDotV, float nDotL, float roughness)
{
	return GeometrySchlickGgxForIbl(nDotV, roughness) * GeometrySchlickGgxForIbl(nDotL, roughness);
}

static DirectX::XMFLOAT2 IntegrateBrdf(float nDotV, float roughness)
{
	constexpr UINT kSampleCount = 128;
	const DirectX::XMVECTOR V = DirectX::XMVectorSet(std::sqrt((std::max)(0.0f, 1.0f - nDotV * nDotV)), 0.0f, nDotV, 0.0f);
	float scale = 0.0f;
	float bias = 0.0f;

	for (UINT sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex)
	{
		const DirectX::XMFLOAT2 xi = Hammersley(sampleIndex, kSampleCount);
		const DirectX::XMVECTOR H = ImportanceSampleGgx(xi, roughness);
		const DirectX::XMVECTOR L = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMVectorScale(H, 2.0f * DirectX::XMVectorGetX(DirectX::XMVector3Dot(V, H))), V));

		const float nDotL = (std::max)(DirectX::XMVectorGetZ(L), 0.0f);
		const float nDotH = (std::max)(DirectX::XMVectorGetZ(H), 0.0f);
		const float vDotH = (std::max)(DirectX::XMVectorGetX(DirectX::XMVector3Dot(V, H)), 0.0f);

		if (nDotL > 0.0f)
		{
			const float geometry = GeometrySmithForIbl(nDotV, nDotL, roughness);
			const float geometryVisible = geometry * vDotH / (std::max)(nDotH * nDotV, 1.0e-4f);
			const float fresnel = std::pow(1.0f - vDotH, 5.0f);
			scale += (1.0f - fresnel) * geometryVisible;
			bias += fresnel * geometryVisible;
		}
	}

	return DirectX::XMFLOAT2(scale / kSampleCount, bias / kSampleCount);
}

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
	if (!renderItem->IsSkinned || !usesSkinnedVertexLayout)
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
		for (UINT cascadeIndex = 0; cascadeIndex < _countof(DirectionalCascadeShadowPipelineState); ++cascadeIndex)
		{
			if (defaultPipelineState == DirectionalCascadeShadowPipelineState[cascadeIndex].Get())
			{
				return SkinnedDirectionalCascadeShadowPipelineState[cascadeIndex] != nullptr
					? SkinnedDirectionalCascadeShadowPipelineState[cascadeIndex].Get()
					: defaultPipelineState;
			}
		}
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
	// 相机对象自身不提供渲染剔除 bounds。
	// 返回 false 表示“没有可用于 frustum 求交的包围盒”；不能返回 true，
	// 否则调用方会把未写入的 outWorldBounds 当成有效 bounds 使用。
	if (ecs->GetComponent<CameraComponent>(entity))
		return false;

	TransformComponent* transformComponent = ecs->GetComponent<TransformComponent>(entity);

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

	SceneEntityBase* cullingRootEntity =
		renderItem.CullingSourceEntity != nullptr ? renderItem.CullingSourceEntity : renderItem.SourceEntity;
	// 相机对象自身不要参与到这个过程中来
	if (mLastExternalECS->GetComponent<CameraComponent>(cullingRootEntity))
		return true;

	bool hasAnyCullingBounds = false;
	if (renderItem.HasLocalBounds)
	{
		DirectX::BoundingBox worldBounds;
		renderItem.LocalBounds.Transform(worldBounds, DirectX::XMLoadFloat4x4(&renderItem.WorldTransform));
		hasAnyCullingBounds = true;
		if (DoesCullingBoundsIntersectFrustum(worldBounds, worldFrustum))
			return false;
	}

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
		if (cacheResource.Width != texture2DSizes[slotIndex] ||
			cacheResource.Height != texture2DSizes[slotIndex])
		{
			return false;
		}
	}

	for (UINT cubeIndex = 0; cubeIndex < pointLightCubeSizes.size(); ++cubeIndex)
	{
		const ShadowCacheCubeResource& cacheResource = staticPointLightShadowCubeResources[cubeIndex];
		if (cacheResource.FaceSize != pointLightCubeSizes[cubeIndex])
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
	ObjectConstants objectConstants = {};
	objectConstants.WorldTransform = MathHelps::Identity;
	objectConstants.WorldInvTranspose = MathHelps::Identity;
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

bool D3DWindow::TryBuildRenderToTextureDescriptorHandles(
	UINT slotIndex,
	CD3DX12_CPU_DESCRIPTOR_HANDLE* outCpuSrv,
	CD3DX12_GPU_DESCRIPTOR_HANDLE* outGpuSrv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE* outCpuRtv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE* outCpuDsv) const
{
	if (!RenderToTextureDescriptorsReserved || slotIndex >= MaxRenderToTextureCount)
		return false;

	*outCpuSrv = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		RenderToTextureSrvStartIndex + slotIndex,
		CbvSrvUavDescriptorSize);
	*outGpuSrv = CD3DX12_GPU_DESCRIPTOR_HANDLE(
		SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		RenderToTextureSrvStartIndex + slotIndex,
		CbvSrvUavDescriptorSize);
	*outCpuRtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		RtvHeap->GetCPUDescriptorHandleForHeapStart(),
		RenderToTextureRtvStartIndex + slotIndex,
		RtvDescriptorSize);
	*outCpuDsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		DsvHeap->GetCPUDescriptorHandleForHeapStart(),
		RenderToTextureDsvStartIndex + slotIndex,
		DsvDescriptorSize);
	return true;
}

UINT D3DWindow::GetRenderToTexturePassCBIndex(UINT slotIndex) const
{
	return 1u + ShadowConfig.MaxShadowMapCount * 6u + slotIndex;
}

bool D3DWindow::TryGetRenderToTextureSlotIndex(std::uint32_t id, UINT* outSlotIndex) const
{
	if (outSlotIndex == nullptr)
		return false;

	const auto slotIt = mRenderToTextureSlotById.find(id);
	if (slotIt == mRenderToTextureSlotById.end())
		return false;

	*outSlotIndex = slotIt->second;
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
	mColorAdjustPass.Initialize(d3dDevice.Get());
	mOITCompositePass.Initialize(d3dDevice.Get());
	mFXAAPass.Initialize(d3dDevice.Get());
	mSkeletonOverlayPass.Initialize(d3dDevice.Get());
	mGizmoPass.Initialize(d3dDevice.Get());
	mSkinningComputePass.Initialize(d3dDevice.Get());
	mRenderToTextureManager.Initialize(d3dDevice.Get());
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
	BuildBrdfLutTexture(MainCommandList.Get());
	if (!TryReserveSrvDescriptorSlots(3, L"EnvironmentLighting Reserved SRV Range", &EnvironmentIblHeapStartIndex))
		return false;
	EnvironmentIblDescriptorsInitialized = true;
	BuildEnvironmentLightingDescriptors();

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
		SwapChainBufferCount + 3 + SwapChainBufferCount * 2 + SwapChainBufferCount * 3;
	if (!TryReserveSrvDescriptorSlots(2, L"DirectionalShadowMask Reserved SRV Range", &DirectionalShadowMaskHeapStartIndex))
		return false;
	DirectionalShadowMaskDescriptorsInitialized = true;
	BuildDirectionalShadowMaskDescriptors();

	// 后处理场景颜色 SRV：每个后备缓冲对应一个输入纹理。
	if (!TryReserveSrvDescriptorSlots(SwapChainBufferCount, L"PostProcessSceneColor Reserved SRV Range", &PostProcessSceneColorHeapStartIndex))
		return false;
	PostProcessSceneColorDescriptorsInitialized = true;
	BuildPostProcessSceneColorDescriptors();

	// 色彩调整输出 SRV：每个后备缓冲对应一个中间纹理。
	if (!TryReserveSrvDescriptorSlots(SwapChainBufferCount, L"ColorAdjustSceneColor Reserved SRV Range", &ColorAdjustSceneColorHeapStartIndex))
		return false;
	ColorAdjustSceneColorDescriptorsInitialized = true;
	BuildColorAdjustSceneColorDescriptors();

	// 交互描边遮罩 SRV：每个后备缓冲对应一个遮罩纹理。
	if (!TryReserveSrvDescriptorSlots(SwapChainBufferCount, L"InteractionOutlineMask Reserved SRV Range", &InteractionOutlineMaskHeapStartIndex))
		return false;
	InteractionOutlineMaskDescriptorsInitialized = true;
	BuildInteractionOutlineMaskDescriptors();

	// 共享场景输入 SRV：每帧占 2 个（normal/sceneDepth），AO 和体积光都会复用。
	if (!TryReserveSrvDescriptorSlots(SwapChainBufferCount * 2, L"SharedSceneInput Reserved SRV Range", &SharedSceneInputHeapStartIndex))
		return false;
	SharedSceneInputDescriptorsInitialized = true;
	BuildSharedSceneInputDescriptors();

	// 透明 OIT SRV：每帧占 2 个（accum/reveal），按 [accum, reveal] 成对连续存放。
	if (!TryReserveSrvDescriptorSlots(SwapChainBufferCount * 2, L"TransparentOit Reserved SRV Range", &TransparentOitHeapStartIndex))
		return false;
	TransparentOitDescriptorsInitialized = true;
	BuildTransparentOitDescriptors();

	RenderToTextureRtvStartIndex =
		DirectionalShadowMaskRtvStartIndex + 2;
	RenderToTextureDsvStartIndex =
		SharedNormalPrepassDepthDsvIndex + 1;
	if (!TryReserveSrvDescriptorSlots(MaxRenderToTextureCount, L"RenderToTexture Reserved SRV Range", &RenderToTextureSrvStartIndex))
		return false;
	RenderToTextureDescriptorsReserved = true;

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
	// 5) 色彩调整中间 RT：每帧 1 个；
	// 6) 交互描边遮罩 RTV：每帧 1 个；
	// 7) DirectionalShadowMask: mask + blur temp 共 2 个。
	// 8) RenderToTexture: 固定预留 MaxRenderToTextureCount 个离屏 color RTV。
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors =
		SwapChainBufferCount + 3 + SwapChainBufferCount * 2 + SwapChainBufferCount * 3 + 2 +
		MaxRenderToTextureCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(RtvHeap.GetAddressOf())));

	// 为主深度、全部阴影贴图、全局 AO 深度和 RenderToTexture 深度预留 DSV。
	// 现在点光源阴影改为 cubemap：1 个逻辑槽位对应 6 个物理面 DSV。
	// 因此这里不能再按“MaxShadowMapCount 个逻辑槽位 = MaxShadowMapCount 个 DSV”估算。
	const UINT maxShadowDsvCount = ShadowConfig.MaxShadowMapCount * 6;
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1 + maxShadowDsvCount + 1 + MaxRenderToTextureCount;
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
		mFrameResources[i].mColorAdjustSceneColor.Reset();
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

	// 色彩调整中间缓冲：保存白平衡 / 对比度 / 饱和度 pass 的输出。
	CD3DX12_RESOURCE_DESC colorAdjustSceneColorDesc = postProcessSceneColorDesc;
	const float colorAdjustSceneColorClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	CD3DX12_CLEAR_VALUE colorAdjustSceneColorClearValue(BackBufferFormat, colorAdjustSceneColorClearColor);
	D3D12_RENDER_TARGET_VIEW_DESC colorAdjustSceneColorRtvDesc = postProcessSceneColorRtvDesc;
	ColorAdjustSceneColorRtvStartIndex = PostProcessSceneColorRtvStartIndex + SwapChainBufferCount;

	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&colorAdjustSceneColorDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&colorAdjustSceneColorClearValue,
			IID_PPV_ARGS(mFrameResources[i].mColorAdjustSceneColor.GetAddressOf())));
		mFrameResources[i].mColorAdjustSceneColor->SetName((L"ColorAdjustSceneColor" + std::to_wstring(i)).c_str());

		CD3DX12_CPU_DESCRIPTOR_HANDLE colorAdjustSceneColorRtvHandle(
			RtvHeap->GetCPUDescriptorHandleForHeapStart(),
			ColorAdjustSceneColorRtvStartIndex + i,
			RtvDescriptorSize);
		d3dDevice->CreateRenderTargetView(
			mFrameResources[i].mColorAdjustSceneColor.Get(),
			&colorAdjustSceneColorRtvDesc,
			colorAdjustSceneColorRtvHandle);
	}

	// 交互描边遮罩：独立于后处理场景颜色，避免状态切换互相踩踏。
	CD3DX12_RESOURCE_DESC interactionOutlineMaskDesc = postProcessSceneColorDesc;
	const float interactionOutlineMaskClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	CD3DX12_CLEAR_VALUE interactionOutlineMaskClearValue(BackBufferFormat, interactionOutlineMaskClearColor);
	D3D12_RENDER_TARGET_VIEW_DESC interactionOutlineMaskRtvDesc = postProcessSceneColorRtvDesc;
	InteractionOutlineMaskRtvStartIndex = ColorAdjustSceneColorRtvStartIndex + SwapChainBufferCount;

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
	BuildColorAdjustSceneColorDescriptors();
	BuildInteractionOutlineMaskDescriptors();
	BuildSharedSceneInputDescriptors();
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
		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] =
		{
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3DRenderBindingContract::SkyTexRegister, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 12, D3DRenderBindingContract::OtherTexRegister, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, D3DRenderBindingContract::DirectionalShadowRegisterCount, D3DRenderBindingContract::ShadowRegisterStart, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, D3DRenderBindingContract::SpotShadowRegisterCount, D3DRenderBindingContract::SpotShadowRegisterStart, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, D3DRenderBindingContract::PointLightCubeRegisterCount, D3DRenderBindingContract::PointLightCubeRegisterStart, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3DRenderBindingContract::AmbientOcclusionRegister, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3DRenderBindingContract::DirectionalShadowMaskRegister, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3DRenderBindingContract::ReflectionRegister, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3DRenderBindingContract::EnvironmentIblRegisterStart, 0}
		};

		// 根参数可以是表，根描述符或根常量。
		CD3DX12_ROOT_PARAMETER1 slotRootParameter[D3DRenderBindingContract::MainRootParameterCount];

		// 创建根CBV。效果提示：从最频繁到最不频繁的顺序
		slotRootParameter[D3DRenderBindingContract::ObjectCB].InitAsConstantBufferView(0);
		slotRootParameter[D3DRenderBindingContract::PassCB].InitAsConstantBufferView(1);
		slotRootParameter[D3DRenderBindingContract::LightCB].InitAsConstantBufferView(2);
		slotRootParameter[D3DRenderBindingContract::MaterialCB].InitAsConstantBufferView(3);
		slotRootParameter[D3DRenderBindingContract::SkinningCB].InitAsConstantBufferView(4);
		slotRootParameter[D3DRenderBindingContract::SkyTexTable].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[D3DRenderBindingContract::OtherTexTable].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[D3DRenderBindingContract::Shadow2DTable].InitAsDescriptorTable(2, &descriptorRanges[2], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[D3DRenderBindingContract::PointLightShadowCubeTable].InitAsDescriptorTable(1, &descriptorRanges[4], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[D3DRenderBindingContract::AmbientOcclusionTable].InitAsDescriptorTable(1, &descriptorRanges[5], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[D3DRenderBindingContract::DirectionalShadowMaskTable].InitAsDescriptorTable(1, &descriptorRanges[6], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[D3DRenderBindingContract::ReflectionTable].InitAsDescriptorTable(1, &descriptorRanges[7], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[D3DRenderBindingContract::EnvironmentIblTable].InitAsDescriptorTable(1, &descriptorRanges[8], D3D12_SHADER_VISIBILITY_PIXEL);

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
	D3D_SHADER_MACRO skinnedOpaqueDefines[] =
	{
		{ "SKINNED_MESH", "1" },
		{ nullptr, nullptr }
	};
	D3D_SHADER_MACRO skinnedTransparentPassDefines[] =
	{
		{ "SKINNED_MESH", "1" },
		{ "TRANSPARENT_PASS", "1" },
		{ "TRANSPARENT_OIT_PASS", "1" },
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
	ComPtr<ID3DBlob> skinnedOpaqueVertexShader = CompileShader(L"DATA/Shaders/pbrx", skinnedOpaqueDefines, "VS", "vs_5_1");
	ComPtr<ID3DBlob> skinnedTransparentVertexShader = CompileShader(L"DATA/Shaders/pbrx", skinnedTransparentPassDefines, "VS", "vs_5_1");
	vertexShader[蒙皮半透明着色器] = CompileShader(L"DATA/Shaders/pbrx", skinnedTransparentNearOpaqueDefines, "VS", "vs_5_1");
	ComPtr<ID3DBlob> skinnedShadowVertexShader = CompileShader(L"DATA/Shaders/Shadows", skinnedOpaqueDefines, "VS", "vs_5_1");
	ComPtr<ID3DBlob> skinnedDrawNormalsVertexShader = CompileShader(L"DATA/Shaders/DrawNormals", skinnedOpaqueDefines, "VS", "vs_5_1");

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

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedOpaquePsoDesc = opaquePsoDesc;
	skinnedOpaquePsoDesc.InputLayout = { SkinnedInputElementDescs.data(), (UINT)SkinnedInputElementDescs.size() };
	skinnedOpaquePsoDesc.VS = CD3DX12_SHADER_BYTECODE(skinnedOpaqueVertexShader.Get());
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&skinnedOpaquePsoDesc,
		IID_PPV_ARGS(&SkinnedOpaquePipelineState)));
	SetD3DObjectName(SkinnedOpaquePipelineState.Get(), L"管线_蒙皮不透明物体_PBR");

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
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedTransparentPsoDesc = transparentPsoDesc;
	skinnedTransparentPsoDesc.InputLayout = { SkinnedInputElementDescs.data(), static_cast<UINT>(SkinnedInputElementDescs.size()) };
	skinnedTransparentPsoDesc.VS = CD3DX12_SHADER_BYTECODE(skinnedTransparentVertexShader.Get());
	ComPtr<ID3D12PipelineState> newSkinnedTransparentPso = nullptr;
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&skinnedTransparentPsoDesc,
		IID_PPV_ARGS(newSkinnedTransparentPso.GetAddressOf())));
	SetD3DObjectName(newSkinnedTransparentPso.Get(), L"管线_蒙皮透明OIT累积");
	SkinnedTransparentPipelineState = newSkinnedTransparentPso;

	//
	//用于阴影贴图传递的PSO。
	//
	shadowMapPass.CreatePipesAndShaders(vertexShader[阴影着色器], pixelShader[阴影着色器],
		basePsoDesc, PipelineState[阴影管道]);
	SetD3DObjectName(PipelineState[阴影管道].Get(), L"管线_阴影深度_通用");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedShadowPsoDesc = basePsoDesc;
	skinnedShadowPsoDesc.InputLayout = { SkinnedInputElementDescs.data(), (UINT)SkinnedInputElementDescs.size() };
	skinnedShadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(skinnedShadowVertexShader.Get());
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
		skinnedDirectionalCascadeShadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(skinnedShadowVertexShader.Get());
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
	skinnedDrawNormalsPsoDesc.VS = CD3DX12_SHADER_BYTECODE(skinnedDrawNormalsVertexShader.Get());
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
		mSkinWeightVizPass.CreatePipesAndShaders(skinWeightVizBasePsoDesc);

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
	volumetricLightPass.CreatePipesAndShaders(postProcessPsoDesc);

	//
	//文字的PSO。
	//
	textR->CreatePipesAndShaders(BackBufferFormat, DepthStencilFormat, &PipelineState[文字管道]);

	// 所有图形 Pass 共享主根签名。
	mGizmoPass.SetSharedRootSignature(RootSignature.Get());
	mSkeletonOverlayPass.SetSharedRootSignature(RootSignature.Get());
	mColorAdjustPass.SetSharedRootSignature(RootSignature.Get());
	mOITCompositePass.SetSharedRootSignature(RootSignature.Get());
	mFXAAPass.SetSharedRootSignature(RootSignature.Get());

	mOITCompositePass.CreatePipesAndShaders(basePsoDesc);
	mColorAdjustPass.CreatePipesAndShaders(basePsoDesc);
	mFXAAPass.CreatePipesAndShaders(basePsoDesc);
	mSkeletonOverlayPass.CreatePipesAndShaders(basePsoDesc);
	mGizmoPass.CreatePipesAndShaders(basePsoDesc);
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

void D3DWindow::BuildBrdfLutTexture(ID3D12GraphicsCommandList* cmdList)
{
	if (d3dDevice == nullptr || cmdList == nullptr)
		return;

	std::vector<DirectX::XMFLOAT2> lutData(kBrdfLutSize * kBrdfLutSize);
	for (UINT y = 0; y < kBrdfLutSize; ++y)
	{
		const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(kBrdfLutSize);
		for (UINT x = 0; x < kBrdfLutSize; ++x)
		{
			const float nDotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(kBrdfLutSize);
			lutData[y * kBrdfLutSize + x] = IntegrateBrdf(nDotV, roughness);
		}
	}

	const CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32G32_FLOAT,
		kBrdfLutSize,
		kBrdfLutSize,
		1,
		1);

	const CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&defaultHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(mBrdfLutTexture.GetAddressOf())));
	mBrdfLutTexture->SetName(L"EnvironmentBRDFLut");

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mBrdfLutTexture.Get(), 0, 1);
	const CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
	const CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(mBrdfLutUploadBuffer.GetAddressOf())));
	mBrdfLutUploadBuffer->SetName(L"EnvironmentBRDFLutUpload");

	D3D12_SUBRESOURCE_DATA subresourceData = {};
	subresourceData.pData = lutData.data();
	subresourceData.RowPitch = static_cast<LONG_PTR>(kBrdfLutSize * sizeof(DirectX::XMFLOAT2));
	subresourceData.SlicePitch = subresourceData.RowPitch * kBrdfLutSize;

	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		mBrdfLutTexture.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &barrier);
	UpdateSubresources(cmdList, mBrdfLutTexture.Get(), mBrdfLutUploadBuffer.Get(), 0, 0, 1, &subresourceData);
	barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		mBrdfLutTexture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->ResourceBarrier(1, &barrier);
}

void D3DWindow::BuildEnvironmentLightingDescriptors()
{
	if (!EnvironmentIblDescriptorsInitialized ||
		EnvironmentIblHeapStartIndex == UINT(-1) ||
		SrvDescriptorHeap == nullptr)
	{
		return;
	}

	ID3D12Resource* skyResource = nullptr;
	for (auto& textureGroupPair : TextureGroups)
	{
		for (Texture& texture : textureGroupPair.second)
		{
			if (texture.GetIndex() == SkyTexHeapIndex)
			{
				skyResource = texture.GetResource();
				break;
			}
		}

		if (skyResource != nullptr)
			break;
	}

	if (skyResource == nullptr)
	{
		const auto skyGroupIt = TextureGroups.find(L"skyMap");
		if (skyGroupIt != TextureGroups.end() &&
			!skyGroupIt->second.empty())
		{
			skyResource = skyGroupIt->second[0].GetResource();
		}
	}

	const auto writeTexture2DSrv = [this](UINT descriptorOffset, ID3D12Resource* resource)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		if (resource != nullptr)
		{
			const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
			srvDesc.Format = resourceDesc.Format;
			srvDesc.Texture2D.MipLevels = resourceDesc.MipLevels;
		}
		else
		{
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srvDesc.Texture2D.MipLevels = 1;
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
			SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			EnvironmentIblHeapStartIndex + descriptorOffset,
			CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(resource, &srvDesc, srvHandle);
	};

	// 当前阶段：irradiance/specular prefilter 先回退到当前天空，BRDF LUT 使用程序生成资源。
	writeTexture2DSrv(0, skyResource);
	writeTexture2DSrv(1, skyResource);
	writeTexture2DSrv(2, mBrdfLutTexture.Get());

	environmentIblDescriptorTable = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	environmentIblDescriptorTable.Offset(EnvironmentIblHeapStartIndex, CbvSrvUavDescriptorSize);
}

void D3DWindow::BuildPostProcessSceneColorDescriptors()
{
	if (!PostProcessSceneColorDescriptorsInitialized)
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

void D3DWindow::BuildColorAdjustSceneColorDescriptors()
{
	if (!ColorAdjustSceneColorDescriptorsInitialized)
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
		ColorAdjustSceneColorHeapStartIndex,
		CbvSrvUavDescriptorSize);

	for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
	{
		auto sceneColorResource = mFrameResources[frameIndex].mColorAdjustSceneColor.Get();
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

		CD3DX12_CPU_DESCRIPTOR_HANDLE targetHandle = outlineMaskSrvCpuHandle;
		targetHandle.Offset(frameIndex, CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(outlineMaskResource, &outlineMaskSrvDesc, targetHandle);
	}
}

void D3DWindow::BuildSharedSceneInputDescriptors()
{
	if (!SharedSceneInputDescriptorsInitialized)
		return;
	sharedNormalPrepass.BuildSceneInputDescriptors(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		SharedSceneInputHeapStartIndex,
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
		SrvDescriptorHeapIndex,
		true);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"skyMap"].resize(1);
	TextureGroups[L"skyMap"][0] = skyCubeTex;
	SkyTexHeapIndex = skyCubeTex.GetIndex();

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
bool D3DWindow::SetGeometryVertexColor(const std::wstring& name, const DirectX::XMFLOAT4& color) { return D3DWindowGeometryProvider::SetGeometryVertexColor(this, name, color); }

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
	const UINT requiredPassCount = 1u + ShadowConfig.MaxShadowMapCount * 6u + MaxRenderToTextureCount;

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

bool D3DWindow::SetMaterialDiffuseRenderToTexture(const std::wstring& materialName, std::uint32_t renderToTextureId)
{
	auto materialIt = Materials.find(materialName);
	if (materialIt == Materials.end())
		return false;

	Material& material = materialIt->second;
	material.DiffuseRenderToTextureId = renderToTextureId;
	material.Properties.UseDiffuseTexture = renderToTextureId != 0 || material.DiffuseTexture != nullptr ? 1u : 0u;
	material.NumFramesDirty = SwapChainBufferCount;
	FreshenMaterialCBs();
	return true;
}

bool D3DWindow::SetMaterialReflection(
	const std::wstring& materialName,
	bool enableReflection,
	MaterialReflectionSource source,
	std::uint32_t renderToTextureId)
{
	auto materialIt = Materials.find(materialName);
	if (materialIt == Materials.end())
		return false;

	Material& material = materialIt->second;
	material.EnableReflection = enableReflection;
	material.ReflectionSource = source;
	material.ReflectionRenderToTextureId =
		source == MaterialReflectionSource::RenderToTexture ? renderToTextureId : 0u;
	material.Properties.UseSpecularTexture = enableReflection ? 1u : 0u;
	material.Properties.ReflectionSource =
		enableReflection && source == MaterialReflectionSource::RenderToTexture && renderToTextureId != 0
		? 1u
		: 0u;
	material.NumFramesDirty = SwapChainBufferCount;
	FreshenMaterialCBs();
	return true;
}

void D3DWindow::SetEntityReflectionReceiverRenderToTexture(SceneEntityBase* entity, std::uint32_t renderToTextureId)
{
	if (entity == nullptr)
		return;

	std::unordered_set<SceneEntityBase*> targetEntities;
	std::vector<SceneEntityBase*> pendingEntities;
	pendingEntities.push_back(entity);
	while (!pendingEntities.empty())
	{
		SceneEntityBase* currentEntity = pendingEntities.back();
		pendingEntities.pop_back();
		if (currentEntity == nullptr || !targetEntities.insert(currentEntity).second)
			continue;

		if (mLastExternalECS != nullptr)
		{
			for (SceneEntityBase* childEntity : mLastExternalECS->GetSceneChildren(currentEntity))
				pendingEntities.push_back(childEntity);
		}
	}

	for (auto& renderItemPair : AllRitems)
	{
		RenderItem& renderItem = renderItemPair.second;
		if (targetEntities.find(renderItem.SourceEntity) != targetEntities.end() ||
			targetEntities.find(renderItem.CullingSourceEntity) != targetEntities.end())
		{
			renderItem.ReflectionReceiverRenderToTextureId = renderToTextureId;
		}
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
		descriptorIndex,
		true);

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
	BuildEnvironmentLightingDescriptors();
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
	workerCommandList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::PassCB, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
	workerCommandList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::LightCB, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
	MainScenePassContext workerSceneBinding = BuildMainScenePassContext();
	BindSceneSrvDescriptorTables(workerCommandList, workerSceneBinding);
	workerCommandList->RSSetViewports(1, &m_viewport);
	workerCommandList->RSSetScissorRects(1, &m_scissorRect);
}

MainScenePassContext D3DWindow::BuildMainScenePassContext(
	ID3D12DescriptorHeap* const* descriptorHeaps,
	UINT descriptorHeapCount)
{
	MainScenePassContext bindingState = {};
	bindingState.DescriptorHeaps = descriptorHeaps;
	bindingState.DescriptorHeapCount = descriptorHeapCount;
	bindingState.PassCBAddress = CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress();
	bindingState.LightCBAddress = CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress();
	bindingState.AmbientOcclusionDescriptor = ambientOcclusionDescriptor;
	bindingState.DirectionalShadowMaskDescriptor = directionalShadowMaskDescriptor;
	bindingState.ReflectionDescriptor = otherTexDescriptor;
	return bindingState;
}

void D3DWindow::BindSceneSrvDescriptorTables(
	ID3D12GraphicsCommandList* cmdList,
	const MainScenePassContext& bindingState)
{
	cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::SkyTexTable, skyTexDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::OtherTexTable, otherTexDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::Shadow2DTable, shadow2DDescriptorTable);
	cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::PointLightShadowCubeTable, pointLightShadowCubeDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::AmbientOcclusionTable, bindingState.AmbientOcclusionDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::DirectionalShadowMaskTable, bindingState.DirectionalShadowMaskDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::ReflectionTable, bindingState.ReflectionDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::EnvironmentIblTable, environmentIblDescriptorTable);
}

void D3DWindow::BindMainScenePassCommonState(
	ID3D12GraphicsCommandList* cmdList,
	const MainScenePassContext& bindingState,
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
	cmdList->SetGraphicsRootSignature(RootSignature.Get());
	if (bindingState.DescriptorHeaps != nullptr && bindingState.DescriptorHeapCount > 0)
		cmdList->SetDescriptorHeaps(bindingState.DescriptorHeapCount, bindingState.DescriptorHeaps);
	cmdList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::PassCB, bindingState.PassCBAddress);
	cmdList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::LightCB, bindingState.LightCBAddress);
	BindSceneSrvDescriptorTables(cmdList, bindingState);
	cmdList->RSSetViewports(1, &m_viewport);
	cmdList->RSSetScissorRects(1, &m_scissorRect);
	cmdList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
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
		if (shadowSlotIndex >= WorkingShadowMapStates.size())
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
		if (cubeIndex >= WorkingPointLightShadowCubeStates.size())
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
	MainScenePassContext shadowSceneBinding = BuildMainScenePassContext();
	BindSceneSrvDescriptorTables(workerCommandList, shadowSceneBinding);

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

void D3DWindow::RenderCameraRequestsToRenderTextures(ID3D12GraphicsCommandList* cmdList)
{
	if (mLastExternalECS == nullptr)
		return;

	const std::vector<CameraRenderRequest> cameraRequests =
		mLastExternalECS->BuildCameraRenderRequests();
	std::unordered_set<std::uint32_t> processedOutputTargetIds;
	processedOutputTargetIds.reserve(cameraRequests.size());

	for (const CameraRenderRequest& request : cameraRequests)
	{
		if (!request.renderEnabled || !request.renderToTextureEnabled || request.outputTargetId == 0)
			continue;
		if (!processedOutputTargetIds.insert(request.outputTargetId).second)
			continue;

		RenderToTexture* renderToTexture = FindRenderToTexture(request.outputTargetId);
		if (renderToTexture == nullptr)
			continue;
		const CameraRenderRequest mirrorRequest =
			BuildRenderToTextureMirrorCameraRequest(request, *renderToTexture);

		UINT slotIndex = UINT(-1);
		if (!TryGetRenderToTextureSlotIndex(request.outputTargetId, &slotIndex))
			continue;

		RenderOpaqueItemsToRenderTexture(
			cmdList,
			mirrorRequest,
			renderToTexture,
			GetRenderToTexturePassCBIndex(slotIndex));
	}
}

void D3DWindow::RenderOpaqueItemsToRenderTexture(
	ID3D12GraphicsCommandList* cmdList,
	const CameraRenderRequest& request,
	RenderToTexture* renderToTexture,
	UINT passCBIndex)
{
	renderToTexture->TransitionColor(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
	renderToTexture->TransitionDepth(cmdList, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderToTexture->GetRtv();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderToTexture->GetDsv();
	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &rtv, true, &dsv);

	ID3D12DescriptorHeap* descriptorHeaps[] = { SrvDescriptorHeap.Get() };
	cmdList->SetGraphicsRootSignature(RootSignature.Get());
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	const UINT passCBByteSize = CalculateConstantBufferByteSize(sizeof(PassConstants));
	const D3D12_GPU_VIRTUAL_ADDRESS passCBAddress =
		CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress() +
		static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(passCBIndex) * passCBByteSize;
	const DefaultDescriptorCatalog defaultDescriptorCatalog = BuildDefaultDescriptorCatalog();
	const CD3DX12_GPU_DESCRIPTOR_HANDLE renderToTextureFallbackDescriptor =
		defaultDescriptorCatalog.RenderToTextureFallbackDescriptor;

	const auto isReflectionReceiverForCurrentTarget = [&request](const RenderItem* renderItem)
	{
		// 只跳过绑定当前 RTT 的镜面接收物体本身。
		// 不按材质跳过，否则多个对象共享同一个镜面材质时会把整批物体都排除出 RTT。
		return renderItem != nullptr &&
			renderItem->ReflectionReceiverRenderToTextureId != 0 &&
			renderItem->ReflectionReceiverRenderToTextureId == request.outputTargetId;
	};

	std::vector<RenderItem*> opaqueRenderItems;
	opaqueRenderItems.reserve(RitemLayer[不透明物体渲染项目].size());
	for (RenderItem* renderItem : CollectRenderItems(RitemLayer[不透明物体渲染项目]))
	{
		if (renderItem == nullptr ||
			renderItem->Obj == nullptr ||
			renderItem->Obj->Material == nullptr ||
			isReflectionReceiverForCurrentTarget(renderItem))
		{
			continue;
		}
		opaqueRenderItems.push_back(renderItem);
	}

	std::vector<RenderItem*> transparentNearOpaqueRenderItems;
	transparentNearOpaqueRenderItems.reserve(RitemLayer[透明物体渲染项目].size());
	for (RenderItem* renderItem : CollectRenderItems(RitemLayer[透明物体渲染项目]))
	{
		if (renderItem == nullptr ||
			renderItem->Obj == nullptr ||
			renderItem->Obj->Material == nullptr ||
			isReflectionReceiverForCurrentTarget(renderItem))
		{
			continue;
		}
		transparentNearOpaqueRenderItems.push_back(renderItem);
	}

	cmdList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::PassCB, passCBAddress);
	cmdList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::LightCB, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
	// RenderToTexture 当前在主场景 shadow/AO/mask 更新前执行。
	// 这里不要采样本帧尚未稳定的 AO 与方向光阴影 mask，避免离屏 pass 和主 pass 之间产生资源状态依赖。
	MainScenePassContext renderToTextureSceneBinding = BuildMainScenePassContext();
	renderToTextureSceneBinding.AmbientOcclusionDescriptor = renderToTextureFallbackDescriptor;
	renderToTextureSceneBinding.DirectionalShadowMaskDescriptor = renderToTextureFallbackDescriptor;
	renderToTextureSceneBinding.ReflectionDescriptor = renderToTextureFallbackDescriptor;
	BindSceneSrvDescriptorTables(cmdList, renderToTextureSceneBinding);
	cmdList->RSSetViewports(1, &renderToTexture->GetViewport());
	cmdList->RSSetScissorRects(1, &renderToTexture->GetScissorRect());

	bool previousCullingReferenceLocked = false;
	DirectX::XMFLOAT4X4 previousLockedCullingView = MathHelps::Identity;
	DirectX::XMFLOAT4X4 previousLockedCullingProj = MathHelps::Identity;
	const std::uint32_t previousActiveRenderToTextureTargetId = mActiveRenderToTextureTargetId;
	const bool previousRenderingRenderToTexturePass = mRenderingRenderToTexturePass;
	mActiveRenderToTextureTargetId = request.outputTargetId;
	mRenderingRenderToTexturePass = true;
	{
		std::lock_guard<std::mutex> lock(mFrustumCullingReferenceMutex);
		previousCullingReferenceLocked = mFrustumCullingReferenceLocked;
		previousLockedCullingView = mLockedCullingView;
		previousLockedCullingProj = mLockedCullingProj;
		mLockedCullingView = request.view;
		DirectX::XMStoreFloat4x4(
			&mLockedCullingProj,
			BuildRenderToTextureProjectionMatrix(request, *renderToTexture));
		mFrustumCullingReferenceLocked = true;
	}

	DrawRenderItems(
		cmdList,
		opaqueRenderItems,
		PipelineState[不透明物体管道],
		不透明物体管道);
	DrawRenderItems(
		cmdList,
		transparentNearOpaqueRenderItems,
		PipelineState[半透明物体管道],
		半透明物体管道);

	{
		std::lock_guard<std::mutex> lock(mFrustumCullingReferenceMutex);
		mFrustumCullingReferenceLocked = previousCullingReferenceLocked;
		mLockedCullingView = previousLockedCullingView;
		mLockedCullingProj = previousLockedCullingProj;
	}

	mActiveRenderToTextureTargetId = previousActiveRenderToTextureTargetId;
	mRenderingRenderToTexturePass = previousRenderingRenderToTexturePass;
	// 结束 RTT pass 前先解除 OM 上的 RTV/DSV 绑定，再只把 color 切回可采样状态。
	// 当前 RenderToTexture 的 depth 只作为本 pass 的 DSV 使用，并没有对外暴露 depth SRV；
	// 因此不要把 depth 长期切到 DEPTH_READ，避免后续帧继续作为 DSV 使用时产生无意义的读写状态切换。
	cmdList->OMSetRenderTargets(0, nullptr, false, nullptr);
	renderToTexture->TransitionColor(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

			DirectX::XMMATRIX WorldTransform = DirectX::XMLoadFloat4x4(&renderItem.WorldTransform);
			DirectX::XMMATRIX texTransform = DirectX::XMLoadFloat4x4(&renderItem.TexTransform);

			ObjectConstants objConstants{};
			DirectX::XMStoreFloat4x4(&objConstants.WorldTransform, DirectX::XMMatrixTranspose(WorldTransform));
			objConstants.WorldInvTranspose = BuildWorldInverseTransposeMatrixFromWorldTransform(renderItem.WorldTransform);
			DirectX::XMStoreFloat4x4(&objConstants.TexTransform, DirectX::XMMatrixTranspose(texTransform));
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
	auto currSkinningCB = CurrFrameResource->SkinningCB.get();
	if (currSkinningCB == nullptr || mLastExternalECS == nullptr)
		return;

	SkinningConstants skinningConstants = {};

	for (auto& renderItemPair : AllRitems)
	{
		RenderItem& renderItem = renderItemPair.second;

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

		ResetSkinningConstantsToIdentity(&skinningConstants);
		if (runtimeComponent == nullptr)
		{
			currSkinningCB->CopyData(renderItem.SkinningCBIndex, skinningConstants);
			continue;
		}

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
		projectDir,
		projectDir.parent_path(),
		currentDir,
		currentDir / L"WitchcraftEngine"
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

	SkinnedMeshComponent* skinnedMeshComponent =
		mLastExternalECS->GetComponent<SkinnedMeshComponent>(renderItem.SourceEntity);

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
		const bool usesRenderToTextureReflection =
			Mat->EnableReflection &&
			Mat->ReflectionSource == MaterialReflectionSource::RenderToTexture &&
			Mat->ReflectionRenderToTextureId != 0;
		if ((Mat->NumFramesDirty > 0 || usesRenderToTextureReflection) && Mat->MatCBIndex!=-1)
		{
			MaterialConstants materialConstants = Mat->Properties;
			if (usesRenderToTextureReflection)
			{
				const auto viewProjTexIt = mRenderToTextureViewProjTexById.find(Mat->ReflectionRenderToTextureId);
				if (viewProjTexIt != mRenderToTextureViewProjTexById.end())
				{
					materialConstants.ReflectionViewProjTex = viewProjTexIt->second;
				}
			}

			currMaterialCB->CopyData(Mat->MatCBIndex, materialConstants);

			if (Mat->NumFramesDirty > 0)
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
	DirectX::XMFLOAT4X4 skyTexTransform = MathHelps::Identity;
	DirectX::XMFLOAT4X4 skyIblTexTransform = MathHelps::Identity;
	const auto& skyRenderItems = RitemLayer[天空渲染项目];
	for (const auto& skyRenderItemPair : skyRenderItems)
	{
		const RenderItem* skyRenderItem = skyRenderItemPair.second;
		if (skyRenderItem == nullptr)
			continue;

		if (!IsFiniteRenderMatrix(skyRenderItem->TexTransform))
			continue;

		skyTexTransform = skyRenderItem->TexTransform;
		break;
	}
	{
		// 天空材质的 UV 平移/缩放不能直接作用于 IBL 方向；只提取旋转部分。
		const DirectX::XMMATRIX skyTexMatrix = DirectX::XMLoadFloat4x4(&skyTexTransform);
		DirectX::XMVECTOR scale;
		DirectX::XMVECTOR rotation;
		DirectX::XMVECTOR translation;
		if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, skyTexMatrix))
			DirectX::XMStoreFloat4x4(&skyIblTexTransform, DirectX::XMMatrixRotationQuaternion(rotation));
		else
			skyIblTexTransform = MathHelps::Identity;
	}

	XMStoreFloat4x4(&MainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&MainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&MainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&MainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&MainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&MainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&MainPassCB.ViewProjTex, XMMatrixTranspose(viewProjTex));
	XMStoreFloat4x4(&MainPassCB.SkyTexTransform, XMMatrixTranspose(XMLoadFloat4x4(&skyTexTransform)));
	XMStoreFloat4x4(&MainPassCB.SkyIblTexTransform, XMMatrixTranspose(XMLoadFloat4x4(&skyIblTexTransform)));
	MainPassCB.EnvironmentLightingSettings.x = std::clamp(MainPassCB.EnvironmentLightingSettings.x, 0.0f, 4.0f);
	MainPassCB.EnvironmentLightingSettings.y = std::clamp(MainPassCB.EnvironmentLightingSettings.y, 0.0f, 4.0f);
	MainPassCB.EnvironmentLightingSettings.z = MainPassCB.EnvironmentLightingSettings.z > 0.5f ? 1.0f : 0.0f;
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
	MainPassCB.RenderTargetSize = DirectX::XMFLOAT2(
		(float)WinInfo.Width,
		(float)WinInfo.Height);
	MainPassCB.AOSettings = DirectX::XMFLOAT2(
		AOConfig.Enabled ? 1.0f : 0.0f,
		std::clamp(AOConfig.Strength, 0.0f, 1.0f));
	MainPassCB.ShadowMaskSettings = DirectX::XMFLOAT4(
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

UINT D3DWindow::ResolveDefaultSkyTextureHeapIndex()
{
	const auto defaultSkyGroupIt = TextureGroups.find(L"skyMap");
	const auto defaultDiffuseGroupIt = TextureGroups.find(L"Diffuse");
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
		defaultSkyTexHeapIndex = defaultSkyGroupIt->second[0].GetIndex();
	else if (hasDefaultDiffuseTexture)
		defaultSkyTexHeapIndex = defaultDiffuseGroupIt->second[0].GetIndex();

	if (defaultSkyTexHeapIndex >= SrvDescriptorHeapCapacity)
		defaultSkyTexHeapIndex = (NullTextureHeapIndex < SrvDescriptorHeapCapacity) ? NullTextureHeapIndex : 0u;

	return defaultSkyTexHeapIndex;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE D3DWindow::GetGpuSrvHandle(UINT heapIndex) const
{
	CD3DX12_GPU_DESCRIPTOR_HANDLE handle(SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	handle.Offset(heapIndex, CbvSrvUavDescriptorSize);
	return handle;
}

DefaultDescriptorCatalog D3DWindow::BuildDefaultDescriptorCatalog()
{
	DefaultDescriptorCatalog catalog;
	catalog.SkyTexHeapIndex = ResolveDefaultSkyTextureHeapIndex();
	catalog.SkyTexDescriptor = GetGpuSrvHandle(catalog.SkyTexHeapIndex);
	catalog.OtherTexDescriptor = GetGpuSrvHandle(0);
	catalog.EnvironmentIblDescriptorTable = GetGpuSrvHandle(EnvironmentIblHeapStartIndex);

	CD3DX12_GPU_DESCRIPTOR_HANDLE renderToTextureFallbackDescriptor(SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	if (NullTextureHeapIndex < SrvDescriptorHeapCapacity)
		renderToTextureFallbackDescriptor.Offset(NullTextureHeapIndex, CbvSrvUavDescriptorSize);
	else
		renderToTextureFallbackDescriptor = catalog.OtherTexDescriptor;
	catalog.RenderToTextureFallbackDescriptor = renderToTextureFallbackDescriptor;

	return catalog;
}

void D3DWindow::UpdateFrameDescriptors()
{
	DefaultDescriptorCatalog defaultDescriptorCatalog = BuildDefaultDescriptorCatalog();
	const bool hasSkyRenderItems = !RitemLayer[天空渲染项目].empty();
	if (!hasSkyRenderItems)
		SkyTexHeapIndex = defaultDescriptorCatalog.SkyTexHeapIndex;
	else if (SkyTexHeapIndex >= SrvDescriptorHeapCapacity)
		SkyTexHeapIndex = defaultDescriptorCatalog.SkyTexHeapIndex;

	if (SkyTexHeapIndex >= SrvDescriptorHeapCapacity)
		SkyTexHeapIndex = (NullTextureHeapIndex < SrvDescriptorHeapCapacity) ? NullTextureHeapIndex : 0u;

	SkyMapIndex = SkyTexHeapIndex;

	// 统一缓存本帧会用到的 SRV 描述符表起点，Render 阶段直接复用。
	skyTexDescriptor = GetGpuSrvHandle(SkyTexHeapIndex);
	environmentIblDescriptorTable = defaultDescriptorCatalog.EnvironmentIblDescriptorTable;
	otherTexDescriptor = defaultDescriptorCatalog.OtherTexDescriptor;

	shadow2DDescriptorTable = GetGpuSrvHandle(ShadowMapHeapStartIndex);
	pointLightShadowCubeDescriptor = GetGpuSrvHandle(ShadowMapHeapStartIndex + ShadowPoolLimits::Combined2DTextureCount);
	ambientOcclusionDescriptor = ambientOcclusion.mhAmbientMap0GpuSrv;
	directionalShadowMaskDescriptor = mDirectionalShadowMaskPass.GetMaskSrv();

	postProcessSceneColorDescriptor = GetGpuSrvHandle(PostProcessSceneColorHeapStartIndex + CurrBackBufferIndex);
	colorAdjustSceneColorDescriptor = GetGpuSrvHandle(ColorAdjustSceneColorHeapStartIndex + CurrBackBufferIndex);
	interactionOutlineMaskDescriptor = GetGpuSrvHandle(InteractionOutlineMaskHeapStartIndex + CurrBackBufferIndex);
	transparentOitAccumDescriptor = GetGpuSrvHandle(TransparentOitHeapStartIndex + CurrBackBufferIndex * 2);

	transparentOitRevealDescriptor = transparentOitAccumDescriptor;
	transparentOitRevealDescriptor.Offset(1, CbvSrvUavDescriptorSize);
}

void D3DWindow::UpdateFrameStateForRender()
{
	UpdateObjectCBs();
	UpdateSkinningCBs();
	UpdateSkinnedDeformationCaches();
	SyncRenderToTextureTargetsFromCameraRequests();
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

void D3DWindow::SyncRenderToTextureTargetsFromCameraRequests()
{
	if (mLastExternalECS == nullptr || !RenderToTextureDescriptorsReserved)
		return;

	mRenderToTextureViewProjTexById.clear();

	const std::vector<CameraRenderRequest> cameraRequests =
		mLastExternalECS->BuildCameraRenderRequests();
	std::unordered_set<std::uint32_t> processedOutputTargetIds;
	processedOutputTargetIds.reserve(cameraRequests.size());

	for (const CameraRenderRequest& request : cameraRequests)
	{
		if (!request.renderEnabled || !request.renderToTextureEnabled || request.outputTargetId == 0)
			continue;

		if (!processedOutputTargetIds.insert(request.outputTargetId).second)
			continue;

		RenderToTextureDesc desc;
		desc.Id = request.outputTargetId;
		desc.Width = DefaultRenderToTextureWidth;
		desc.Height = DefaultRenderToTextureHeight;
		desc.ColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.DepthFormat = DepthStencilFormat;
		desc.HasDepth = true;
		desc.AutoResizeWithViewport = false;
		const std::wstring cameraName =
			request.entity != nullptr ? request.entity->GetName() : L"UnknownCamera";
		desc.DebugName =
			L"RTT_id=" + std::to_wstring(request.outputTargetId) +
			L"_camera=" + cameraName +
			L"_entity=" + std::to_wstring(request.entityId);

		RenderToTexture* renderToTexture = EnsureRenderToTexture(desc);
		if (renderToTexture == nullptr)
			continue;

		UINT slotIndex = UINT(-1);
		if (!TryGetRenderToTextureSlotIndex(request.outputTargetId, &slotIndex))
			continue;

		const CameraRenderRequest mirrorRequest =
			BuildRenderToTextureMirrorCameraRequest(request, *renderToTexture);
		const UINT passCBIndex = GetRenderToTexturePassCBIndex(slotIndex);
		const PassConstants renderToTexturePassCB =
			BuildRenderToTexturePassConstants(mirrorRequest, *renderToTexture);
		DirectX::XMFLOAT4X4 reflectionViewProjTex = MathHelps::Identity;
		DirectX::XMStoreFloat4x4(
			&reflectionViewProjTex,
			DirectX::XMMatrixTranspose(BuildRenderToTextureReflectionViewProjTexMatrix(mirrorRequest, *renderToTexture)));
		mRenderToTextureViewProjTexById[request.outputTargetId] = reflectionViewProjTex;
		for (UINT frameIndex = 0; frameIndex < SwapChainBufferCount; ++frameIndex)
		{
			auto passCB = mFrameResources[frameIndex].PassCB.get();
			if (passCB != nullptr)
				passCB->CopyData(passCBIndex, renderToTexturePassCB);
		}
	}
}

DirectX::XMMATRIX D3DWindow::BuildRenderToTextureProjectionMatrix(
	const CameraRenderRequest& request,
	const RenderToTexture& renderToTexture) const
{
	const RenderToTextureDesc& desc = renderToTexture.GetDesc();
	const float width = static_cast<float>((std::max)(desc.Width, 1u));
	const float height = static_cast<float>((std::max)(desc.Height, 1u));
	const float outputAspect = width / height;
	const float safeFovY = std::clamp(request.fovY, 0.1f, MaxRenderToTextureReflectionFovY);
	// RenderToTexture 相机常用于贴近镜面/监视器表面的位置。
	// 如果沿用相机组件默认 nearZ=1.0，靠近相机的物体会被离屏 pass 直接裁掉；
	// 这里仅对 RTT 输出压小 near plane，不改变 ECS 相机组件本身的用户参数。
	const float safeNearZ = std::clamp(request.nearZ, 0.001f, 0.01f);
	const float safeFarZ = request.farZ <= safeNearZ ? safeNearZ + 0.01f : request.farZ;
	return DirectX::XMMatrixPerspectiveFovLH(safeFovY, outputAspect, safeNearZ, safeFarZ);
}

bool D3DWindow::TryResolveRenderToTextureMirrorPlane(
	std::uint32_t renderToTextureId,
	DirectX::XMVECTOR* outPoint,
	DirectX::XMVECTOR* outNormal,
	DirectX::XMVECTOR* outTangentUp) const
{
	if (renderToTextureId == 0 || outPoint == nullptr || outNormal == nullptr || outTangentUp == nullptr)
		return false;

	for (const auto& renderItemPair : AllRitems)
	{
		const RenderItem& renderItem = renderItemPair.second;
		if (renderItem.ReflectionReceiverRenderToTextureId != renderToTextureId ||
			!renderItem.HasLocalBounds)
		{
			continue;
		}

		const DirectX::XMFLOAT3& extents = renderItem.LocalBounds.Extents;
		const float axisExtents[3] = { extents.x, extents.y, extents.z };
		int thicknessAxis = 0;
		if (axisExtents[1] < axisExtents[thicknessAxis])
			thicknessAxis = 1;
		if (axisExtents[2] < axisExtents[thicknessAxis])
			thicknessAxis = 2;

		int tangentAxisA = -1;
		int tangentAxisB = -1;
		for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
		{
			if (axisIndex == thicknessAxis)
				continue;
			if (tangentAxisA < 0)
				tangentAxisA = axisIndex;
			else
				tangentAxisB = axisIndex;
		}
		if (tangentAxisA < 0 || tangentAxisB < 0)
			return false;

		DirectX::XMVECTOR localNormal = DirectX::XMVectorZero();
		switch (thicknessAxis)
		{
		case 0:
			localNormal = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
			break;
		case 1:
			localNormal = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			break;
		default:
			localNormal = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
			break;
		}

		const auto makeLocalAxis = [](int axisIndex)
		{
			switch (axisIndex)
			{
			case 0:
				return DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
			case 1:
				return DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			default:
				return DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
			}
		};

		const DirectX::XMMATRIX world =
			DirectX::XMLoadFloat4x4(&renderItem.WorldTransform);
		const DirectX::XMVECTOR worldNormal =
			DirectX::XMVector3TransformNormal(localNormal, world);
		if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(worldNormal)) <= 1.0e-8f)
			return false;

		const DirectX::XMVECTOR worldTangentA =
			DirectX::XMVector3TransformNormal(makeLocalAxis(tangentAxisA), world);
		const DirectX::XMVECTOR worldTangentB =
			DirectX::XMVector3TransformNormal(makeLocalAxis(tangentAxisB), world);
		if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(worldTangentA)) <= 1.0e-8f ||
			DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(worldTangentB)) <= 1.0e-8f)
		{
			return false;
		}

		const DirectX::XMVECTOR receiverCenterLocal =
			DirectX::XMLoadFloat3(&renderItem.LocalBounds.Center);
		*outPoint = DirectX::XMVector3TransformCoord(receiverCenterLocal, world);
		*outNormal = DirectX::XMVector3Normalize(worldNormal);
		const DirectX::XMVECTOR normalizedTangentA =
			DirectX::XMVector3Normalize(worldTangentA);
		const DirectX::XMVECTOR normalizedTangentB =
			DirectX::XMVector3Normalize(worldTangentB);
		const float tangentADotWorldUp = std::abs(DirectX::XMVectorGetY(normalizedTangentA));
		const float tangentBDotWorldUp = std::abs(DirectX::XMVectorGetY(normalizedTangentB));
		*outTangentUp = tangentADotWorldUp >= tangentBDotWorldUp
			? normalizedTangentA
			: normalizedTangentB;
		return true;
	}

	return false;
}

CameraRenderRequest D3DWindow::BuildRenderToTextureMirrorCameraRequest(
	const CameraRenderRequest& request,
	const RenderToTexture& renderToTexture) const
{
	DirectX::XMVECTOR mirrorPoint = DirectX::XMVectorZero();
	DirectX::XMVECTOR mirrorNormal = DirectX::XMVectorZero();
	DirectX::XMVECTOR mirrorTangentUp = DirectX::XMVectorZero();
	if (!TryResolveRenderToTextureMirrorPlane(request.outputTargetId, &mirrorPoint, &mirrorNormal, &mirrorTangentUp))
		return request;

	const DirectX::XMFLOAT3 mainEyeData = mCamera.GetPosition3f();
	const DirectX::SimpleMath::Vector3 mainForwardData = mCamera.GetCamTarget();
	const DirectX::SimpleMath::Vector3 mainUpData = mCamera.GetCamUp();
	DirectX::XMVECTOR mainEye =
		DirectX::XMLoadFloat3(&mainEyeData);
	DirectX::XMVECTOR mainForward =
		DirectX::XMVector3Normalize(DirectX::XMVectorSet(mainForwardData.x, mainForwardData.y, mainForwardData.z, 0.0f));
	DirectX::XMVECTOR mainUp =
		DirectX::XMVector3Normalize(DirectX::XMVectorSet(mainUpData.x, mainUpData.y, mainUpData.z, 0.0f));
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(mainForward)) <= 1.0e-8f ||
		DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(mainUp)) <= 1.0e-8f)
	{
		return request;
	}
	DirectX::XMVECTOR mainRight = DirectX::XMVector3Cross(mainUp, mainForward);
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(mainRight)) <= 1.0e-8f)
		return request;
	mainRight = DirectX::XMVector3Normalize(mainRight);
	mainUp = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(mainForward, mainRight));

	DirectX::XMVECTOR baseEye =
		DirectX::XMLoadFloat3(&request.positionWS);
	DirectX::XMVECTOR baseForward =
		DirectX::XMLoadFloat3(&request.forwardWS);
	DirectX::XMVECTOR baseUp =
		DirectX::XMLoadFloat3(&request.upWS);
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(baseForward)) <= 1.0e-8f ||
		DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(baseUp)) <= 1.0e-8f)
	{
		return request;
	}
	baseForward = DirectX::XMVector3Normalize(baseForward);
	baseUp = DirectX::XMVector3Normalize(baseUp);
	DirectX::XMVECTOR baseRight = DirectX::XMVector3Cross(baseUp, baseForward);
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(baseRight)) <= 1.0e-8f)
		return request;
	baseRight = DirectX::XMVector3Normalize(baseRight);
	baseUp = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(baseForward, baseRight));

	const DirectX::XMVECTOR eyeToPlane =
		DirectX::XMVectorSubtract(mainEye, mirrorPoint);
	const float signedDistance =
		DirectX::XMVectorGetX(DirectX::XMVector3Dot(eyeToPlane, mirrorNormal));
	const float mainSideSign =
		std::abs(signedDistance) > 1.0e-4f
		? (signedDistance >= 0.0f ? 1.0f : -1.0f)
		: (DirectX::XMVectorGetX(DirectX::XMVector3Dot(mainForward, mirrorNormal)) <= 0.0f ? 1.0f : -1.0f);
	DirectX::XMVECTOR reflectedEye =
		DirectX::XMVectorSubtract(
			mainEye,
			DirectX::XMVectorScale(mirrorNormal, 2.0f * signedDistance));
	DirectX::XMVECTOR mirrorForward =
		DirectX::XMVector3Reflect(mainForward, mirrorNormal);
	DirectX::XMVECTOR mirrorUp =
		DirectX::XMVector3Reflect(mainUp, mirrorNormal);

	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(mirrorForward)) <= 1.0e-8f ||
		DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(mirrorUp)) <= 1.0e-8f)
	{
		return request;
	}

	mirrorForward = DirectX::XMVector3Normalize(mirrorForward);
	mirrorUp = DirectX::XMVector3Normalize(mirrorUp);
	DirectX::XMVECTOR mirrorRight = DirectX::XMVector3Cross(mirrorUp, mirrorForward);
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(mirrorRight)) <= 1.0e-8f)
		return request;
	mirrorRight = DirectX::XMVector3Normalize(mirrorRight);
	mirrorUp = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(mirrorForward, mirrorRight));

	const DirectX::XMVECTOR neutralMirrorForward =
		DirectX::XMVectorScale(mirrorNormal, mainSideSign);
	// neutral pose 是“用户未额外修正时”的镜面相机姿态。
	// RTT 相机实体不再重解释理想反射姿态，而是先相对这个 neutral pose 提取
	// 位置/朝向偏移，再把偏移应用到主相机的理想镜面反射姿态上。
	DirectX::XMVECTOR neutralMirrorRight =
		DirectX::XMVectorSubtract(
			mainRight,
			DirectX::XMVectorScale(
				neutralMirrorForward,
				DirectX::XMVectorGetX(DirectX::XMVector3Dot(mainRight, neutralMirrorForward))));
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(neutralMirrorRight)) <= 1.0e-8f)
	{
		neutralMirrorRight =
			DirectX::XMVector3Cross(
				mirrorTangentUp,
				neutralMirrorForward);
	}
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(neutralMirrorRight)) <= 1.0e-8f)
		return request;
	neutralMirrorRight = DirectX::XMVector3Normalize(neutralMirrorRight);
	if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(neutralMirrorRight, mainRight)) < 0.0f)
		neutralMirrorRight = DirectX::XMVectorNegate(neutralMirrorRight);
	DirectX::XMVECTOR neutralMirrorUp =
		DirectX::XMVector3Cross(neutralMirrorForward, neutralMirrorRight);
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(neutralMirrorUp)) <= 1.0e-8f)
	{
		neutralMirrorUp =
		DirectX::XMVectorSubtract(
			mirrorTangentUp,
			DirectX::XMVectorScale(
				neutralMirrorForward,
				DirectX::XMVectorGetX(DirectX::XMVector3Dot(mirrorTangentUp, neutralMirrorForward))));
	}
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(neutralMirrorUp)) <= 1.0e-8f)
		return request;
	neutralMirrorUp = DirectX::XMVector3Normalize(neutralMirrorUp);

	const DirectX::XMVECTOR idealRight = mirrorRight;
	const DirectX::XMVECTOR idealUp = mirrorUp;
	const DirectX::XMVECTOR idealForward = mirrorForward;

	const auto toNeutralLocal = [&](DirectX::XMVECTOR worldVector) -> DirectX::XMVECTOR
	{
		return DirectX::XMVectorSet(
			DirectX::XMVectorGetX(DirectX::XMVector3Dot(worldVector, neutralMirrorRight)),
			DirectX::XMVectorGetX(DirectX::XMVector3Dot(worldVector, neutralMirrorUp)),
			DirectX::XMVectorGetX(DirectX::XMVector3Dot(worldVector, neutralMirrorForward)),
			0.0f);
	};
	const auto fromIdealLocal = [&](DirectX::XMVECTOR localVector) -> DirectX::XMVECTOR
	{
		return DirectX::XMVectorAdd(
			DirectX::XMVectorAdd(
				DirectX::XMVectorScale(idealRight, DirectX::XMVectorGetX(localVector)),
				DirectX::XMVectorScale(idealUp, DirectX::XMVectorGetY(localVector))),
			DirectX::XMVectorScale(idealForward, DirectX::XMVectorGetZ(localVector)));
	};

	// RTT 相机实体只表示用户相对 neutral mirror camera 的偏移。
	// final = ideal reflected main camera + user offset。
	// 这样用户旋转 RTT 相机时是在理想镜面反射基础上修正角度，
	// 不会再把理想反射姿态放到 RTT 相机坐标系里重解释。
	const DirectX::XMVECTOR localPositionOffset =
		toNeutralLocal(DirectX::XMVectorSubtract(baseEye, mirrorPoint));
	const DirectX::XMVECTOR localForwardOffset =
		toNeutralLocal(baseForward);
	const DirectX::XMVECTOR localUpOffset =
		toNeutralLocal(baseUp);

	const DirectX::XMVECTOR finalTranslation =
		DirectX::XMVectorAdd(reflectedEye, fromIdealLocal(localPositionOffset));
	DirectX::XMVECTOR finalForward =
		fromIdealLocal(localForwardOffset);
	DirectX::XMVECTOR finalUp =
		fromIdealLocal(localUpOffset);

	CameraRenderRequest mirrorRequest = request;
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(finalForward)) <= 1.0e-8f ||
		DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(finalUp)) <= 1.0e-8f)
	{
		return request;
	}

	const DirectX::XMVECTOR normalizedFinalForward = DirectX::XMVector3Normalize(finalForward);
	const DirectX::XMVECTOR normalizedFinalUp = DirectX::XMVector3Normalize(finalUp);
	DirectX::XMStoreFloat3(&mirrorRequest.positionWS, finalTranslation);
	DirectX::XMStoreFloat3(&mirrorRequest.forwardWS, normalizedFinalForward);
	DirectX::XMStoreFloat3(&mirrorRequest.upWS, normalizedFinalUp);
	const DirectX::XMMATRIX finalView =
		DirectX::XMMatrixLookToLH(finalTranslation, normalizedFinalForward, normalizedFinalUp);
	const RenderToTextureDesc& desc = renderToTexture.GetDesc();
	const float outputAspect =
		static_cast<float>((std::max)(desc.Width, 1u)) /
		static_cast<float>((std::max)(desc.Height, 1u));
	float requiredHalfFovTan = std::tan(std::clamp(request.fovY, 0.1f, MaxRenderToTextureReflectionFovY) * 0.5f);
	for (const auto& renderItemPair : AllRitems)
	{
		const RenderItem& renderItem = renderItemPair.second;
		if (renderItem.ReflectionReceiverRenderToTextureId != request.outputTargetId ||
			!renderItem.HasLocalBounds)
		{
			continue;
		}

		const DirectX::XMFLOAT3& center = renderItem.LocalBounds.Center;
		const DirectX::XMFLOAT3& extents = renderItem.LocalBounds.Extents;
		const DirectX::XMMATRIX world =
			DirectX::XMLoadFloat4x4(&renderItem.WorldTransform);
		for (int xSign = -1; xSign <= 1; xSign += 2)
		{
			for (int ySign = -1; ySign <= 1; ySign += 2)
			{
				for (int zSign = -1; zSign <= 1; zSign += 2)
				{
					const DirectX::XMVECTOR cornerLocal = DirectX::XMVectorSet(
						center.x + extents.x * static_cast<float>(xSign),
						center.y + extents.y * static_cast<float>(ySign),
						center.z + extents.z * static_cast<float>(zSign),
						1.0f);
					const DirectX::XMVECTOR cornerView =
						DirectX::XMVector3TransformCoord(
							DirectX::XMVector3TransformCoord(cornerLocal, world),
							finalView);
					const float viewZ = DirectX::XMVectorGetZ(cornerView);
					if (viewZ <= 1.0e-4f)
						continue;

					const float viewX = DirectX::XMVectorGetX(cornerView);
					const float viewY = DirectX::XMVectorGetY(cornerView);
					requiredHalfFovTan = (std::max)(
						requiredHalfFovTan,
						(std::abs)(viewY / viewZ));
					requiredHalfFovTan = (std::max)(
						requiredHalfFovTan,
						(std::abs)(viewX / viewZ) / (std::max)(outputAspect, 0.001f));
				}
			}
		}
		break;
	}

	constexpr float kMirrorFovPadding = 1.08f;
	mirrorRequest.fovY = std::clamp(
		2.0f * std::atan(requiredHalfFovTan * kMirrorFovPadding),
		0.1f,
		MaxRenderToTextureReflectionFovY);
	const DirectX::XMMATRIX mirrorProj =
		BuildRenderToTextureProjectionMatrix(mirrorRequest, renderToTexture);
	const DirectX::XMMATRIX mirrorViewProj =
		DirectX::XMMatrixMultiply(finalView, mirrorProj);
	DirectX::XMStoreFloat4x4(&mirrorRequest.view, finalView);
	DirectX::XMStoreFloat4x4(&mirrorRequest.proj, mirrorProj);
	DirectX::XMStoreFloat4x4(&mirrorRequest.viewProj, mirrorViewProj);
	return mirrorRequest;
}

DirectX::XMMATRIX D3DWindow::BuildRenderToTextureReflectionViewProjTexMatrix(
	const CameraRenderRequest& request,
	const RenderToTexture& renderToTexture) const
{
	const DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&request.view);
	const DirectX::XMMATRIX proj = BuildRenderToTextureProjectionMatrix(request, renderToTexture);
	const DirectX::XMMATRIX texTransform(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	return DirectX::XMMatrixMultiply(DirectX::XMMatrixMultiply(view, proj), texTransform);
}

PassConstants D3DWindow::BuildRenderToTexturePassConstants(
	const CameraRenderRequest& request,
	const RenderToTexture& renderToTexture) const
{
	PassConstants constants = MainPassCB;

	const DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&request.view);
	const DirectX::XMMATRIX proj = BuildRenderToTextureProjectionMatrix(request, renderToTexture);
	const DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);
	DirectX::XMVECTOR determinantView = DirectX::XMMatrixDeterminant(view);
	const DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&determinantView, view);
	DirectX::XMVECTOR determinantProj = DirectX::XMMatrixDeterminant(proj);
	const DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(&determinantProj, proj);
	DirectX::XMVECTOR determinantViewProj = DirectX::XMMatrixDeterminant(viewProj);
	const DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(&determinantViewProj, viewProj);

	const DirectX::XMMATRIX texTransform(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	const DirectX::XMMATRIX viewProjTex = DirectX::XMMatrixMultiply(viewProj, texTransform);

	DirectX::XMStoreFloat4x4(&constants.View, DirectX::XMMatrixTranspose(view));
	DirectX::XMStoreFloat4x4(&constants.InvView, DirectX::XMMatrixTranspose(invView));
	DirectX::XMStoreFloat4x4(&constants.Proj, DirectX::XMMatrixTranspose(proj));
	DirectX::XMStoreFloat4x4(&constants.InvProj, DirectX::XMMatrixTranspose(invProj));
	DirectX::XMStoreFloat4x4(&constants.ViewProj, DirectX::XMMatrixTranspose(viewProj));
	DirectX::XMStoreFloat4x4(&constants.InvViewProj, DirectX::XMMatrixTranspose(invViewProj));
	DirectX::XMStoreFloat4x4(&constants.ViewProjTex, DirectX::XMMatrixTranspose(viewProjTex));
	constants.EyePosW = request.positionWS;
	constants.RenderTargetSize = DirectX::XMFLOAT2(
		static_cast<float>(renderToTexture.GetDesc().Width),
		static_cast<float>(renderToTexture.GetDesc().Height));

	return constants;
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
	CollectRenderFrameItemSnapshots();
	ResolveRenderFramePlanFlags();
}

void D3DWindow::CollectRenderFrameItemSnapshots()
{
	// 在正式录制本帧命令前，先生成一份“Render 侧快照”：
	// 这里只负责统一收集会被多个渲染阶段重复使用的 RenderItem 列表；
	// 让 RenderB / RenderE 消费同一份结果，避免同帧不同阶段各自临时收集，
	//    导致 AO、描边、debug、天空等通道看到的场景快照不一致。
	FrameSkyRenderItems = CollectRenderItems(RitemLayer[天空渲染项目]);
	FrameDebugRenderItems = CollectRenderItems(RitemLayer[debugrt]);
	FrameSelectedOutlineRenderItems = CollectSelectedRenderItems();
	FrameStaticShadowCasterRenderItems.clear();
	FrameDynamicShadowCasterRenderItems.clear();

	for (auto& renderItemPair : AllRitems)
	{
		const std::wstring& renderItemName = renderItemPair.first;
		RenderItem& renderItem = renderItemPair.second;

		bool isSkyRenderItem = RitemLayer[天空渲染项目].find(renderItemName) != RitemLayer[天空渲染项目].end();
		bool isDebugRenderItem = RitemLayer[debugrt].find(renderItemName) != RitemLayer[debugrt].end();
		if (isSkyRenderItem || isDebugRenderItem)
			continue;

		if (ShadowRuntimeHelpers::IsDynamicShadowSceneType(renderItem.SceneType))
			FrameDynamicShadowCasterRenderItems.push_back(&renderItem);
		else
			FrameStaticShadowCasterRenderItems.push_back(&renderItem);
	}
}

void D3DWindow::ResolveRenderFramePlanFlags()
{
	// 这里只负责根据当前帧已经收集好的列表，整理出本帧哪些 pass 需要参与。
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

void D3DWindow::RecordRenderTailPasses(
	ID3D12GraphicsCommandList* endCommandList,
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
	ID3D12DescriptorHeap* const* srvDescriptorHeaps,
	UINT srvHeapCount,
	bool useRecordedFramePlan,
	bool hasOpaqueRenderItems,
	bool hasTransparentRenderItems,
	bool hasAoRenderItems,
	bool hasShadowCasterRenderItems,
	bool hasOitResources,
	bool enableVolumetricLightPass,
	bool allowNoSkyPostProcessTail,
	bool allowNoSkyMainGeometrySubmission,
	bool useNoSkyMinimalUiTail,
	bool renderFPS,
	bool renderEditor)
{
	(void)srvHeapCount;
	(void)hasAoRenderItems;
	(void)hasShadowCasterRenderItems;
	(void)allowNoSkyMainGeometrySubmission;

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
	const bool hasSkyRenderItems = useRecordedFramePlan ?
		CurrentRenderFramePlan.HasSkyRenderItems :
		fallbackHasSkyRenderItems;
	(void)hasSkyRenderItems;
	const bool resolvedHasOpaqueRenderItems = useRecordedFramePlan ?
		hasOpaqueRenderItems :
		fallbackHasOpaqueRenderItems;
	const bool resolvedHasTransparentRenderItems = useRecordedFramePlan ?
		hasTransparentRenderItems :
		fallbackHasTransparentRenderItems;
	const bool resolvedHasAoRenderItems = useRecordedFramePlan ?
		hasAoRenderItems :
		fallbackHasAoRenderItems;
	const bool resolvedHasShadowCasterRenderItems = useRecordedFramePlan ?
		hasShadowCasterRenderItems :
		fallbackHasShadowCasterRenderItems;
	(void)resolvedHasAoRenderItems;
	(void)resolvedHasShadowCasterRenderItems;
	const bool resolvedHasOitResources = useRecordedFramePlan ?
		hasOitResources :
		fallbackHasOitResources;
	const bool resolvedHasMainSceneGeometry = resolvedHasOpaqueRenderItems || resolvedHasTransparentRenderItems;
	const bool resolvedEnableVolumetricLightPass =
		(resolvedHasMainSceneGeometry &&
			CurrFrameResource != nullptr &&
			MainPassCB.LightConst > 0u &&
			DepthStencilBuffer != nullptr &&
			SharedSceneInputDescriptorsInitialized) && enableVolumetricLightPass;

	if (useNoSkyMinimalUiTail)
	{
		endCommandList->RSSetViewports(1, &m_viewport);
		endCommandList->RSSetScissorRects(1, &m_scissorRect);
		endCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

		if (renderFPS)
		{
			endCommandList->SetPipelineState(PipelineState[文字管道].Get());
			D3DPassContext textContext = {};
			textContext.CommandList = endCommandList;
			textR->Draw(textContext, Text, DirectX::XMFLOAT2(0.32f, 0.25f), DirectX::XMFLOAT4{ 1.0f,1.0f,1.0f,1.0f }, CurrBackBufferIndex);
		}

		if (mEditor && renderEditor)
			mEditor->Render();

		endCommandList->OMSetRenderTargets(0, nullptr, false, nullptr);
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
	endCommandList->SetDescriptorHeaps(srvHeapCount, srvDescriptorHeaps);
	endCommandList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::PassCB, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
	endCommandList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::LightCB, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
	MainScenePassContext endSceneBinding = BuildMainScenePassContext();
	BindSceneSrvDescriptorTables(endCommandList, endSceneBinding);
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

	auto postCommandList = endCommandList;
	postCommandList->RSSetViewports(1, &m_viewport);
	postCommandList->RSSetScissorRects(1, &m_scissorRect);
	postCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	if (resolvedHasOitResources)
	{
		D3D12_RESOURCE_BARRIER oitToShaderReadBarriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(transparentOitAccumResource,
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(transparentOitRevealResource,
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		postCommandList->ResourceBarrier(_countof(oitToShaderReadBarriers), oitToShaderReadBarriers);

		auto sceneColorResource = CurrFrameResource->mPostProcessSceneColor.Get();
		if (sceneColorResource != nullptr)
		{
			postCommandList->OMSetRenderTargets(0, nullptr, false, nullptr);
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

			D3DPassContext oitCompositeContext = {};
			oitCompositeContext.CommandList = postCommandList;
			oitCompositeContext.DescriptorHeaps = srvDescriptorHeaps;
			oitCompositeContext.DescriptorHeapCount = srvHeapCount;
			oitCompositeContext.Viewport = m_viewport;
			oitCompositeContext.ScissorRect = m_scissorRect;
			oitCompositeContext.RtvHandle = rtvHandle;
			oitCompositeContext.DsvHandle = dsvHandle;
			oitCompositeContext.PostProcessCBAddress = CurrFrameResource->PostProcessCB->Resource()->GetGPUVirtualAddress();
			oitCompositeContext.SceneColorDescriptor = postProcessSceneColorDescriptor;
			oitCompositeContext.OitAccumDescriptor = transparentOitAccumDescriptor;

			mOITCompositePass.Draw(oitCompositeContext);

			postCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
		}
	}

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
			srvHeapCount,
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

	if (resolvedEnableVolumetricLightPass)
	{
		const D3D12_GPU_DESCRIPTOR_HANDLE volumetricSceneDepthDescriptor =
			sharedNormalPrepass.GetSceneInputDepthSrv(
				SharedSceneInputHeapStartIndex,
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

			postCommandList->OMSetRenderTargets(1, &rtvHandle, true, nullptr);
			TransitionTrackedResourceState(
				postCommandList,
				DepthStencilBuffer.Get(),
				DepthStencilBufferState,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

			D3DPassContext volumetricLightContext = {};
			volumetricLightContext.CommandList = postCommandList;
			volumetricLightContext.DescriptorHeaps = srvDescriptorHeaps;
			volumetricLightContext.DescriptorHeapCount = srvHeapCount;
			volumetricLightContext.PassCBAddress = CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress();
			volumetricLightContext.LightCBAddress = CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress();
			volumetricLightContext.NormalDepthDescriptor = volumetricSceneDepthDescriptor;
			volumetricLightContext.RtvHandle = rtvHandle;
			volumetricLightContext.DsvHandle = dsvHandle;

			volumetricLightPass.Draw(
				volumetricLightContext,
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
			postCommandList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::PointLightShadowCubeTable, pointLightShadowCubeDescriptor);
			postCommandList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::AmbientOcclusionTable, ambientOcclusionDescriptor);
			postCommandList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::DirectionalShadowMaskTable, directionalShadowMaskDescriptor);
			postCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
		}
	}

	const bool useFxaa = IsFXAAEnabled() &&
		CurrFrameResource->mColorAdjustSceneColor != nullptr;
	if (allowNoSkyPostProcessTail &&
		CurrFrameResource->PostProcessCB != nullptr &&
		CurrFrameResource->mPostProcessSceneColor != nullptr)
	{
		auto sceneColorResource = CurrFrameResource->mPostProcessSceneColor.Get();
		auto colorAdjustResource = useFxaa ? CurrFrameResource->mColorAdjustSceneColor.Get() : nullptr;
		CD3DX12_CPU_DESCRIPTOR_HANDLE colorAdjustSceneColorRtvHandle;
		if (useFxaa)
		{
			colorAdjustSceneColorRtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
				RtvHeap->GetCPUDescriptorHandleForHeapStart(),
				ColorAdjustSceneColorRtvStartIndex + CurrBackBufferIndex,
				RtvDescriptorSize);
		}

		postCommandList->OMSetRenderTargets(0, nullptr, false, nullptr);
		if (useFxaa)
		{
			D3D12_RESOURCE_BARRIER colorAdjustPreBarriers[3] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(sceneColorResource,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
				CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(colorAdjustResource,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
			};
			postCommandList->ResourceBarrier(_countof(colorAdjustPreBarriers), colorAdjustPreBarriers);
		}
		else
		{
			D3D12_RESOURCE_BARRIER colorAdjustPreBarriers[2] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(sceneColorResource,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
				CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE)
			};
			postCommandList->ResourceBarrier(_countof(colorAdjustPreBarriers), colorAdjustPreBarriers);
		}
		postCommandList->CopyResource(sceneColorResource, SwapChainBuffer[CurrBackBufferIndex].Get());

		D3D12_RESOURCE_BARRIER colorAdjustPostCopyBarriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
				D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
			CD3DX12_RESOURCE_BARRIER::Transition(sceneColorResource,
				D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
		};
		postCommandList->ResourceBarrier(_countof(colorAdjustPostCopyBarriers), colorAdjustPostCopyBarriers);

		D3DPassContext colorAdjustContext = {};
		colorAdjustContext.CommandList = postCommandList;
		colorAdjustContext.DescriptorHeaps = srvDescriptorHeaps;
		colorAdjustContext.DescriptorHeapCount = srvHeapCount;
		colorAdjustContext.Viewport = m_viewport;
		colorAdjustContext.ScissorRect = m_scissorRect;
		colorAdjustContext.RtvHandle = useFxaa ? colorAdjustSceneColorRtvHandle : rtvHandle;
		colorAdjustContext.DsvHandle = dsvHandle;
		colorAdjustContext.PostProcessCBAddress = CurrFrameResource->PostProcessCB->Resource()->GetGPUVirtualAddress();
		colorAdjustContext.SceneColorDescriptor = postProcessSceneColorDescriptor;

		mColorAdjustPass.Draw(colorAdjustContext);

		if (useFxaa)
		{
			postCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
			D3D12_RESOURCE_BARRIER colorAdjustPostBarriers =
				CD3DX12_RESOURCE_BARRIER::Transition(
					CurrFrameResource->mColorAdjustSceneColor.Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			postCommandList->ResourceBarrier(1, &colorAdjustPostBarriers);
		}
	}

	if (allowNoSkyPostProcessTail &&
		useFxaa &&
		CurrFrameResource->PostProcessCB != nullptr &&
		CurrFrameResource->mColorAdjustSceneColor != nullptr)
	{
		D3DPassContext fxaaContext = {};
		fxaaContext.CommandList = postCommandList;
		fxaaContext.DescriptorHeaps = srvDescriptorHeaps;
		fxaaContext.DescriptorHeapCount = srvHeapCount;
		fxaaContext.Viewport = m_viewport;
		fxaaContext.ScissorRect = m_scissorRect;
		fxaaContext.RtvHandle = rtvHandle;
		fxaaContext.DsvHandle = dsvHandle;
		fxaaContext.PostProcessCBAddress = CurrFrameResource->PostProcessCB->Resource()->GetGPUVirtualAddress();
		fxaaContext.SceneColorDescriptor = colorAdjustSceneColorDescriptor;

		mFXAAPass.Draw(fxaaContext);

		postCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
	}

	DrawSkeletonOverlayPass(postCommandList, rtvHandle, dsvHandle);
	if (mSkinWeightVizPass.IsEnabled())
	{
		mSkinWeightVizPass.SetSrvDescriptorHeap(SrvDescriptorHeap.Get());
		mSkinWeightVizPass.SetOtherTexDescriptor(otherTexDescriptor);
		D3DPassContext skinWeightVizContext = {};
		skinWeightVizContext.CommandList = postCommandList;
		skinWeightVizContext.RtvHandle = rtvHandle;
		skinWeightVizContext.DsvHandle = dsvHandle;
		mSkinWeightVizPass.Draw(skinWeightVizContext);
	}
	DrawGizmoPass(postCommandList, rtvHandle, dsvHandle);

	if (renderFPS)
	{
		postCommandList->SetPipelineState(PipelineState[文字管道].Get());
		D3DPassContext textContext = {};
		textContext.CommandList = postCommandList;
		textR->Draw(textContext, Text, DirectX::XMFLOAT2(0.32f, 0.25f), DirectX::XMFLOAT4{ 1.0f,1.0f,1.0f,1.0f }, CurrBackBufferIndex);
	}

	if (mEditor && renderEditor)
		mEditor->Render();

	D3D12_RESOURCE_BARRIER Barriers = {};
	postCommandList->OMSetRenderTargets(0, nullptr, false, nullptr);
	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	postCommandList->ResourceBarrier(1, &Barriers);

	ThrowIfFailed(postCommandList->Close());

	ID3D12CommandList* postPhaseCommandLists[] = { postCommandList };
	CommandQueue->ExecuteCommandLists(_countof(postPhaseCommandLists), postPhaseCommandLists);

	ThrowIfFailed(SwapChain->Present(0, 0));
	CurrBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();

	CurrFrameResource->Fence = ++fenceValue;
	ThrowIfFailed(CommandQueue->Signal(fence.Get(), fenceValue));
	DrainDeferredReleasesByCompletedFence();
	CurrentRenderFramePlan.Valid = false;
}

void D3DWindow::Update()
{
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

	UpdateFrameStateForRender();
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
	const bool hasRenderToTextureRequests = !mRenderToTextureViewProjTexById.empty();
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
	if (hasRenderToTextureRequests && fenceValue != 0 && fence->GetCompletedValue() < fenceValue)
	{
		// RenderToTexture 目前是跨帧共享的单实例 GPU 资源，而材质采样发生在后续场景 pass。
		// 在改造成 per-frame RTT 之前，RTT 场景先等待上一轮队列完成，避免同一 RTT
		// 在 GPU 时间线上被上一帧采样时，本帧又切回 RTV 写入。
		ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
		WaitForSingleObject(fenceEvent, INFINITE);
		DrainDeferredReleasesByCompletedFence();
	}
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
	RenderCameraRequestsToRenderTextures(midCommandList);

	if (hasSkyRenderItems)
	{
		ID3D12DescriptorHeap* mainSceneDescriptorHeaps[] = { SrvDescriptorHeap.Get() };
		MainScenePassContext mainScenePassContext = BuildMainScenePassContext(
			mainSceneDescriptorHeaps,
			static_cast<UINT>(_countof(mainSceneDescriptorHeaps)));
		BindMainScenePassCommonState(midCommandList, mainScenePassContext, rtvHandle, dsvHandle);
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
		SharedSceneInputDescriptorsInitialized;
	const bool allowNoSkyPostProcessTail = true;
	const bool allowNoSkyMainGeometrySubmission = true;
	const bool useNoSkyMinimalUiTail = false;
	auto endCommandList = CurrFrameResource->EndCommandList.Get();
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());
	ID3D12DescriptorHeap* srvDescriptorHeaps[] = { SrvDescriptorHeap.Get() };

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
		ID3D12DescriptorHeap* aoSrvDescriptorHeaps[] = { SrvDescriptorHeap.Get() };

		D3DPassContext aoPassContext = {};
		aoPassContext.CommandList = aoCommandList;
		aoPassContext.DescriptorHeaps = aoSrvDescriptorHeaps;
		aoPassContext.DescriptorHeapCount = static_cast<UINT>(_countof(aoSrvDescriptorHeaps));
		aoPassContext.AoCBAddress = CurrFrameResource->AOCB->Resource()->GetGPUVirtualAddress();
		aoPassContext.NormalDepthDescriptor = sharedNormalPrepass.GetNormalDepthSrv();
		ambientOcclusion.RecordSsaoPasses(
			aoPassContext,
			PipelineState[环境遮蔽管道].Get(),
			PipelineState[遮蔽模糊管道].Get());
		if (ShadowMaskConfig.Enabled && ShadowMaskConfig.UseInMainPbr && hasShadowCasterRenderItems)
		{
			D3DPassContext shadowPassContext = {};
			shadowPassContext.CommandList = aoCommandList;
			shadowPassContext.DescriptorHeaps = aoSrvDescriptorHeaps;
			shadowPassContext.DescriptorHeapCount = static_cast<UINT>(_countof(aoSrvDescriptorHeaps));
			shadowPassContext.PassCBAddress = CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress();
			shadowPassContext.LightCBAddress = CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress();
			shadowPassContext.NormalDepthDescriptor = sharedNormalPrepass.GetNormalDepthSrv();
			shadowPassContext.ShadowMapDescriptor = shadow2DDescriptorTable;
			mDirectionalShadowMaskPass.RecordPasses(
				shadowPassContext,
				mDirectionalShadowMaskPass.GetMaskPipelineState(),
				mDirectionalShadowMaskPass.GetBlurPipelineState());
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

	// 阶段 8-9：收口到统一的尾部编排器。
	RecordRenderTailPasses(
		endCommandList,
		rtvHandle,
		dsvHandle,
		srvDescriptorHeaps,
		static_cast<UINT>(_countof(srvDescriptorHeaps)),
		useRecordedFramePlan,
		hasOpaqueRenderItems,
		hasTransparentRenderItems,
		hasAoRenderItems,
		hasShadowCasterRenderItems,
		hasOitResources,
		enableVolumetricLightPass,
		allowNoSkyPostProcessTail,
		allowNoSkyMainGeometrySubmission,
		useNoSkyMinimalUiTail,
		renderFPS,
		renderEditor);
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

	ID3D12PipelineState* lastBoundPipelineState = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE lastBoundDiffuseSrv = {};
	D3D12_GPU_DESCRIPTOR_HANDLE lastBoundReflectionSrv = {};

	for (auto ritem : rditems)
	{
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
		const bool allowRenderToTextureSampling = !mRenderingRenderToTexturePass;
		const std::uint32_t diffuseRenderToTextureId = ritem->Obj->Material->DiffuseRenderToTextureId;
		if (allowRenderToTextureSampling &&
			diffuseRenderToTextureId != 0 &&
			diffuseRenderToTextureId != mActiveRenderToTextureTargetId)
		{
			const RenderToTexture* renderToTexture = FindRenderToTexture(diffuseRenderToTextureId);
			if (renderToTexture != nullptr &&
				renderToTexture->GetColorResource() != nullptr &&
				renderToTexture->GetGpuSrv().ptr != 0)
			{
				Tex = renderToTexture->GetGpuSrv();
			}
			else if (ritem->Obj->Material->DiffuseTexture != nullptr)
			{
				Tex = ritem->Obj->Material->DiffuseTexture->GetGPUTexDescriptor();
			}
		}
		else if (ritem->Obj->Material->DiffuseTexture != nullptr)
		{
			Tex = ritem->Obj->Material->DiffuseTexture->GetGPUTexDescriptor();
		}

		CD3DX12_GPU_DESCRIPTOR_HANDLE reflectionTex = otherTexDescriptor;
		if (reflectionTex.ptr == 0)
		{
			reflectionTex = CD3DX12_GPU_DESCRIPTOR_HANDLE(SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
			if (NullTextureHeapIndex < SrvDescriptorHeapCapacity)
				reflectionTex.Offset(NullTextureHeapIndex, CbvSrvUavDescriptorSize);
		}
		const Material* material = ritem->Obj->Material;
		if (material != nullptr &&
			allowRenderToTextureSampling &&
			material->EnableReflection &&
			material->ReflectionSource == MaterialReflectionSource::RenderToTexture &&
			material->ReflectionRenderToTextureId != 0 &&
			material->ReflectionRenderToTextureId != mActiveRenderToTextureTargetId)
		{
			const RenderToTexture* reflectionRenderToTexture =
				FindRenderToTexture(material->ReflectionRenderToTextureId);
			if (reflectionRenderToTexture != nullptr &&
				reflectionRenderToTexture->GetColorResource() != nullptr &&
				reflectionRenderToTexture->GetGpuSrv().ptr != 0)
			{
				reflectionTex = reflectionRenderToTexture->GetGpuSrv();
			}
		}

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress()
			+ ritem->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::ObjectCB, objCBAddress);

		D3D12_GPU_VIRTUAL_ADDRESS skinningCBAddress = 0;
		const bool canBindSkinningCB =
			ritem->IsSkinned && skinningCB != nullptr && ritem->SkinningCBIndex != UINT(-1);
		if (canBindSkinningCB)
		{
			skinningCBAddress = skinningCB->GetGPUVirtualAddress()
				+ ritem->SkinningCBIndex * skinningCBByteSize;
			cmdList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::SkinningCB, skinningCBAddress);
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
			cmdList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::PassCB, passCBAddress);
		}

		if (requiresMaterialAndTexture)
		{
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = MatCB->GetGPUVirtualAddress()
				+ ritem->Obj->Material->MatCBIndex * matCBByteSize;
			cmdList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::MaterialCB, matCBAddress);
			if (Tex.ptr != lastBoundDiffuseSrv.ptr)
			{
				cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::OtherTexTable, Tex);
				lastBoundDiffuseSrv = Tex;
			}
			if (reflectionTex.ptr != lastBoundReflectionSrv.ptr)
			{
				cmdList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::ReflectionTable, reflectionTex);
				lastBoundReflectionSrv = reflectionTex;
			}
		}

		cmdList->DrawIndexedInstanced(ritem->Obj->AggrObject->IndexCount, 1, ritem->Obj->AggrObject->StartIndexLocation, ritem->Obj->AggrObject->BaseVertexLocation, 0);
	}
}

void D3DWindow::DrawGizmoPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
	if (!mGizmoPass.IsVisible())
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

	if (GizmoObjectCB == nullptr)
	{
		GizmoObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(d3dDevice.Get(), SwapChainBufferCount, true);
	}

	ObjectConstants gizmoObjectConstants{};
	const DirectX::XMMATRIX gizmoWorldTransform =
		DirectX::XMMatrixScaling(renderData.DrawScale, renderData.DrawScale, renderData.DrawScale) *
		DirectX::XMMatrixTranslation(renderData.OriginWS.x, renderData.OriginWS.y, renderData.OriginWS.z);
	DirectX::XMStoreFloat4x4(&gizmoObjectConstants.WorldTransform, DirectX::XMMatrixTranspose(gizmoWorldTransform));
	gizmoObjectConstants.WorldInvTranspose = BuildWorldInverseTransposeMatrixFromWorldTransform(gizmoWorldTransform);
	DirectX::XMFLOAT4X4 gizmoStateTransform = MathHelps::Identity;
	gizmoStateTransform._11 = static_cast<float>(static_cast<std::uint32_t>(renderData.HoverHandle));
	gizmoStateTransform._22 = static_cast<float>(static_cast<std::uint32_t>(renderData.ActiveHandle));
	DirectX::XMStoreFloat4x4(&gizmoObjectConstants.TexTransform, DirectX::XMMatrixTranspose(XMLoadFloat4x4(&gizmoStateTransform)));
	GizmoObjectCB->CopyData(CurrBackBufferIndex, gizmoObjectConstants);

	const UINT gizmoObjectCBByteSize = CalculateConstantBufferByteSize(sizeof(ObjectConstants));
	const D3D12_GPU_VIRTUAL_ADDRESS gizmoObjectCBAddress =
		GizmoObjectCB->Resource()->GetGPUVirtualAddress() +
		static_cast<UINT64>(CurrBackBufferIndex) * gizmoObjectCBByteSize;

	auto aggrObjectIt = AggrObject.find(*geometryName);
	if (aggrObjectIt == AggrObject.end())
		return;

	D3DPassContext gizmoPassContext = {};
	gizmoPassContext.CommandList = cmdList;
	gizmoPassContext.RtvHandle = rtvHandle;
	gizmoPassContext.DsvHandle = dsvHandle;
	gizmoPassContext.ObjectCBAddress = gizmoObjectCBAddress;
	gizmoPassContext.PassCBAddress = CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress();
	mGizmoPass.Draw(
		gizmoPassContext,
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
	objectConstants.WorldInvTranspose = BuildWorldInverseTransposeMatrixFromWorldTransform(MathHelps::Identity);
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

	D3DPassContext skeletonOverlayContext = {};
	skeletonOverlayContext.CommandList = cmdList;
	skeletonOverlayContext.RtvHandle = rtvHandle;
	skeletonOverlayContext.DsvHandle = dsvHandle;
	skeletonOverlayContext.ObjectCBAddress = objectCBAddress;
	skeletonOverlayContext.PassCBAddress = CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress();
	mSkeletonOverlayPass.Draw(
		skeletonOverlayContext,
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
	D3DPassContext skinWeightVizContext = {};
	skinWeightVizContext.CommandList = cmdList;
	skinWeightVizContext.RtvHandle = rtvHandle;
	skinWeightVizContext.DsvHandle = dsvHandle;
	mSkinWeightVizPass.Draw(skinWeightVizContext);
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

RenderToTexture* D3DWindow::EnsureRenderToTexture(const RenderToTextureDesc& desc)
{
	if (!RenderToTextureDescriptorsReserved || desc.Id == 0)
		return nullptr;

	UINT slotIndex = UINT(-1);
	bool newSlotAllocated = false;
	auto slotIt = mRenderToTextureSlotById.find(desc.Id);
	if (slotIt != mRenderToTextureSlotById.end())
	{
		slotIndex = slotIt->second;
	}
	else
	{
		if (mRenderToTextureSlotById.size() >= MaxRenderToTextureCount)
			return nullptr;

		slotIndex = static_cast<UINT>(mRenderToTextureSlotById.size());
		mRenderToTextureSlotById.emplace(desc.Id, slotIndex);
		newSlotAllocated = true;
	}

	RenderToTexture* renderToTexture = mRenderToTextureManager.Find(desc.Id);
	if (renderToTexture != nullptr)
		return renderToTexture;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDsv;
	if (!TryBuildRenderToTextureDescriptorHandles(slotIndex, &cpuSrv, &gpuSrv, &cpuRtv, &cpuDsv))
	{
		if (newSlotAllocated)
			mRenderToTextureSlotById.erase(desc.Id);
		return nullptr;
	}

	renderToTexture = mRenderToTextureManager.Create(desc, cpuSrv, gpuSrv, cpuRtv, cpuDsv);
	if (renderToTexture == nullptr)
	{
		if (newSlotAllocated)
			mRenderToTextureSlotById.erase(desc.Id);
		return nullptr;
	}

	return renderToTexture;
}

RenderToTexture* D3DWindow::FindRenderToTexture(std::uint32_t id)
{
	return mRenderToTextureManager.Find(id);
}

const RenderToTexture* D3DWindow::FindRenderToTexture(std::uint32_t id) const
{
	return mRenderToTextureManager.Find(id);
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
	RenderItem* renderItem = GetRenderItem(renderItemName);
	if (!renderItem)
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

DirectX::XMFLOAT3 D3DWindow::GetColorAdjustWhiteBalance() const
{
	return DirectX::XMFLOAT3(
		MainPostProcessCB.ColorAdjustSettings.x,
		MainPostProcessCB.ColorAdjustSettings.y,
		MainPostProcessCB.ColorAdjustSettings.z);
}

void D3DWindow::SetColorAdjustWhiteBalance(const DirectX::XMFLOAT3& whiteBalance)
{
	MainPostProcessCB.ColorAdjustSettings.x = std::clamp(whiteBalance.x, 0.0f, 4.0f);
	MainPostProcessCB.ColorAdjustSettings.y = std::clamp(whiteBalance.y, 0.0f, 4.0f);
	MainPostProcessCB.ColorAdjustSettings.z = std::clamp(whiteBalance.z, 0.0f, 4.0f);
}

float D3DWindow::GetColorAdjustContrast() const
{
	return MainPostProcessCB.ColorAdjustSettings.w;
}

void D3DWindow::SetColorAdjustContrast(float contrast)
{
	MainPostProcessCB.ColorAdjustSettings.w = std::clamp(contrast, 0.0f, 2.0f);
}

float D3DWindow::GetColorAdjustSaturation() const
{
	return MainPostProcessCB.ColorAdjustSettings2.x;
}

void D3DWindow::SetColorAdjustSaturation(float saturation)
{
	MainPostProcessCB.ColorAdjustSettings2.x = std::clamp(saturation, 0.0f, 2.0f);
}

float D3DWindow::GetEnvironmentDiffuseIntensity() const
{
	return MainPassCB.EnvironmentLightingSettings.x;
}

void D3DWindow::SetEnvironmentDiffuseIntensity(float intensity)
{
	MainPassCB.EnvironmentLightingSettings.x = std::clamp(intensity, 0.0f, 4.0f);
}

float D3DWindow::GetEnvironmentSpecularIntensity() const
{
	return MainPassCB.EnvironmentLightingSettings.y;
}

void D3DWindow::SetEnvironmentSpecularIntensity(float intensity)
{
	MainPassCB.EnvironmentLightingSettings.y = std::clamp(intensity, 0.0f, 4.0f);
}

bool D3DWindow::IsEnvironmentBrdfLutEnabled() const
{
	return MainPassCB.EnvironmentLightingSettings.z > 0.5f;
}

void D3DWindow::SetEnvironmentBrdfLutEnabled(bool enable)
{
	MainPassCB.EnvironmentLightingSettings.z = enable ? 1.0f : 0.0f;
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
