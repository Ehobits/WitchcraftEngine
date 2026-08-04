#include "System/Animation/Assets/SkinnedMeshAsset.h"

namespace Witchcraft::Animation
{
	void SkinnedMeshAsset::SetName(const std::wstring& name)
	{
		m_name = name;
	}

	const std::wstring& SkinnedMeshAsset::GetName() const
	{
		return m_name;
	}

	void SkinnedMeshAsset::SetSkeletonAssetPath(const std::wstring& skeletonAssetPath)
	{
		m_skeletonAssetPath = skeletonAssetPath;
	}

	const std::wstring& SkinnedMeshAsset::GetSkeletonAssetPath() const
	{
		return m_skeletonAssetPath;
	}

	std::vector<SkinnedVertex>& SkinnedMeshAsset::GetVertices()
	{
		return m_vertices;
	}

	const std::vector<SkinnedVertex>& SkinnedMeshAsset::GetVertices() const
	{
		return m_vertices;
	}

	std::vector<std::uint32_t>& SkinnedMeshAsset::GetIndices()
	{
		return m_indices;
	}

	const std::vector<std::uint32_t>& SkinnedMeshAsset::GetIndices() const
	{
		return m_indices;
	}

	std::vector<SkinnedSubmeshDesc>& SkinnedMeshAsset::GetSubmeshes()
	{
		return m_submeshes;
	}

	const std::vector<SkinnedSubmeshDesc>& SkinnedMeshAsset::GetSubmeshes() const
	{
		return m_submeshes;
	}

	bool SkinnedMeshAsset::IsValid() const
	{
		return !m_vertices.empty() && !m_indices.empty() && !m_skeletonAssetPath.empty();
	}

	void SkinnedMeshAsset::NormalizeWeights()
	{
		for (SkinnedVertex& vertex : m_vertices)
			vertex.Skinning.Normalize();
	}
}
