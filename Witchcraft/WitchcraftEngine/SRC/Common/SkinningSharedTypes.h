#pragma once

#include <array>
#include <cstdint>
#include <xstring>
#include <vector>

#include <DirectXMath.h>

#include "Common/MeshSharedTypes.h"

namespace Witchcraft::Animation
{
	constexpr std::uint32_t MaxBoneInfluenceCountPerVertex = 4;

	struct VertexBoneInfluence4
	{
		std::array<std::uint32_t, MaxBoneInfluenceCountPerVertex> BoneIndices = { 0u, 0u, 0u, 0u };
		std::array<float, MaxBoneInfluenceCountPerVertex> BoneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };

		void Normalize()
		{
			float weightSum = 0.0f;
			for (float weight : BoneWeights)
				weightSum += weight;

			if (weightSum <= 0.00001f)
			{
				BoneIndices = { 0u, 0u, 0u, 0u };
				BoneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
				return;
			}

			for (float& weight : BoneWeights)
				weight /= weightSum;
		}
	};

	struct SkinnedSubmeshDesc
	{
		std::wstring Name;
		std::wstring MaterialSlotName;
		std::uint32_t IndexStart = 0;
		std::uint32_t IndexCount = 0;
		std::uint32_t BaseVertex = 0;
	};

	struct SkinnedVertex
	{
		Vertex StaticVertex = {};
		VertexBoneInfluence4 Skinning = {};
	};

	struct SkinningPalette
	{
		std::vector<DirectX::XMFLOAT4X4> FinalBoneMatrices;
		std::uint64_t Revision = 0;
	};
}
