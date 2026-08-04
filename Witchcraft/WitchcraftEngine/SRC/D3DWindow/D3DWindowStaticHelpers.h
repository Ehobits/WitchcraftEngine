#pragma once

#include "D3DWindow.h"
#include "D3D12_framework.h"
#include "ECS/WitchcraECS.h"
#include "ECS/Component/SkinningRuntimeComponent.h"

static constexpr bool kEnableComputeSkinning = false;

// ── 蒙皮矩阵校验 ──

inline bool IsFiniteSkinningFloat(float value)
{
	return std::isfinite(value);
}

inline bool IsFiniteSkinningMatrix(const DirectX::XMFLOAT4X4& value)
{
	return
		IsFiniteSkinningFloat(value._11) && IsFiniteSkinningFloat(value._12) && IsFiniteSkinningFloat(value._13) && IsFiniteSkinningFloat(value._14) &&
		IsFiniteSkinningFloat(value._21) && IsFiniteSkinningFloat(value._22) && IsFiniteSkinningFloat(value._23) && IsFiniteSkinningFloat(value._24) &&
		IsFiniteSkinningFloat(value._31) && IsFiniteSkinningFloat(value._32) && IsFiniteSkinningFloat(value._33) && IsFiniteSkinningFloat(value._34) &&
		IsFiniteSkinningFloat(value._41) && IsFiniteSkinningFloat(value._42) && IsFiniteSkinningFloat(value._43) && IsFiniteSkinningFloat(value._44);
}

inline bool IsFiniteRenderMatrix(const DirectX::XMFLOAT4X4& value)
{
	return
		std::isfinite(value._11) && std::isfinite(value._12) && std::isfinite(value._13) && std::isfinite(value._14) &&
		std::isfinite(value._21) && std::isfinite(value._22) && std::isfinite(value._23) && std::isfinite(value._24) &&
		std::isfinite(value._31) && std::isfinite(value._32) && std::isfinite(value._33) && std::isfinite(value._34) &&
		std::isfinite(value._41) && std::isfinite(value._42) && std::isfinite(value._43) && std::isfinite(value._44);
}

// ── 蒙皮常量 ──

inline DirectX::XMFLOAT4X4 BuildIdentitySkinningMatrix()
{
	return DirectX::XMFLOAT4X4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
}

inline void ResetSkinningConstantsToIdentity(SkinningConstants* skinningConstants)
{
	if (skinningConstants == nullptr)
		return;

	const DirectX::XMFLOAT4X4 identityMatrix = BuildIdentitySkinningMatrix();
	for (UINT boneIndex = 0; boneIndex < MaxSkinBonesPerDraw; ++boneIndex)
		skinningConstants->BoneMatrices[boneIndex] = identityMatrix;
}

// ── GPU 缓冲创建 ──

inline bool CreateUploadStructuredBuffer(
	ID3D12Device* device,
	const void* initData,
	UINT64 byteSize,
	ID3D12Resource** outResource)
{
	if (device == nullptr || initData == nullptr || byteSize == 0 || outResource == nullptr)
		return false;

	*outResource = nullptr;

	const D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	const D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

	ComPtr<ID3D12Resource> resource = nullptr;
	if (FAILED(device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(resource.GetAddressOf()))))
	{
		return false;
	}

	void* mappedData = nullptr;
	const CD3DX12_RANGE readRange(0, 0);
	if (FAILED(resource->Map(0, &readRange, &mappedData)))
		return false;

	memcpy(mappedData, initData, static_cast<size_t>(byteSize));
	resource->Unmap(0, nullptr);
	*outResource = resource.Detach();
	return true;
}

inline bool CreateUnorderedAccessVertexBuffer(
	ID3D12Device* device,
	UINT64 byteSize,
	ID3D12Resource** outResource)
{
	if (device == nullptr || byteSize == 0 || outResource == nullptr)
		return false;

	*outResource = nullptr;

	const D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	ComPtr<ID3D12Resource> resource = nullptr;
	if (FAILED(device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
		nullptr,
		IID_PPV_ARGS(resource.GetAddressOf()))))
	{
		return false;
	}

	*outResource = resource.Detach();
	return true;
}

// ── ECS 辅助 ──

inline std::wstring GetEntityDebugName(WitchcraECS* ecs, SceneEntityBase* entity)
{
	if (ecs == nullptr || entity == nullptr)
		return L"<null>";

	const std::wstring entityName = ecs->GetEntityName(entity);
	return entityName.empty() ? L"<unnamed>" : entityName;
}

inline SceneEntityBase* ResolveSkinningRuntimeOwnerEntity(WitchcraECS* ecs, SceneEntityBase* sourceEntity)
{
	if (ecs == nullptr || sourceEntity == nullptr)
		return nullptr;

	if (ecs->GetComponent<SkinningRuntimeComponent>(sourceEntity) != nullptr)
		return sourceEntity;

	for (SceneEntityBase* parentEntity = ecs->GetParentEntity(sourceEntity);
		parentEntity != nullptr;
		parentEntity = ecs->GetParentEntity(parentEntity))
	{
		if (ecs->GetComponent<SkinningRuntimeComponent>(parentEntity) != nullptr)
			return parentEntity;
	}

	return sourceEntity;
}

// ── 调试辅助 ──
// 发出渲染变换调试消息
inline void EmitRenderTransformDebugMessage(
	const wchar_t* stage,
	const std::wstring& renderItemName,
	WitchcraECS* ecs,
	SceneEntityBase* entity,
	const DirectX::XMFLOAT4X4& worldTransform,
	const DirectX::XMFLOAT4X4& texTransform)
{
	wchar_t debugText[1024] = {};
	swprintf_s(
		debugText,
		L"[RenderTransformDebug] stage=%s renderItem=%s entity=%s worldFinite=%d texFinite=%d worldTranslation=(%.4f,%.4f,%.4f)\n",
		stage != nullptr ? stage : L"<null>",
		renderItemName.c_str(),
		GetEntityDebugName(ecs, entity).c_str(),
		IsFiniteRenderMatrix(worldTransform) ? 1 : 0,
		IsFiniteRenderMatrix(texTransform) ? 1 : 0,
		worldTransform._41,
		worldTransform._42,
		worldTransform._43);

	EngineHelpers::AddLog(debugText);
}
