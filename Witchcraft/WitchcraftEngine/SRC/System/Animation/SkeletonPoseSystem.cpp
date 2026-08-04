#include "System/Animation/SkeletonPoseSystem.h"
#include "Helpers/MathHelpers.h"

#include <cstdint>
#include <functional>

using namespace DirectX;

bool SkeletonPoseSystem::IsFiniteFloat(float value)
{
	return std::isfinite(value);
}

bool SkeletonPoseSystem::IsFiniteFloat3(const DirectX::XMFLOAT3& value)
{
	return IsFiniteFloat(value.x) && IsFiniteFloat(value.y) && IsFiniteFloat(value.z);
}

bool SkeletonPoseSystem::IsFiniteFloat4(const DirectX::XMFLOAT4& value)
{
	return IsFiniteFloat(value.x) && IsFiniteFloat(value.y) && IsFiniteFloat(value.z) && IsFiniteFloat(value.w);
}

bool SkeletonPoseSystem::IsFiniteMatrix(const DirectX::XMFLOAT4X4& value)
{
	return
		IsFiniteFloat(value._11) && IsFiniteFloat(value._12) && IsFiniteFloat(value._13) && IsFiniteFloat(value._14) &&
		IsFiniteFloat(value._21) && IsFiniteFloat(value._22) && IsFiniteFloat(value._23) && IsFiniteFloat(value._24) &&
		IsFiniteFloat(value._31) && IsFiniteFloat(value._32) && IsFiniteFloat(value._33) && IsFiniteFloat(value._34) &&
		IsFiniteFloat(value._41) && IsFiniteFloat(value._42) && IsFiniteFloat(value._43) && IsFiniteFloat(value._44);
}

aiMatrix4x4 SkeletonPoseSystem::ComposeBoneMatrix(const Witchcraft::Animation::BoneLocalPose& pose)
{
	const DirectX::XMFLOAT3 sanitizedScale = IsFiniteFloat3(pose.Scale)
		? pose.Scale
		: DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	const DirectX::XMFLOAT3 sanitizedTranslation = IsFiniteFloat3(pose.Translation)
		? pose.Translation
		: DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

	DirectX::XMFLOAT4 sanitizedRotation = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	if (IsFiniteFloat4(pose.Rotation))
	{
		const XMVECTOR loadedRotation = XMLoadFloat4(&pose.Rotation);
		const float rotationLengthSq = XMVectorGetX(XMQuaternionLengthSq(loadedRotation));
		if (std::isfinite(rotationLengthSq) && rotationLengthSq > 0.000001f)
			XMStoreFloat4(&sanitizedRotation, XMQuaternionNormalize(loadedRotation));
	}

	aiMatrix4x4 scaleMatrix;
	aiMatrix4x4::Scaling(aiVector3D(sanitizedScale.x, sanitizedScale.y, sanitizedScale.z), scaleMatrix);

	const aiQuaternion rotationQuaternion(
		sanitizedRotation.w,
		sanitizedRotation.x,
		sanitizedRotation.y,
		sanitizedRotation.z);
	const aiMatrix4x4 rotationMatrix = aiMatrix4x4(rotationQuaternion.GetMatrix());

	aiMatrix4x4 translationMatrix;
	aiMatrix4x4::Translation(
		aiVector3D(sanitizedTranslation.x, sanitizedTranslation.y, sanitizedTranslation.z),
		translationMatrix);

	return translationMatrix * rotationMatrix * scaleMatrix;
}

aiMatrix4x4 SkeletonPoseSystem::MultiplyMatrix(
	const aiMatrix4x4& lhs,
	const aiMatrix4x4& rhs)
{
	return lhs * rhs;
}

void SkeletonPoseSystem::UpdateGlobalPose(
	Witchcraft::Animation::SkeletonData* skeletonData,
	const Witchcraft::Animation::SkeletonTopology& topology) const
{
	if (skeletonData == nullptr)
		return;

	const auto& localPose = skeletonData->LocalPose;
	const auto& localMatrixPose = skeletonData->LocalMatrixPose;
	if (localPose.size() != topology.Bones.size())
		return;

	std::vector<DirectX::XMFLOAT4X4> globalPose(topology.Bones.size(), Witchcraft::Animation::MakeIdentityFloat4x4());
	std::vector<aiMatrix4x4> globalPoseAi(topology.Bones.size(), aiMatrix4x4());
	enum class EvaluationState : std::uint8_t { Unvisited, Visiting, Complete };
	std::vector<EvaluationState> evaluationStates(topology.Bones.size(), EvaluationState::Unvisited);

	std::function<void(std::uint32_t)> evaluateBone = [&](std::uint32_t boneIndex)
	{
		if (evaluationStates[boneIndex] == EvaluationState::Complete)
			return;
		if (evaluationStates[boneIndex] == EvaluationState::Visiting)
			return;

		evaluationStates[boneIndex] = EvaluationState::Visiting;
		aiMatrix4x4 localMatrix = ComposeBoneMatrix(localPose[boneIndex]);
		if (boneIndex < localMatrixPose.size() && IsFiniteMatrix(localMatrixPose[boneIndex]))
			localMatrix = MathHelps::ConvertFloat4x4ToAiMatrix(localMatrixPose[boneIndex]);
		else if (localPose[boneIndex].HasMatrix && IsFiniteMatrix(localPose[boneIndex].Matrix))
			localMatrix = MathHelps::ConvertFloat4x4ToAiMatrix(localPose[boneIndex].Matrix);

		const std::int32_t parentIndex = topology.Bones[boneIndex].ParentIndex;
		if (parentIndex >= 0 && parentIndex < static_cast<std::int32_t>(globalPose.size()))
		{
			evaluateBone(static_cast<std::uint32_t>(parentIndex));
			if (evaluationStates[static_cast<std::uint32_t>(parentIndex)] == EvaluationState::Complete)
				globalPoseAi[boneIndex] = MultiplyMatrix(globalPoseAi[static_cast<std::uint32_t>(parentIndex)], localMatrix);
			else
				globalPoseAi[boneIndex] = localMatrix;
		}
		else
		{
			globalPoseAi[boneIndex] = localMatrix;
		}

		globalPose[boneIndex] = MathHelps::ConvertAiMatrixToFloat4x4(globalPoseAi[boneIndex]);
		evaluationStates[boneIndex] = EvaluationState::Complete;
	};

	for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(topology.Bones.size()); ++boneIndex)
		evaluateBone(boneIndex);

	skeletonData->GlobalPose = std::move(globalPose);
}
