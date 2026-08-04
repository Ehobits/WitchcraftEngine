#include "ShadowPoolPlanner.h"

#include <algorithm>
#include <cmath>

namespace ShadowPoolPlanner {

	UINT ResolveShadowSlotCount(const D3DWindowShadowConfig& shadowConfig, const Light& light)
	{
		if (!light.CastShadow)
			return 0;

		const UINT directionalLightType = static_cast<UINT>(std::lround(shadowConfig.DirectionalLightType));
		const UINT pointLightType = static_cast<UINT>(std::lround(shadowConfig.PointLightType));
		const UINT spotLightType = static_cast<UINT>(std::lround(shadowConfig.SpotLightType));
		const UINT resolvedLightType = static_cast<UINT>(std::lround(light.Type));

		if (resolvedLightType == directionalLightType)
			return std::clamp(shadowConfig.DirectionalCascadeCount, 1u, 3u);
		if (resolvedLightType == pointLightType)
			return 1u;  // 点光源使用 TextureCube，单个槽位包含 6 面
		if (resolvedLightType == spotLightType)
			return 1u;
		return 0;
	}

	PoolKind ResolvePoolKind(const D3DWindowShadowConfig& shadowConfig, const Light& light)
	{
		const UINT directionalLightType = static_cast<UINT>(std::lround(shadowConfig.DirectionalLightType));
		const UINT pointLightType = static_cast<UINT>(std::lround(shadowConfig.PointLightType));
		const UINT spotLightType = static_cast<UINT>(std::lround(shadowConfig.SpotLightType));
		const UINT resolvedLightType = static_cast<UINT>(std::lround(light.Type));

		if (resolvedLightType == directionalLightType)
			return PoolKind::Directional;
		if (resolvedLightType == pointLightType)
			return PoolKind::Point;
		if (resolvedLightType == spotLightType)
			return PoolKind::Spot;
		return PoolKind::None;
	}

	void AssignPool(const std::vector<PendingAssignment>& pendingAssignments, PoolKind targetPool, UINT maxShadowMapCount, PoolRange* poolRange, std::vector<LightAssignment>* lightAssignments, UINT* nextSlot)
	{
		if (poolRange == nullptr || lightAssignments == nullptr || nextSlot == nullptr)
			return;

		poolRange->StartSlot = *nextSlot;
		poolRange->ReservedSlotCount = 0;
		poolRange->UsedSlotCount = 0;

		for (const PendingAssignment& pending : pendingAssignments)
		{
			if (pending.Pool != targetPool || pending.SlotCount == 0)
				continue;

			poolRange->ReservedSlotCount += pending.SlotCount;
			if (*nextSlot + pending.SlotCount > maxShadowMapCount)
				continue;

			LightAssignment& assignment = (*lightAssignments)[pending.LightIndex];
			assignment.BaseShadowMapIndex = static_cast<int>(*nextSlot);
			assignment.SlotCount = pending.SlotCount;
			assignment.Pool = targetPool;

			*nextSlot += pending.SlotCount;
			poolRange->UsedSlotCount += pending.SlotCount;
		}
	}

	ShadowPoolPlan BuildShadowPoolPlan(
		const D3DWindowShadowConfig& shadowConfig,
		const std::vector<Light>& lightsCache,
		UINT maxShadowMapCount)
	{
		ShadowPoolPlan plan;
		plan.LightAssignments.resize(lightsCache.size());

		std::vector<PendingAssignment> pendingAssignments;
		pendingAssignments.reserve(lightsCache.size());

		for (UINT lightIndex = 0; lightIndex < lightsCache.size(); ++lightIndex)
		{
			const Light& light = lightsCache[lightIndex];

			PendingAssignment pending;
			pending.LightIndex = lightIndex;
			pending.SlotCount = ResolveShadowSlotCount(shadowConfig, light);
			pending.Pool = ResolvePoolKind(shadowConfig, light);
			pendingAssignments.push_back(pending);
		}

		UINT nextSlot = 0;
		// 逻辑 shadow slot 顺序需要和实际 SRV 物理布局保持一致：
		// 1) Directional/Spot 走 Texture2D
		// 2) Point 走 TextureCube
		// 如果这里把 Point 排在 Spot 前面，CPU 侧的全局 ShadowSlotIndex
		// 就会和 shadow SRV 堆中的真实资源顺序错位。
		AssignPool(
			pendingAssignments,
			PoolKind::Directional,
			maxShadowMapCount,
			&plan.DirectionalPool,
			&plan.LightAssignments,
			&nextSlot);
		AssignPool(
			pendingAssignments,
			PoolKind::Spot,
			maxShadowMapCount,
			&plan.SpotPool,
			&plan.LightAssignments,
			&nextSlot);
		AssignPool(
			pendingAssignments,
			PoolKind::Point,
			maxShadowMapCount,
			&plan.PointPool,
			&plan.LightAssignments,
			&nextSlot);

		plan.TotalAssignedShadowSlotCount = nextSlot;
		return plan;
	}
}
