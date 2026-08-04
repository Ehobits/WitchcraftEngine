#pragma once

#include <cstdint>
#include <xstring>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>

namespace Witchcraft::Animation
{
	inline DirectX::XMFLOAT4X4 MakeIdentityFloat4x4()
	{
		return DirectX::XMFLOAT4X4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
	}

	struct BoneLocalPose
	{
		DirectX::XMFLOAT3 Translation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4 Rotation = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		DirectX::XMFLOAT3 Scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
		DirectX::XMFLOAT4X4 Matrix = MakeIdentityFloat4x4();
		bool HasMatrix = false;
	};

	inline DirectX::XMFLOAT4X4 ComposeBoneLocalPoseMatrix(const BoneLocalPose& pose)
	{
		DirectX::XMFLOAT4X4 matrix = MakeIdentityFloat4x4();
		const DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&pose.Scale);
		const DirectX::XMVECTOR rotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&pose.Rotation));
		const DirectX::XMVECTOR translation = DirectX::XMLoadFloat3(&pose.Translation);
		DirectX::XMStoreFloat4x4(
			&matrix,
			DirectX::XMMatrixAffineTransformation(scale, DirectX::XMVectorZero(), rotation, translation));
		return matrix;
	}

	struct SkeletonBone
	{
		std::wstring Name;
		std::int32_t ParentIndex = -1;
		BoneLocalPose BindLocalPose;
		DirectX::XMFLOAT4X4 BindGlobalMatrix = MakeIdentityFloat4x4();
		DirectX::XMFLOAT4X4 InverseBindPose = MakeIdentityFloat4x4();
	};

	struct SkeletonTopology
	{
		std::vector<SkeletonBone> Bones;
		std::unordered_map<std::wstring, std::uint32_t> BoneNameToIndex;
		std::int32_t RootBoneIndex = -1;

		void RebuildNameToIndexMap()
		{
			BoneNameToIndex.clear();
			for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(Bones.size()); ++boneIndex)
			{
				BoneNameToIndex[Bones[boneIndex].Name] = boneIndex;
			}
		}

		bool IsValidBoneIndex(std::int32_t boneIndex) const
		{
			return boneIndex >= 0 && boneIndex < static_cast<std::int32_t>(Bones.size());
		}
	};

	struct SkeletonData
	{
		std::wstring SkeletonAssetPath;
		SkeletonTopology Topology;
		std::vector<std::wstring> BoneNames;
		std::vector<BoneLocalPose> LocalPose;
		std::vector<DirectX::XMFLOAT4X4> LocalMatrixPose;
		std::vector<DirectX::XMFLOAT4X4> GlobalPose;
		bool Dirty = true;
	};

	inline void PopulateSkeletonDataFromTopology(
		const SkeletonTopology& topology,
		SkeletonData* outData)
	{
		if (outData == nullptr)
			return;

		outData->Topology = topology;
		outData->BoneNames.clear();
		outData->LocalPose.clear();
		outData->LocalMatrixPose.clear();
		outData->GlobalPose.clear();

		outData->BoneNames.reserve(topology.Bones.size());
		outData->LocalPose.reserve(topology.Bones.size());
		outData->LocalMatrixPose.reserve(topology.Bones.size());
		outData->GlobalPose.reserve(topology.Bones.size());

		for (const SkeletonBone& bone : topology.Bones)
		{
			outData->BoneNames.push_back(bone.Name);
			outData->LocalPose.push_back(bone.BindLocalPose);
			outData->LocalMatrixPose.push_back(
				bone.BindLocalPose.HasMatrix
				? bone.BindLocalPose.Matrix
				: ComposeBoneLocalPoseMatrix(bone.BindLocalPose));
			outData->GlobalPose.push_back(bone.BindGlobalMatrix);
		}

		outData->Dirty = false;
	}
}
