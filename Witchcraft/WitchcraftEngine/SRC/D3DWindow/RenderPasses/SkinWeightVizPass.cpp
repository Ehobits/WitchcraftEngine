#include "SkinWeightVizPass.h"
#include "../D3DWindow.h"
#include "ECS/WitchcraECS.h"
#include "ECS/Component/SkinningRuntimeComponent.h"
#include "Editor/Editor.h"
#include "Helpers/MathHelpers.h"

#include "../D3DWindowStaticHelpers.h"

// =========================
// 初始化 / 管线
// =========================

void SkinWeightVizPass::Initialize(ID3D12Device* device)
{
	mDevice = device;
}

void SkinWeightVizPass::CreatePipesAndShaders()
{
	mVertexShader = CompileShader(L"DATA/Shaders/SkinWeightVisualization", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/SkinWeightVisualization", nullptr, "PS", "ps_5_1");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = mBasePsoDesc;
	// 使用蒙皮输入布局（SkinnedVertex = Vertex + VertexBoneInfluence4）
	// InputLayout 由调用者通过 SetBasePsoDesc 中设置，此处在 Initialize 时保留
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(mVertexShader.Get());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(mPixelShader.Get());
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPipelineState)));
	SetD3DObjectName(mPipelineState.Get(), L"管线_蒙皮权重可视化");
}

// =========================
// 增量更新刷权重 VB
// =========================

void SkinWeightVizPass::IncrementalUpdateVB(const std::vector<std::tuple<const void*, UINT, Witchcraft::Animation::VertexBoneInfluence4>>& changes)
{
	if (changes.empty())
		return;

	if (mVisualizationGeometry.VertexBufferGPU == nullptr ||
		mMappedVertexData == nullptr ||
		mVertexBufferByteSize == 0)
	{
		mNeedsBuild = true;
		return;
	}

	constexpr size_t kSkinnedVertexStride = sizeof(Witchcraft::Animation::SkinnedVertex);
	constexpr size_t kSkinningOffset = sizeof(Vertex);

	for (const auto& change : changes)
	{
		const void* meshKey = std::get<0>(change);
		const UINT vertexIndex = std::get<1>(change);
		const Witchcraft::Animation::VertexBoneInfluence4& skinning = std::get<2>(change);

		const auto meshOffsetIt = mMeshOffsets.find(meshKey);
		if (meshOffsetIt == mMeshOffsets.end())
		{
			mNeedsBuild = true;
			return;
		}

		const size_t globalVertexIndex =
			static_cast<size_t>(meshOffsetIt->second) + static_cast<size_t>(vertexIndex);
		const size_t byteOffset = globalVertexIndex * kSkinnedVertexStride + kSkinningOffset;
		if (byteOffset + sizeof(Witchcraft::Animation::VertexBoneInfluence4) > mVertexBufferByteSize)
		{
			mNeedsBuild = true;
			return;
		}

		memcpy(
			mMappedVertexData + byteOffset,
			&skinning,
			sizeof(Witchcraft::Animation::VertexBoneInfluence4));
	}
}

// =========================
// 重建刷权重可视化几何体
// =========================

void SkinWeightVizPass::TryRebuildFromEditor()
{
	if (!mNeedsBuild || mEditor == nullptr)
		return;

	const WModelFileData* modelData = mEditor->GetBrushWeightModelData();
	if (modelData == nullptr || modelData->Meshes.empty())
	{
		mNeedsBuild = false;
		return;
	}

	std::vector<BrushVizMeshSlice> slices;
	slices.reserve(modelData->Meshes.size());
	for (UINT i = 0; i < static_cast<UINT>(modelData->Meshes.size()); ++i)
	{
		const auto& m = modelData->Meshes[i];
		slices.push_back({ &m, m.Id, m.Vertices.data(), m.Indices.data(),
			m.Skinning.empty() ? nullptr : m.Skinning.data(),
			static_cast<UINT>(m.Vertices.size()), static_cast<UINT>(m.Indices.size()) });
	}
	std::function<BrushVizNode(const WModelNodeData&)> cn = [&](const WModelNodeData& s) -> BrushVizNode {
		BrushVizNode n; n.LocalTransform = s.LocalTransform; n.MeshRef = s.MeshRef;
		for (const auto& c : s.Children) n.Children.push_back(cn(c));
		return n;
	};
	RebuildGeometry(slices, cn(modelData->RootNode));
	mNeedsBuild = false;
}

