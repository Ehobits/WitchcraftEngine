#pragma once

#include "D3DWindow.h"

#include <bit>
#include <functional>

// D3DWindow 阴影相关内部辅助定义。
// 推荐仅供 D3DWindow 调用；当前设计目标是由 D3DWindow.cpp 在包含 D3DWindow.h 之后引入，
// 不建议作为通用引擎头被其他模块直接依赖。

struct ShadowPoolLimits
{
	static constexpr UINT DirectionalTextureCount = 32u;
	static constexpr UINT SpotTextureCount = 224u;
	static constexpr UINT PointCubeTextureCount = 64u;
	static constexpr UINT Combined2DTextureCount = DirectionalTextureCount + SpotTextureCount;
};

struct ShadowHashUtils
{
	template<typename T>
	static void HashCombine(std::uint64_t& seed, const T& value)
	{
		const std::uint64_t valueHash = static_cast<std::uint64_t>(std::hash<T>{}(value));
		seed ^= valueHash + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
	}

	static void HashFloat(std::uint64_t& seed, float value)
	{
		HashCombine(seed, std::bit_cast<std::uint32_t>(value));
	}

	static std::uint64_t BuildShadowRelevantLightHash(const Light& light)
	{
		std::uint64_t hash = 0;
		HashFloat(hash, light.Type);
		HashCombine(hash, light.CastShadow);
		HashFloat(hash, light.Position.x);
		HashFloat(hash, light.Position.y);
		HashFloat(hash, light.Position.z);
		HashFloat(hash, light.Direction.x);
		HashFloat(hash, light.Direction.y);
		HashFloat(hash, light.Direction.z);
		HashFloat(hash, light.Up.x);
		HashFloat(hash, light.Up.y);
		HashFloat(hash, light.Up.z);
		HashFloat(hash, light.Power);
		return hash;
	}
};

struct ShadowRuntimeHelpers
{
	static bool SupportsShadowDirtyScheduling(ShadowPoolPlanner::PoolKind pool)
	{
		return pool == ShadowPoolPlanner::PoolKind::Point ||
			pool == ShadowPoolPlanner::PoolKind::Spot;
	}

	static bool SupportsShadowStaticCache(ShadowPoolPlanner::PoolKind pool)
	{
		// 当前 working/cache 路径只收口在 point/spot。
		// Directional 仍沿用保守全量路径，避免把 CSM 级联缓存一起搅进来。
		return SupportsShadowDirtyScheduling(pool);
	}

	static bool IsShadowSlotOwnedByThread(UINT shadowSlotIndex, int threadIndex)
	{
		if (threadIndex < 0)
			return false;
		return (shadowSlotIndex % NumContexts) == static_cast<UINT>(threadIndex);
	}

	static bool ShouldRecordShadowUpdateRequest(const std::vector<ShadowUpdateRequest>& updateRequests, UINT lightIndex)
	{
		if (lightIndex >= updateRequests.size())
			return true;

		const ShadowUpdateRequest& updateRequest = updateRequests[lightIndex];
		if (!SupportsShadowDirtyScheduling(updateRequest.Pool))
			return true;

		return updateRequest.Type != ShadowUpdateRequestType::None;
	}

	static ShadowUpdateRequestType ResolveShadowEntryUpdateType(
		const std::vector<ShadowUpdateRequest>& updateRequests,
		UINT lightIndex)
	{
		if (lightIndex >= updateRequests.size())
			return ShadowUpdateRequestType::FullUpdate;

		const ShadowUpdateRequest& updateRequest = updateRequests[lightIndex];
		if (!SupportsShadowDirtyScheduling(updateRequest.Pool))
			return ShadowUpdateRequestType::FullUpdate;

		return updateRequest.Type;
	}

	static bool WasShadowSlotWrittenThisFrame(
		const std::vector<ShadowRenderEntry>& shadowEntries,
		const std::vector<ShadowUpdateRequest>& updateRequests,
		UINT shadowSlotIndex)
	{
		for (const ShadowRenderEntry& entry : shadowEntries)
		{
			if (entry.ShadowSlotIndex != shadowSlotIndex)
				continue;
			if (ShouldRecordShadowUpdateRequest(updateRequests, entry.LightIndex))
				return true;
		}

		return false;
	}

	static ShadowUpdateRequestType MergeShadowUpdateRequestType(
		ShadowUpdateRequestType lhs,
		ShadowUpdateRequestType rhs)
	{
		if (lhs == ShadowUpdateRequestType::FullUpdate || rhs == ShadowUpdateRequestType::FullUpdate)
			return ShadowUpdateRequestType::FullUpdate;
		if (lhs == ShadowUpdateRequestType::DynamicOnlyUpdate || rhs == ShadowUpdateRequestType::DynamicOnlyUpdate)
			return ShadowUpdateRequestType::DynamicOnlyUpdate;
		return ShadowUpdateRequestType::None;
	}

	static bool IsDynamicShadowSceneType(SceneEntityType sceneType)
	{
		return sceneType == SceneEntityType::DynamicScenery ||
			sceneType == SceneEntityType::Interactive;
	}
};

struct RenderInfluenceHelpers
{
	static DirectX::BoundingSphere BuildWorldBoundingSphere(const RenderItem& renderItem)
	{
		DirectX::BoundingSphere worldSphere(
			DirectX::XMFLOAT3(renderItem.WorldTransform._41, renderItem.WorldTransform._42, renderItem.WorldTransform._43),
			1.0f);

		if (renderItem.HasLocalBounds)
		{
			DirectX::BoundingBox worldBounds;
			renderItem.LocalBounds.Transform(worldBounds, DirectX::XMLoadFloat4x4(&renderItem.WorldTransform));
			DirectX::BoundingSphere::CreateFromBoundingBox(worldSphere, worldBounds);
		}

		return worldSphere;
	}

