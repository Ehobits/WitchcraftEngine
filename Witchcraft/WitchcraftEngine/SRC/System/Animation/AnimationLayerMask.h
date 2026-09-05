#pragma once

#include <cstddef>
#include <vector>
#include <xstring>

#include "Common/SkeletonSharedTypes.h"

namespace Witchcraft::Animation
{
	bool BuildAnimationLayerBoneMask(
		const SkeletonTopology& topology,
		const std::wstring& maskRootBoneName,
		std::vector<bool>* outMask);

	bool IsAnimationLayerMaskRootValid(
		const SkeletonTopology& topology,
		const std::wstring& maskRootBoneName);

	std::size_t CountAnimationLayerMaskedBones(
		const SkeletonTopology& topology,
		const std::wstring& maskRootBoneName);
}