void SkinWeightVizPass::RebuildGeometry(const std::vector<BrushVizMeshSlice>& meshes, const BrushVizNode& rootNode)
{
	// 先延迟释放旧几何资源
	if (mVisualizationGeometry.VertexBufferGPU != nullptr ||
		mVisualizationGeometry.IndexBufferGPU != nullptr ||
		mVisualizationGeometry.VertexBufferUploader != nullptr ||
		mVisualizationGeometry.IndexBufferUploader != nullptr)
	{
		if (mDeferredReleaseQueue != nullptr && mComputeFenceFn)
			mDeferredReleaseQueue->push_back({ std::move(mVisualizationGeometry), mComputeFenceFn() });
	}

	mVisualizationGeometry = MeshGeometry{};
	mVisualizationGeometry.Name = L"BrushWeightViz";
	mMeshOffsets.clear();
	mMappedVertexData = nullptr;
	mVertexBufferByteSize = 0;

	if (mDevice == nullptr)
		return;

	auto buildNodeLocalMatrix = [](const Transform& transform) -> DirectX::XMMATRIX
	{
		return
			DirectX::XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z) *
			DirectX::XMMatrixRotationRollPitchYaw(
				DirectX::XMConvertToRadians(transform.rotation.x),
				DirectX::XMConvertToRadians(transform.rotation.y),
				DirectX::XMConvertToRadians(transform.rotation.z)) *
			DirectX::XMMatrixTranslation(transform.position.x, transform.position.y, transform.position.z);
	};

	std::unordered_map<std::wstring, UINT> meshIdxByRef;
	for (UINT i = 0; i < static_cast<UINT>(meshes.size()); ++i)
	{
		if (meshes[i].VertexCount > 0 && meshes[i].IndexCount > 0 && !meshes[i].Id.empty())
			meshIdxByRef[meshes[i].Id] = i;
	}

	struct BrushVizMeshInstance
	{
		UINT MeshIndex = 0;
		DirectX::XMMATRIX LocalToModel = DirectX::XMMatrixIdentity();
	};

	std::vector<BrushVizMeshInstance> meshInstances;

	std::function<void(const BrushVizNode&, const DirectX::XMMATRIX&)> collectMeshInstances;
	collectMeshInstances = [&](const BrushVizNode& node, const DirectX::XMMATRIX& parentMatrix)
	{
		const DirectX::XMMATRIX nodeLocalMatrix = buildNodeLocalMatrix(node.LocalTransform);
		const DirectX::XMMATRIX nodeToModelMatrix = nodeLocalMatrix * parentMatrix;

		if (!node.MeshRef.empty())
		{
			const auto it = meshIdxByRef.find(node.MeshRef);
			if (it != meshIdxByRef.end())
				meshInstances.push_back({ it->second, nodeToModelMatrix });
		}

		for (const BrushVizNode& child : node.Children)
			collectMeshInstances(child, nodeToModelMatrix);
	};

	collectMeshInstances(rootNode, DirectX::XMMatrixIdentity());

	UINT totalVertexCount = 0;
	UINT totalIndexCount = 0;
	for (const BrushVizMeshInstance& meshInstance : meshInstances)
	{
		if (meshInstance.MeshIndex >= meshes.size()) continue;
		const BrushVizMeshSlice& s = meshes[meshInstance.MeshIndex];
		if (s.VertexCount == 0 || s.IndexCount == 0) continue;
		totalVertexCount += s.VertexCount;
		totalIndexCount += s.IndexCount;
	}

	if (totalVertexCount == 0 || totalIndexCount == 0)
		return;

	constexpr UINT kSkinnedVertexStride = sizeof(Witchcraft::Animation::SkinnedVertex);
	std::vector<BYTE> vertexBuffer(totalVertexCount * kSkinnedVertexStride);
	std::vector<std::uint32_t> indexBuffer(totalIndexCount);

	UINT vertexWritePos = 0;
	UINT indexWritePos = 0;
	UINT vertexBaseOffset = 0;

	for (const BrushVizMeshInstance& meshInstance : meshInstances)
	{
		if (meshInstance.MeshIndex >= meshes.size())
			continue;

		const BrushVizMeshSlice& slice = meshes[meshInstance.MeshIndex];
		if (slice.VertexCount == 0 || slice.IndexCount == 0)
			continue;

		const UINT meshVertexCount = slice.VertexCount;
		mMeshOffsets[slice.SourcePtr] = vertexBaseOffset;
		for (UINT i = 0; i < meshVertexCount; ++i)
		{
			Vertex transformedVertex = slice.Vertices[i];
			const DirectX::XMVECTOR localPosition = DirectX::XMLoadFloat3(&slice.Vertices[i].Pos);
			const DirectX::XMVECTOR localNormal = DirectX::XMLoadFloat3(&slice.Vertices[i].Normal);
			const DirectX::XMVECTOR localTangent = DirectX::XMLoadFloat3(&slice.Vertices[i].Tangent);
			const DirectX::XMVECTOR localBitangent = DirectX::XMLoadFloat3(&slice.Vertices[i].Bitangent);
			DirectX::XMStoreFloat3(&transformedVertex.Pos, DirectX::XMVector3TransformCoord(localPosition, meshInstance.LocalToModel));
			DirectX::XMStoreFloat3(&transformedVertex.Normal, DirectX::XMVector3TransformNormal(localNormal, meshInstance.LocalToModel));
			DirectX::XMStoreFloat3(&transformedVertex.Tangent, DirectX::XMVector3TransformNormal(localTangent, meshInstance.LocalToModel));
			DirectX::XMStoreFloat3(&transformedVertex.Bitangent, DirectX::XMVector3TransformNormal(localBitangent, meshInstance.LocalToModel));

			memcpy(
				vertexBuffer.data() + (vertexWritePos + i) * kSkinnedVertexStride,
				&transformedVertex, sizeof(Vertex));

			Witchcraft::Animation::VertexBoneInfluence4 skinning = {};
			if (slice.Skinning != nullptr && i < slice.VertexCount)
				skinning = slice.Skinning[i];
			else
				skinning.BoneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
			memcpy(
				vertexBuffer.data() + (vertexWritePos + i) * kSkinnedVertexStride + sizeof(Vertex),
				&skinning, sizeof(Witchcraft::Animation::VertexBoneInfluence4));
		}

		for (UINT i = 0; i < slice.IndexCount; ++i)
			indexBuffer[indexWritePos + i] = slice.Indices[i] + vertexBaseOffset;

		vertexWritePos += meshVertexCount;
		indexWritePos += slice.IndexCount;
		vertexBaseOffset += meshVertexCount;
	}

	// 刷权重预览需要频繁改写骨骼权重，因此 VB 保持在 Upload 堆并长期映射。
	const UINT64 vbByteSize = static_cast<UINT64>(vertexBuffer.size());
	const UINT64 ibByteSize = static_cast<UINT64>(indexBuffer.size() * sizeof(std::uint32_t));

	{
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbByteSize);
		ThrowIfFailed(mDevice->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(mVisualizationGeometry.VertexBufferGPU.GetAddressOf())));

		BYTE* vbMapped = nullptr;
		mVisualizationGeometry.VertexBufferGPU->Map(0, nullptr, reinterpret_cast<void**>(&vbMapped));
		memcpy(vbMapped, vertexBuffer.data(), static_cast<size_t>(vbByteSize));
		mMappedVertexData = vbMapped;
		mVertexBufferByteSize = vbByteSize;
	}

	{
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibByteSize);
		ThrowIfFailed(mDevice->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(mVisualizationGeometry.IndexBufferGPU.GetAddressOf())));
		BYTE* ibMapped = nullptr;
		mVisualizationGeometry.IndexBufferGPU->Map(0, nullptr, reinterpret_cast<void**>(&ibMapped));
		memcpy(ibMapped, indexBuffer.data(), static_cast<size_t>(ibByteSize));
		mVisualizationGeometry.IndexBufferGPU->Unmap(0, nullptr);
	}

	if (mVisualizationGeometry.VertexBufferGPU == nullptr ||
		mVisualizationGeometry.IndexBufferGPU == nullptr)
	{
		mVisualizationGeometry = MeshGeometry{};
		mMappedVertexData = nullptr;
		mVertexBufferByteSize = 0;
		return;
	}

	mVisualizationGeometry.vertexBufferView.BufferLocation =
		mVisualizationGeometry.VertexBufferGPU->GetGPUVirtualAddress();
	mVisualizationGeometry.vertexBufferView.StrideInBytes = kSkinnedVertexStride;
	mVisualizationGeometry.vertexBufferView.SizeInBytes = static_cast<UINT>(vbByteSize);

	mVisualizationGeometry.indexBufferView.BufferLocation =
		mVisualizationGeometry.IndexBufferGPU->GetGPUVirtualAddress();
	mVisualizationGeometry.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	mVisualizationGeometry.indexBufferView.SizeInBytes = static_cast<UINT>(ibByteSize);

	mVisualizationGeometry.VertexByteStride = kSkinnedVertexStride;
	mNeedsBuild = false;
}

