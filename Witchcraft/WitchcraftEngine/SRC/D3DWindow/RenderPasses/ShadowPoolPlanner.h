#pragma once

#include "../Light.h"
#include "ShadowFrameBuilder.h"

#include <vector>

namespace ShadowPoolPlanner
{
	enum class PoolKind : UINT
	{
		None = 0,
		Directional,
		Point,
		Spot
	};

	struct PoolRange
	{
		UINT StartSlot = 0;
		UINT ReservedSlotCount = 0;
		UINT UsedSlotCount = 0;
	};

	struct LightAssignment
	{
		int BaseShadowMapIndex = -1;
		UINT SlotCount = 0;
		PoolKind Pool = PoolKind::None;
	};

	struct ShadowPoolPlan
	{
		std::vector<LightAssignment> LightAssignments;
		PoolRange DirectionalPool;
		PoolRange PointPool;
		PoolRange SpotPool;
		UINT TotalAssignedShadowSlotCount = 0;
	};

	struct PendingAssignment
	{
		UINT LightIndex = 0;
		UINT SlotCount = 0;
		PoolKind Pool = PoolKind::None;
	};

	UINT ResolveShadowSlotCount(
		const D3DWindowShadowConfig& shadowConfig,
		const Light& light);

	PoolKind ResolvePoolKind(
		const D3DWindowShadowConfig& shadowConfig,
		const Light& light);

	void AssignPool(
		const std::vector<PendingAssignment>& pendingAssignments,
		PoolKind targetPool,
		UINT maxShadowMapCount,
		PoolRange* poolRange,
		std::vector<LightAssignment>* lightAssignments,
		UINT* nextSlot);

	// 第一阶段只负责“槽位怎么排”：
	// 把当前扁平 shadow slot 重新组织成方向光 / 点光 / 聚光三段连续区间，
	// 但仍然对外暴露与旧系统一致的 base slot 索引，保证渲染链行为不变。
	ShadowPoolPlan BuildShadowPoolPlan(
		const D3DWindowShadowConfig& shadowConfig,
		const std::vector<Light>& lightsCache,
		UINT maxShadowMapCount);
}
