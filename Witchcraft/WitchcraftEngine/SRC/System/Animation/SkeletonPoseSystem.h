#pragma once

#include <assimp/matrix4x4.h>
#include "Common/SkeletonSharedTypes.h"

namespace Witchcraft::Animation { class SkeletonAsset; }

class SkeletonPoseSystem
{
public:
	void UpdateGlobalPose(
		Witchcraft::Animation::SkeletonData* skeletonData,
		const Witchcraft::Animation::SkeletonTopology& topology) const;

private:
	static bool IsFiniteFloat(float value);
	static bool IsFiniteFloat3(const DirectX::XMFLOAT3& value);
	static bool IsFiniteFloat4(const DirectX::XMFLOAT4& value);
	static bool IsFiniteMatrix(const DirectX::XMFLOAT4X4& value);
	static aiMatrix4x4 ComposeBoneMatrix(const Witchcraft::Animation::BoneLocalPose& pose);
	static aiMatrix4x4 MultiplyMatrix(
		const aiMatrix4x4& lhs,
		const aiMatrix4x4& rhs);
};