// =========================
// 绘制
// =========================

void SkinWeightVizPass::Draw(
	ID3D12GraphicsCommandList* cmdList,
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
	// 获取实体的世界矩阵
	ObjectConstants objectConstants{};
	DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixIdentity();
	if (mECS->HasEntity(mTargetEntity))
	{
		Transform worldTransform;
		if (mECS->GetEntityWorldTransform(mTargetEntity, &worldTransform))
		{
			worldMatrix =
				DirectX::XMMatrixScaling(worldTransform.scale.x, worldTransform.scale.y, worldTransform.scale.z) *
				DirectX::XMMatrixRotationRollPitchYaw(
					DirectX::XMConvertToRadians(worldTransform.rotation.x),
					DirectX::XMConvertToRadians(worldTransform.rotation.y),
					DirectX::XMConvertToRadians(worldTransform.rotation.z)) *
				DirectX::XMMatrixTranslation(worldTransform.position.x, worldTransform.position.y, worldTransform.position.z);
		}
	}
	DirectX::XMStoreFloat4x4(&objectConstants.WorldTransform, DirectX::XMMatrixTranspose(worldMatrix));
	objectConstants.TexTransform = MathHelps::Identity;

	// 创建或复用刷权重可视化的专用 ObjectCB
	if (mObjectCB == nullptr)
		mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(mDevice, 1, true);

	mObjectCB->CopyData(0, objectConstants);
	const D3D12_GPU_VIRTUAL_ADDRESS objectCBAddress =
		mObjectCB->Resource()->GetGPUVirtualAddress();

	// 可视化网格复用目标实体当前的骨骼调色板
	if (mSkinningCB == nullptr)
		mSkinningCB = std::make_unique<UploadBuffer<SkinningConstants>>(mDevice, 1, true);

	SkinningConstants visualizationSkinning{};
	ResetSkinningConstantsToIdentity(&visualizationSkinning);
	SceneEntityBase* runtimeOwnerEntity = mResolveSkinnningFn
		? mResolveSkinnningFn(mECS, mTargetEntity)
		: nullptr;
	const SkinningRuntimeComponent* runtimeComponent =
		runtimeOwnerEntity != nullptr
		? mECS->GetComponent<SkinningRuntimeComponent>(runtimeOwnerEntity)
		: nullptr;
	if (runtimeComponent != nullptr)
	{
		const auto& palette = runtimeComponent->GetPalette();
		const UINT boneCount = (std::min)(
			static_cast<UINT>(palette.FinalBoneMatrices.size()),
			static_cast<UINT>(MaxSkinBonesPerDraw));
		for (UINT boneIndex = 0; boneIndex < boneCount; ++boneIndex)
		{
			if (!IsFiniteSkinningMatrix(palette.FinalBoneMatrices[boneIndex]))
				continue;

			const DirectX::XMMATRIX boneMatrix =
				DirectX::XMLoadFloat4x4(&palette.FinalBoneMatrices[boneIndex]);
			DirectX::XMStoreFloat4x4(
				&visualizationSkinning.BoneMatrices[boneIndex],
				DirectX::XMMatrixTranspose(boneMatrix));
		}
	}
	mSkinningCB->CopyData(0, visualizationSkinning);
	const D3D12_GPU_VIRTUAL_ADDRESS skinningCBAddress =
		mSkinningCB->Resource()->GetGPUVirtualAddress();

	if (mSrvHeap == nullptr)
		return;
	const D3D12_GPU_DESCRIPTOR_HANDLE defaultSrvDescriptor =
		mSrvHeap->GetGPUDescriptorHandleForHeapStart();
	if (defaultSrvDescriptor.ptr == 0)
		return;
	const D3D12_GPU_DESCRIPTOR_HANDLE otherTexDescriptor =
		mOtherTexDescriptor.ptr != 0 ? mOtherTexDescriptor : defaultSrvDescriptor;

	// 设置根签名参数
	cmdList->SetGraphicsRootSignature(mRootSignature.Get());
	cmdList->SetGraphicsRootConstantBufferView(0, objectCBAddress);
	cmdList->SetGraphicsRootConstantBufferView(1, mFramePassCB->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootConstantBufferView(4, skinningCBAddress);

	// 绑定 SRV 描述符堆
	ID3D12DescriptorHeap* srvHeaps[] = { mSrvHeap };
	cmdList->SetDescriptorHeaps(1, srvHeaps);
	cmdList->SetGraphicsRootDescriptorTable(5, otherTexDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(6, defaultSrvDescriptor);

	// 设置 PSO
	cmdList->SetPipelineState(mPipelineState.Get());

	// 绑定顶点/索引缓冲区并绘制
	cmdList->IASetVertexBuffers(0, 1, &mVisualizationGeometry.vertexBufferView);
	cmdList->IASetIndexBuffer(&mVisualizationGeometry.indexBufferView);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const UINT indexCount = mVisualizationGeometry.indexBufferView.SizeInBytes / sizeof(UINT);
	cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}
