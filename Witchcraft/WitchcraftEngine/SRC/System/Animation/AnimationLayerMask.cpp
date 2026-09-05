#include "System/Animation/AnimationLayerMask.h"

#include <algorithm>

namespace Witchcraft::Animation
{
	bool BuildAnimationLayerBoneMask(
		const SkeletonTopology& topology,
		const std::wstring& maskRootBoneName,
		std::vector<bool>* outMask)
	{
		if (outMask == nullptr || topology.Bones.empty())
			return false;

		outMask->assign(topology.Bones.size(), false);
		if (maskRootBoneName.empty())
		{
			std::fill(outMask->begin(), outMask->end(), true);
			return true;
		}

		const auto rootIt = topology.BoneNameToIndex.find(maskRootBoneName);
		if (rootIt == topology.BoneNameToIndex.end())
			return false;

		const std::int32_t rootBoneIndex = static_cast<std::int32_t>(rootIt->second);
		for (std::size_t boneIndex = 0; boneIndex < topology.Bones.size(); ++boneIndex)
		{
			std::int32_t currentBoneIndex = static_cast<std::int32_t>(boneIndex);
			while (currentBoneIndex >= 0 && currentBoneIndex < static_cast<std::int32_t>(topology.Bones.size()))
			{
				if (currentBoneIndex == rootBoneIndex)
				{
					(*outMask)[boneIndex] = true;
					break;
				}
				currentBoneIndex = topology.Bones[static_cast<std::size_t>(currentBoneIndex)].ParentIndex;
			}
		}

		return true;
	}

	bool IsAnimationLayerMaskRootValid(
		const SkeletonTopology& topology,
		const std::wstring& maskRootBoneName)
	{
		if (topology.Bones.empty())
			return false;
		return maskRootBoneName.empty() ||
			topology.BoneNameToIndex.find(maskRootBoneName) != topology.BoneNameToIndex.end();
	}

	std::size_t CountAnimationLayerMaskedBones(
		const SkeletonTopology& topology,
		const std::wstring& maskRootBoneName)
	{
		std::vector<bool> mask;
		if (!BuildAnimationLayerBoneMask(topology, maskRootBoneName, &mask))
			return 0;

		return static_cast<std::size_t>(std::count(mask.begin(), mask.end(), true));
	}
}