	static bool IntersectsPointLightInfluence(const Light& light, const RenderItem& renderItem)
	{
		const DirectX::BoundingSphere worldSphere = BuildWorldBoundingSphere(renderItem);
		const DirectX::XMFLOAT3& lightPosition = light.Position;
		const float influenceRadius = (std::max)(20.0f, (std::max)(light.SpotRange, light.VolumetricAttenuationDistance));
		const float dx = worldSphere.Center.x - lightPosition.x;
		const float dy = worldSphere.Center.y - lightPosition.y;
		const float dz = worldSphere.Center.z - lightPosition.z;
		const float distanceSq = dx * dx + dy * dy + dz * dz;
		const float maxDistance = influenceRadius + worldSphere.Radius;
		return distanceSq <= maxDistance * maxDistance;
	}

	static bool IntersectsSpotLightInfluence(const Light& light, const RenderItem& renderItem)
	{
		const DirectX::BoundingSphere worldSphere = BuildWorldBoundingSphere(renderItem);
		const DirectX::XMFLOAT3& lightPosition = light.Position;
		const float range = (std::max)(std::clamp(light.SpotRange, 0.1f, 500.0f), 1.0f);
		const float dx = worldSphere.Center.x - lightPosition.x;
		const float dy = worldSphere.Center.y - lightPosition.y;
		const float dz = worldSphere.Center.z - lightPosition.z;
		const float distanceSq = dx * dx + dy * dy + dz * dz;
		const float maxDistance = range + worldSphere.Radius;
		if (distanceSq > maxDistance * maxDistance)
			return false;

		const DirectX::XMVECTOR lightForward = XMVector3Normalize(XMLoadFloat3(&light.Direction));
		const DirectX::XMVECTOR toCaster = XMVectorSet(dx, dy, dz, 0.0f);
		if (distanceSq <= 1e-6f)
			return true;

		const DirectX::XMVECTOR toCasterDir = XMVector3Normalize(toCaster);
		const float coneCos = XMVectorGetX(XMVector3Dot(lightForward, toCasterDir));
		const float outerHalfAngleDegrees = std::clamp(light.SpotOuterAngleDegrees, 5.0f, 85.0f);
		const float outerHalfAngleRadians = DirectX::XMConvertToRadians(outerHalfAngleDegrees);
		const float casterAngularPadding = std::asin(std::clamp(worldSphere.Radius / (std::sqrt(distanceSq) + 1e-4f), 0.0f, 1.0f));
		const float acceptedCos = std::cos(outerHalfAngleRadians + casterAngularPadding);
		return coneCos >= acceptedCos;
	}

	static bool IntersectsDirectionalCascadeCullBounds(const ShadowRenderEntry& shadowEntry, const RenderItem& renderItem)
	{
		if (shadowEntry.ViewKind != ShadowViewKind::DirectionalCascade || !shadowEntry.HasLightSpaceCullBounds)
			return true;

		// 没有可靠 bounds 的物体保守保留，避免因为导入/运行时 bounds 缺失导致阴影被裁掉。
		if (!renderItem.HasLocalBounds)
			return true;

		const DirectX::BoundingSphere worldSphere = BuildWorldBoundingSphere(renderItem);
		DirectX::XMFLOAT3 centerLS{};
		DirectX::XMStoreFloat3(
			&centerLS,
			DirectX::XMVector3TransformCoord(
				DirectX::XMLoadFloat3(&worldSphere.Center),
				DirectX::XMLoadFloat4x4(&shadowEntry.View)));

		DirectX::XMFLOAT3 boundsMin = shadowEntry.LightSpaceCullMin;
		DirectX::XMFLOAT3 boundsMax = shadowEntry.LightSpaceCullMax;

		const float extentX = (std::max)(0.0f, boundsMax.x - boundsMin.x);
		const float extentY = (std::max)(0.0f, boundsMax.y - boundsMin.y);
		const float extentZ = (std::max)(0.0f, boundsMax.z - boundsMin.z);
		const float maxExtent = (std::max)(extentX, (std::max)(extentY, extentZ));
		const float conservativePadding = 0.50f + maxExtent * 0.025f;
		boundsMin.x -= conservativePadding;
		boundsMin.y -= conservativePadding;
		boundsMin.z -= conservativePadding;
		boundsMax.x += conservativePadding;
		boundsMax.y += conservativePadding;
		boundsMax.z += conservativePadding;

		float distanceSq = 0.0f;
		const auto accumulateAxisDistanceSq = [&distanceSq](float value, float minValue, float maxValue)
		{
			if (value < minValue)
			{
				const float delta = minValue - value;
				distanceSq += delta * delta;
			}
			else if (value > maxValue)
			{
				const float delta = value - maxValue;
				distanceSq += delta * delta;
			}
		};

		accumulateAxisDistanceSq(centerLS.x, boundsMin.x, boundsMax.x);
		accumulateAxisDistanceSq(centerLS.y, boundsMin.y, boundsMax.y);
		accumulateAxisDistanceSq(centerLS.z, boundsMin.z, boundsMax.z);

		float radius = (std::max)(worldSphere.Radius, 0.0f);
		if (renderItem.IsSkinned)
			radius = radius * 1.25f + 0.50f;
		return distanceSq <= radius * radius;
	}
};
