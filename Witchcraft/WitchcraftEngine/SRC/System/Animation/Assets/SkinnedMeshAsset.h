#pragma once

#include <xstring>
#include <vector>

#include "Common/SkinningSharedTypes.h"

namespace Witchcraft::Animation
{
	class SkinnedMeshAsset
	{
	public:
		SkinnedMeshAsset() = default;

		void SetName(const std::wstring& name);
		const std::wstring& GetName() const;

		void SetSkeletonAssetPath(const std::wstring& skeletonAssetPath);
		const std::wstring& GetSkeletonAssetPath() const;

		std::vector<SkinnedVertex>& GetVertices();
		const std::vector<SkinnedVertex>& GetVertices() const;

		std::vector<std::uint32_t>& GetIndices();
		const std::vector<std::uint32_t>& GetIndices() const;

		std::vector<SkinnedSubmeshDesc>& GetSubmeshes();
		const std::vector<SkinnedSubmeshDesc>& GetSubmeshes() const;

		bool IsValid() const;
		void NormalizeWeights();

	private:
		std::wstring m_name;
		std::wstring m_skeletonAssetPath;
		std::vector<SkinnedVertex> m_vertices;
		std::vector<std::uint32_t> m_indices;
		std::vector<SkinnedSubmeshDesc> m_submeshes;
	};
}
