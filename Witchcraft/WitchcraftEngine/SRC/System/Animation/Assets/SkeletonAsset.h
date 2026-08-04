#pragma once

#include "Common/SkeletonSharedTypes.h"

namespace Witchcraft::Animation
{
	class SkeletonAsset
	{
	public:
		SkeletonAsset() = default;

		void SetName(const std::wstring& name);
		const std::wstring& GetName() const;

		SkeletonTopology& GetTopology();
		const SkeletonTopology& GetTopology() const;

		void RebuildCaches();
		bool IsValid() const;
		std::uint32_t GetBoneCount() const;
		std::int32_t FindBoneIndexByName(const std::wstring& boneName) const;

	private:
		std::wstring m_name;
		SkeletonTopology m_topology;
	};
}
