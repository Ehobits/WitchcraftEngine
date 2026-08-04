#include "System/Animation/Assets/SkeletonAsset.h"

namespace Witchcraft::Animation
{
	void SkeletonAsset::SetName(const std::wstring& name)
	{
		m_name = name;
	}

	const std::wstring& SkeletonAsset::GetName() const
	{
		return m_name;
	}

	SkeletonTopology& SkeletonAsset::GetTopology()
	{
		return m_topology;
	}

	const SkeletonTopology& SkeletonAsset::GetTopology() const
	{
		return m_topology;
	}

	void SkeletonAsset::RebuildCaches()
	{
		m_topology.RebuildNameToIndexMap();
		if (m_topology.RootBoneIndex >= static_cast<std::int32_t>(m_topology.Bones.size()))
			m_topology.RootBoneIndex = -1;

		if (m_topology.RootBoneIndex < 0)
		{
			for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(m_topology.Bones.size()); ++boneIndex)
			{
				if (m_topology.Bones[boneIndex].ParentIndex < 0)
				{
					m_topology.RootBoneIndex = static_cast<std::int32_t>(boneIndex);
					break;
				}
			}
		}
	}

	bool SkeletonAsset::IsValid() const
	{
		if (m_topology.Bones.empty())
			return false;

		for (const SkeletonBone& bone : m_topology.Bones)
		{
			if (bone.Name.empty())
				return false;
			if (bone.ParentIndex >= static_cast<std::int32_t>(m_topology.Bones.size()))
				return false;
		}

		return true;
	}

	std::uint32_t SkeletonAsset::GetBoneCount() const
	{
		return static_cast<std::uint32_t>(m_topology.Bones.size());
	}

	std::int32_t SkeletonAsset::FindBoneIndexByName(const std::wstring& boneName) const
	{
		const auto it = m_topology.BoneNameToIndex.find(boneName);
		if (it == m_topology.BoneNameToIndex.end())
			return -1;

		return static_cast<std::int32_t>(it->second);
	}
}
