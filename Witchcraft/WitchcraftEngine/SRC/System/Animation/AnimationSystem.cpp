#include "System/Animation/AnimationSystem.h"

#include "ECS/Component/BoneAttachmentComponent.h"
#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/SkinningRuntimeComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/WitchcraECS.h"
#include "Engine/EngineUtils.h"
#include "System/WitchcraftFile/WAnimationFile.h"
#include "System/WitchcraftFile/WSkeletonFile.h"

using namespace DirectX;

namespace AnimationSystemDetail
{
	constexpr float kAnimationDefaultValueEpsilon = 1e-4f;

	bool IsFiniteAnimationFloat(float value)
	{
		return std::isfinite(value);
	}

	bool IsFiniteAnimationFloat3(const DirectX::XMFLOAT3& value)
	{
		return IsFiniteAnimationFloat(value.x) &&
			IsFiniteAnimationFloat(value.y) &&
			IsFiniteAnimationFloat(value.z);
	}

	bool IsFiniteAnimationFloat4(const DirectX::XMFLOAT4& value)
	{
		return IsFiniteAnimationFloat(value.x) &&
			IsFiniteAnimationFloat(value.y) &&
			IsFiniteAnimationFloat(value.z) &&
			IsFiniteAnimationFloat(value.w);
	}

	bool IsFiniteAnimationMatrix(const DirectX::XMFLOAT4X4& value)
	{
		return
			IsFiniteAnimationFloat(value._11) && IsFiniteAnimationFloat(value._12) && IsFiniteAnimationFloat(value._13) && IsFiniteAnimationFloat(value._14) &&
			IsFiniteAnimationFloat(value._21) && IsFiniteAnimationFloat(value._22) && IsFiniteAnimationFloat(value._23) && IsFiniteAnimationFloat(value._24) &&
			IsFiniteAnimationFloat(value._31) && IsFiniteAnimationFloat(value._32) && IsFiniteAnimationFloat(value._33) && IsFiniteAnimationFloat(value._34) &&
			IsFiniteAnimationFloat(value._41) && IsFiniteAnimationFloat(value._42) && IsFiniteAnimationFloat(value._43) && IsFiniteAnimationFloat(value._44);
	}

	DirectX::XMFLOAT4X4 ComposeAnimationLocalPoseMatrix(const Witchcraft::Animation::BoneLocalPose& pose)
	{
		DirectX::XMFLOAT4X4 result = Witchcraft::Animation::MakeIdentityFloat4x4();
		const DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&pose.Scale);
		const DirectX::XMVECTOR rotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&pose.Rotation));
		const DirectX::XMVECTOR translation = DirectX::XMLoadFloat3(&pose.Translation);
		DirectX::XMStoreFloat4x4(
			&result,
			DirectX::XMMatrixAffineTransformation(scale, DirectX::XMVectorZero(), rotation, translation));
		return IsFiniteAnimationMatrix(result) ? result : Witchcraft::Animation::MakeIdentityFloat4x4();
	}

	bool TryDecomposeAnimationMatrixToBoneLocalPose(
		const DirectX::XMFLOAT4X4& matrixValue,
		Witchcraft::Animation::BoneLocalPose* outPose)
	{
		if (outPose == nullptr || !IsFiniteAnimationMatrix(matrixValue))
			return false;

		const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&matrixValue);
		DirectX::XMVECTOR scaleVector;
		DirectX::XMVECTOR rotationQuaternion;
		DirectX::XMVECTOR translationVector;
		if (!DirectX::XMMatrixDecompose(&scaleVector, &rotationQuaternion, &translationVector, matrix))
			return false;

		DirectX::XMStoreFloat3(&outPose->Scale, scaleVector);
		DirectX::XMStoreFloat4(&outPose->Rotation, DirectX::XMQuaternionNormalize(rotationQuaternion));
		DirectX::XMStoreFloat3(&outPose->Translation, translationVector);
		outPose->Matrix = matrixValue;
		outPose->HasMatrix = true;
		return
			IsFiniteAnimationFloat3(outPose->Translation) &&
			IsFiniteAnimationFloat4(outPose->Rotation) &&
			IsFiniteAnimationFloat3(outPose->Scale);
	}

	DirectX::XMFLOAT4X4 TransposeAnimationMatrixValue(const DirectX::XMFLOAT4X4& matrixValue)
	{
		DirectX::XMFLOAT4X4 result = Witchcraft::Animation::MakeIdentityFloat4x4();
		DirectX::XMStoreFloat4x4(
			&result,
			DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&matrixValue)));
		return result;
	}

	bool LooksLikeLegacyImportedMatrix(const DirectX::XMFLOAT4X4& matrixValue)
	{
		const float translationInLastColumn =
			std::abs(matrixValue._14) +
			std::abs(matrixValue._24) +
			std::abs(matrixValue._34);
		const float translationInLastRow =
			std::abs(matrixValue._41) +
			std::abs(matrixValue._42) +
			std::abs(matrixValue._43);
		const bool affineLastRow =
			std::abs(matrixValue._41) <= 1e-5f &&
			std::abs(matrixValue._42) <= 1e-5f &&
			std::abs(matrixValue._43) <= 1e-5f &&
			std::abs(matrixValue._44 - 1.0f) <= 1e-5f;
		return affineLastRow && translationInLastColumn > 1e-5f && translationInLastRow <= 1e-5f;
	}

	void NormalizeLegacySkeletonTopologyMatrices(
		Witchcraft::Animation::SkeletonTopology* topology,
		const std::wstring& assetPath)
	{
		if (topology == nullptr || topology->Bones.empty())
			return;

		bool shouldNormalize = false;
		for (const auto& bone : topology->Bones)
		{
			if (LooksLikeLegacyImportedMatrix(bone.BindGlobalMatrix) ||
				LooksLikeLegacyImportedMatrix(bone.InverseBindPose))
			{
				shouldNormalize = true;
				break;
			}
		}

		if (!shouldNormalize)
			return;

		for (auto& bone : topology->Bones)
		{
			bone.BindGlobalMatrix = TransposeAnimationMatrixValue(bone.BindGlobalMatrix);
			bone.InverseBindPose = TransposeAnimationMatrixValue(bone.InverseBindPose);
		}

	}

	bool NearlyEqualAnimationFloat(float lhs, float rhs)
	{
		return std::fabs(lhs - rhs) <= kAnimationDefaultValueEpsilon;
	}

	bool IsDefaultTranslationValue(const DirectX::XMFLOAT3& value)
	{
		return NearlyEqualAnimationFloat(value.x, 0.0f) &&
			NearlyEqualAnimationFloat(value.y, 0.0f) &&
			NearlyEqualAnimationFloat(value.z, 0.0f);
	}

	bool IsDefaultRotationValue(const DirectX::XMFLOAT4& value)
	{
		return NearlyEqualAnimationFloat(value.x, 0.0f) &&
			NearlyEqualAnimationFloat(value.y, 0.0f) &&
			NearlyEqualAnimationFloat(value.z, 0.0f) &&
			NearlyEqualAnimationFloat(value.w, 1.0f);
	}

	bool IsDefaultScaleValue(const DirectX::XMFLOAT3& value)
	{
		return NearlyEqualAnimationFloat(value.x, 1.0f) &&
			NearlyEqualAnimationFloat(value.y, 1.0f) &&
			NearlyEqualAnimationFloat(value.z, 1.0f);
	}

	bool IsDefaultTranslationTrack(const Witchcraft::Animation::BoneAnimationTrack& track)
	{
		if (track.TranslationKeys.empty())
			return true;

		for (const auto& key : track.TranslationKeys)
		{
			if (!IsFiniteAnimationFloat(key.Time) || !IsFiniteAnimationFloat3(key.Value))
				continue;
			if (!IsDefaultTranslationValue(key.Value))
				return false;
		}

		return true;
	}

	bool IsDefaultRotationTrack(const Witchcraft::Animation::BoneAnimationTrack& track)
	{
		if (track.RotationKeys.empty())
			return true;

		for (const auto& key : track.RotationKeys)
		{
			if (!IsFiniteAnimationFloat(key.Time) || !IsFiniteAnimationFloat4(key.Value))
				continue;
			if (!IsDefaultRotationValue(key.Value))
				return false;
		}

		return true;
	}

	bool IsDefaultScaleTrack(const Witchcraft::Animation::BoneAnimationTrack& track)
	{
		if (track.ScaleKeys.empty())
			return true;

		for (const auto& key : track.ScaleKeys)
		{
			if (!IsFiniteAnimationFloat(key.Time) || !IsFiniteAnimationFloat3(key.Value))
				continue;
			if (!IsDefaultScaleValue(key.Value))
				return false;
		}

		return true;
	}

	template<typename TKey, typename TValue, typename TValueGetter, typename TDefaultPredicate>
	void RemoveDefaultKeysWhenBindNonDefault(
		std::vector<TKey>* keys,
		TValueGetter&& getValue,
		TDefaultPredicate&& isDefaultValue)
	{
		if (keys == nullptr || keys->empty())
			return;

		bool hasAnyNonDefault = false;
		for (const TKey& key : *keys)
		{
			if (!isDefaultValue(getValue(key)))
			{
				hasAnyNonDefault = true;
				break;
			}
		}

		if (!hasAnyNonDefault)
			return;

		std::vector<TKey> sanitizedKeys;
		sanitizedKeys.reserve(keys->size());
		for (const TKey& key : *keys)
		{
			if (!isDefaultValue(getValue(key)))
				sanitizedKeys.push_back(key);
		}

		*keys = std::move(sanitizedKeys);
	}

	void SanitizeTrackComponentsAgainstBindPose(
		const Witchcraft::Animation::BoneLocalPose& bindPose,
		Witchcraft::Animation::BoneAnimationTrack* track)
	{
		if (track == nullptr)
			return;

		if (!IsDefaultTranslationValue(bindPose.Translation))
		{
			if (IsDefaultTranslationTrack(*track))
			{
				track->TranslationKeys.clear();
			}
			else
			{
				RemoveDefaultKeysWhenBindNonDefault<Witchcraft::Animation::BoneTranslationKey, DirectX::XMFLOAT3>(
					&track->TranslationKeys,
					[](const Witchcraft::Animation::BoneTranslationKey& key) -> const DirectX::XMFLOAT3&
					{
						return key.Value;
					},
					[](const DirectX::XMFLOAT3& value)
					{
						return IsDefaultTranslationValue(value);
					});
				if (IsDefaultTranslationTrack(*track))
					track->TranslationKeys.clear();
			}
		}

		if (!IsDefaultRotationValue(bindPose.Rotation))
		{
			if (IsDefaultRotationTrack(*track))
			{
				track->RotationKeys.clear();
			}
			else
			{
				RemoveDefaultKeysWhenBindNonDefault<Witchcraft::Animation::BoneRotationKey, DirectX::XMFLOAT4>(
					&track->RotationKeys,
					[](const Witchcraft::Animation::BoneRotationKey& key) -> const DirectX::XMFLOAT4&
					{
						return key.Value;
					},
					[](const DirectX::XMFLOAT4& value)
					{
						return IsDefaultRotationValue(value);
					});
				if (IsDefaultRotationTrack(*track))
					track->RotationKeys.clear();
			}
		}

		if (!IsDefaultScaleValue(bindPose.Scale))
		{
			if (IsDefaultScaleTrack(*track))
			{
				track->ScaleKeys.clear();
			}
			else
			{
				RemoveDefaultKeysWhenBindNonDefault<Witchcraft::Animation::BoneScaleKey, DirectX::XMFLOAT3>(
					&track->ScaleKeys,
					[](const Witchcraft::Animation::BoneScaleKey& key) -> const DirectX::XMFLOAT3&
					{
						return key.Value;
					},
					[](const DirectX::XMFLOAT3& value)
					{
						return IsDefaultScaleValue(value);
					});
				if (IsDefaultScaleTrack(*track))
					track->ScaleKeys.clear();
			}
		}
	}

	bool IsDefaultBoneLocalPose(const Witchcraft::Animation::BoneLocalPose& pose)
	{
		return IsDefaultTranslationValue(pose.Translation) &&
			IsDefaultRotationValue(pose.Rotation) &&
			IsDefaultScaleValue(pose.Scale);
	}

	bool AreAnimationFloat3NearlyEqual(
		const DirectX::XMFLOAT3& lhs,
		const DirectX::XMFLOAT3& rhs)
	{
		return NearlyEqualAnimationFloat(lhs.x, rhs.x) &&
			NearlyEqualAnimationFloat(lhs.y, rhs.y) &&
			NearlyEqualAnimationFloat(lhs.z, rhs.z);
	}

	bool AreAnimationFloat4NearlyEqual(
		const DirectX::XMFLOAT4& lhs,
		const DirectX::XMFLOAT4& rhs)
	{
		return NearlyEqualAnimationFloat(lhs.x, rhs.x) &&
			NearlyEqualAnimationFloat(lhs.y, rhs.y) &&
			NearlyEqualAnimationFloat(lhs.z, rhs.z) &&
			NearlyEqualAnimationFloat(lhs.w, rhs.w);
	}

	bool AreAnimationQuaternionsEquivalent(
		const DirectX::XMFLOAT4& lhs,
		const DirectX::XMFLOAT4& rhs)
	{
		if (AreAnimationFloat4NearlyEqual(lhs, rhs))
			return true;

		return NearlyEqualAnimationFloat(lhs.x, -rhs.x) &&
			NearlyEqualAnimationFloat(lhs.y, -rhs.y) &&
			NearlyEqualAnimationFloat(lhs.z, -rhs.z) &&
			NearlyEqualAnimationFloat(lhs.w, -rhs.w);
	}

	bool IsAnimationRotationNearIdentity(const DirectX::XMFLOAT4& rotation, float absDotThreshold = 0.9995f)
	{
		if (!IsFiniteAnimationFloat4(rotation))
			return false;

		const DirectX::XMVECTOR rotationQuat = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&rotation));
		const DirectX::XMVECTOR identityQuat = DirectX::XMQuaternionIdentity();
		return std::abs(DirectX::XMVectorGetX(DirectX::XMVector4Dot(rotationQuat, identityQuat))) >= absDotThreshold;
	}

	float ComputeAnimationQuaternionAbsDot(
		const DirectX::XMFLOAT4& lhs,
		const DirectX::XMFLOAT4& rhs)
	{
		if (!IsFiniteAnimationFloat4(lhs) || !IsFiniteAnimationFloat4(rhs))
			return 0.0f;

		const DirectX::XMVECTOR lhsQuat = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&lhs));
		const DirectX::XMVECTOR rhsQuat = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&rhs));
		return std::abs(DirectX::XMVectorGetX(DirectX::XMVector4Dot(lhsQuat, rhsQuat)));
	}

	bool AreBoneLocalPosesNearlyEqual(
		const Witchcraft::Animation::BoneLocalPose& lhs,
		const Witchcraft::Animation::BoneLocalPose& rhs)
	{
		return AreAnimationFloat3NearlyEqual(lhs.Translation, rhs.Translation) &&
			AreAnimationFloat4NearlyEqual(lhs.Rotation, rhs.Rotation) &&
			AreAnimationFloat3NearlyEqual(lhs.Scale, rhs.Scale);
	}

	bool ShouldIgnoreTrackAsInvalidOverride(
		const Witchcraft::Animation::BoneAnimationTrack& track,
		const Witchcraft::Animation::BoneLocalPose& bindPose)
	{
		if (IsDefaultBoneLocalPose(bindPose))
			return false;

		return IsDefaultTranslationTrack(track) &&
			IsDefaultRotationTrack(track) &&
			IsDefaultScaleTrack(track);
	}

	DirectX::XMFLOAT4X4 BuildAnimationRotationMatrix(const DirectX::XMFLOAT4& rotation)
	{
		DirectX::XMFLOAT4X4 result = Witchcraft::Animation::MakeIdentityFloat4x4();
		DirectX::XMVECTOR rotationVector = DirectX::XMLoadFloat4(&rotation);
		const float rotationLengthSq = DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(rotationVector));
		if (!std::isfinite(rotationLengthSq) || rotationLengthSq <= 1e-8f)
			return result;

		rotationVector = DirectX::XMQuaternionNormalize(rotationVector);
		DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixRotationQuaternion(rotationVector));
		return IsFiniteAnimationMatrix(result) ? result : Witchcraft::Animation::MakeIdentityFloat4x4();
	}

	DirectX::XMFLOAT4X4 MultiplyAnimationMatrices(
		const DirectX::XMFLOAT4X4& lhs,
		const DirectX::XMFLOAT4X4& rhs)
	{
		DirectX::XMFLOAT4X4 result = Witchcraft::Animation::MakeIdentityFloat4x4();
		DirectX::XMStoreFloat4x4(
			&result,
			DirectX::XMMatrixMultiply(
				DirectX::XMLoadFloat4x4(&lhs),
				DirectX::XMLoadFloat4x4(&rhs)));
		return IsFiniteAnimationMatrix(result) ? result : Witchcraft::Animation::MakeIdentityFloat4x4();
	}

	DirectX::XMFLOAT4 ExtractAnimationRotationFromMatrix(const DirectX::XMFLOAT4X4& matrixValue)
	{
		const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&matrixValue);
		DirectX::XMVECTOR scaleVector;
		DirectX::XMVECTOR rotationQuaternion;
		DirectX::XMVECTOR translationVector;
		if (!DirectX::XMMatrixDecompose(&scaleVector, &rotationQuaternion, &translationVector, matrix))
			return DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

		DirectX::XMFLOAT4 result = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		DirectX::XMStoreFloat4(&result, DirectX::XMQuaternionNormalize(rotationQuaternion));
		return IsFiniteAnimationFloat4(result) ? result : DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	DirectX::XMFLOAT4 ApplyBindRelativeRotation(
		const DirectX::XMFLOAT4& bindRotation,
		const DirectX::XMFLOAT4& deltaRotation)
	{
		const DirectX::XMFLOAT4X4 bindMatrix = BuildAnimationRotationMatrix(bindRotation);
		const DirectX::XMFLOAT4X4 deltaMatrix = BuildAnimationRotationMatrix(deltaRotation);
		return ExtractAnimationRotationFromMatrix(MultiplyAnimationMatrices(deltaMatrix, bindMatrix));
	}

	bool ShouldTreatRotationTrackAsBindRelative(
		const Witchcraft::Animation::BoneLocalPose& bindPose,
		const Witchcraft::Animation::BoneAnimationTrack& track)
	{
		if (IsDefaultRotationValue(bindPose.Rotation))
			return false;
		if (track.RotationKeys.size() < 2)
			return false;

		const auto& firstKey = track.RotationKeys.front();
		if (IsFiniteAnimationFloat4(firstKey.Value) &&
			IsAnimationRotationNearIdentity(firstKey.Value) &&
			ComputeAnimationQuaternionAbsDot(firstKey.Value, bindPose.Rotation) < 0.95f)
		{
			return true;
		}

		if (ComputeAnimationQuaternionAbsDot(firstKey.Value, bindPose.Rotation) < 0.95f)
			return false;

		const float kEarlyTimeWindow = 0.001f;
		for (size_t keyIndex = 1; keyIndex < track.RotationKeys.size(); ++keyIndex)
		{
			const auto& key = track.RotationKeys[keyIndex];
			if (!IsFiniteAnimationFloat(key.Time) || !IsFiniteAnimationFloat4(key.Value))
				continue;
			if (key.Time > kEarlyTimeWindow)
				break;
			if (ComputeAnimationQuaternionAbsDot(key.Value, bindPose.Rotation) < 0.95f)
				return true;
		}

		return false;
	}

	std::vector<Witchcraft::Animation::BoneRotationKey> BuildBindRelativeRotationKeys(
		const Witchcraft::Animation::BoneAnimationTrack& track,
		const Witchcraft::Animation::BoneLocalPose& bindPose)
	{
		std::vector<Witchcraft::Animation::BoneRotationKey> result;
		result.reserve(track.RotationKeys.size() + 1);

		Witchcraft::Animation::BoneRotationKey identityKey;
		identityKey.Time = 0.0f;
		identityKey.Value = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		result.push_back(identityKey);

		const float kBindDotThreshold = 0.95f;
		bool skippedSentinel = false;
		for (const auto& key : track.RotationKeys)
		{
			if (!IsFiniteAnimationFloat(key.Time) || !IsFiniteAnimationFloat4(key.Value))
				continue;

			if (!skippedSentinel &&
				key.Time <= 0.001f &&
				ComputeAnimationQuaternionAbsDot(key.Value, bindPose.Rotation) >= kBindDotThreshold)
			{
				continue;
			}

			skippedSentinel = true;
			result.push_back(key);
		}

		if (result.size() == 1)
			result.push_back(identityKey);

		return result;
	}

}

using namespace AnimationSystemDetail;

std::filesystem::path AnimationSystem::ResolveAssetPath(const std::wstring& assetPath)
{
	if (assetPath.empty())
		return {};

	std::filesystem::path path(assetPath);
	if (path.is_absolute())
		return path;

	return std::filesystem::path(EngineUtils::GetProjectDirPath()) / path;
}

bool AnimationSystem::TryLoadSkeleton(
	const std::wstring& assetPath,
	SkeletonCacheEntry* outSkeleton)
{
	if (outSkeleton == nullptr || assetPath.empty())
		return false;

	const std::filesystem::path resolvedPath = ResolveAssetPath(assetPath);
	std::error_code fileTimeError;
	const std::filesystem::file_time_type currentWriteTime =
		std::filesystem::last_write_time(resolvedPath, fileTimeError);

	const auto cacheIt = mSkeletonCache.find(assetPath);
	if (cacheIt != mSkeletonCache.end() &&
		(fileTimeError || cacheIt->second.LastWriteTime == currentWriteTime))
	{
		*outSkeleton = cacheIt->second;
		return true;
	}

	WSkeletonFileData fileData;
	if (!WSkeletonFile::LoadFromFile(resolvedPath, &fileData))
		return false;

	NormalizeLegacySkeletonTopologyMatrices(&fileData.Topology, assetPath);

	SkeletonCacheEntry entry;
	entry.Topology = fileData.Topology;
	if (!fileTimeError)
		entry.LastWriteTime = currentWriteTime;
	mSkeletonCache[assetPath] = entry;
	*outSkeleton = entry;
	return true;
}

bool AnimationSystem::TryLoadAnimation(
	const std::wstring& assetPath,
	AnimationCacheEntry* outAnimation)
{
	if (outAnimation == nullptr || assetPath.empty())
		return false;

	const std::filesystem::path resolvedPath = ResolveAssetPath(assetPath);
	std::error_code fileTimeError;
	const std::filesystem::file_time_type currentWriteTime =
		std::filesystem::last_write_time(resolvedPath, fileTimeError);

	const auto cacheIt = mAnimationCache.find(assetPath);
	if (cacheIt != mAnimationCache.end() &&
		(fileTimeError || cacheIt->second.LastWriteTime == currentWriteTime))
	{
		*outAnimation = cacheIt->second;
		return true;
	}

	WAnimationFileData fileData;
	if (!WAnimationFile::LoadFromFile(resolvedPath, &fileData))
		return false;

	AnimationCacheEntry entry;
	entry.Clip = fileData.Clip;
	if (!fileTimeError)
		entry.LastWriteTime = currentWriteTime;
	mAnimationCache[assetPath] = entry;
	*outAnimation = entry;
	return true;
}

Witchcraft::Animation::BoneLocalPose AnimationSystem::SampleTrack(
	const Witchcraft::Animation::BoneAnimationTrack& track,
	float time,
	const Witchcraft::Animation::BoneLocalPose& fallbackPose)
{
	Witchcraft::Animation::BoneLocalPose pose = fallbackPose;

	auto findLastIndexNotAfter =
		[time](const auto& keys) -> int
	{
		if (keys.empty())
			return -1;

		int left = 0;
		int right = static_cast<int>(keys.size()) - 1;
		int result = -1;
		while (left <= right)
		{
			const int middle = left + (right - left) / 2;
			const auto& key = keys[static_cast<size_t>(middle)];
			if (!IsFiniteAnimationFloat(key.Time))
			{
				right = middle - 1;
				continue;
			}
			if (key.Time <= time)
			{
				result = middle;
				left = middle + 1;
			}
			else
			{
				right = middle - 1;
			}
		}
		return result;
	};

	auto computeInterpolationAlpha =
		[time](float startTime, float endTime) -> float
	{
		const float duration = endTime - startTime;
		if (!std::isfinite(duration) || std::abs(duration) <= 1e-6f)
			return 0.0f;

		const float alpha = (time - startTime) / duration;
		if (!std::isfinite(alpha))
			return 0.0f;
		return (std::max)(0.0f, (std::min)(1.0f, alpha));
	};

	// MatrixKeys 保存的是导入后重建的局部矩阵。
	// 这里优先使用它们，但必须做“相邻关键帧插值”，不能再退回到“取最近上一帧”的阶梯采样；
	// 否则就会表现成抽搐，而完全禁用它又会让部分资源退回接近 T Pose。
	if (!track.MatrixKeys.empty())
	{
		const int keyIndex = findLastIndexNotAfter(track.MatrixKeys);
		const Witchcraft::Animation::BoneMatrixKey& keyA =
			keyIndex < 0 ? track.MatrixKeys.front() : track.MatrixKeys[static_cast<size_t>(keyIndex)];

		Witchcraft::Animation::BoneLocalPose poseA = fallbackPose;
		if (TryDecomposeAnimationMatrixToBoneLocalPose(keyA.Value, &poseA))
		{
			Witchcraft::Animation::BoneLocalPose resultPose = poseA;
			if (keyIndex >= 0 &&
				keyIndex + 1 < static_cast<int>(track.MatrixKeys.size()))
			{
				const Witchcraft::Animation::BoneMatrixKey& keyB =
					track.MatrixKeys[static_cast<size_t>(keyIndex + 1)];
				Witchcraft::Animation::BoneLocalPose poseB = poseA;
				if (IsFiniteAnimationFloat(keyA.Time) &&
					IsFiniteAnimationFloat(keyB.Time) &&
					time > keyA.Time &&
					TryDecomposeAnimationMatrixToBoneLocalPose(keyB.Value, &poseB))
				{
					const float alpha = computeInterpolationAlpha(keyA.Time, keyB.Time);

					DirectX::XMStoreFloat3(
						&resultPose.Translation,
						DirectX::XMVectorLerp(
							DirectX::XMLoadFloat3(&poseA.Translation),
							DirectX::XMLoadFloat3(&poseB.Translation),
							alpha));

					DirectX::XMStoreFloat3(
						&resultPose.Scale,
						DirectX::XMVectorLerp(
							DirectX::XMLoadFloat3(&poseA.Scale),
							DirectX::XMLoadFloat3(&poseB.Scale),
							alpha));

					DirectX::XMVECTOR quatA = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&poseA.Rotation));
					DirectX::XMVECTOR quatB = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&poseB.Rotation));
					if (DirectX::XMVectorGetX(DirectX::XMVector4Dot(quatA, quatB)) < 0.0f)
						quatB = DirectX::XMVectorNegate(quatB);
					DirectX::XMStoreFloat4(
						&resultPose.Rotation,
						DirectX::XMQuaternionNormalize(DirectX::XMQuaternionSlerp(quatA, quatB, alpha)));
				}
			}

			resultPose.Matrix = ComposeAnimationLocalPoseMatrix(resultPose);
			resultPose.HasMatrix = true;
			return resultPose;
		}
	}

	auto sampleFloat3Keys =
		[&](const auto& keys, const DirectX::XMFLOAT3& fallbackValue) -> DirectX::XMFLOAT3
	{
		if (keys.empty())
			return fallbackValue;

		const int keyIndex = findLastIndexNotAfter(keys);
		if (keyIndex < 0)
			return IsFiniteAnimationFloat3(keys.front().Value) ? keys.front().Value : fallbackValue;

		const auto& keyA = keys[static_cast<size_t>(keyIndex)];
		if (keyIndex + 1 >= static_cast<int>(keys.size()) ||
			!IsFiniteAnimationFloat(keyA.Time) ||
			!IsFiniteAnimationFloat3(keyA.Value))
		{
			return IsFiniteAnimationFloat3(keyA.Value) ? keyA.Value : fallbackValue;
		}

		const auto& keyB = keys[static_cast<size_t>(keyIndex + 1)];
		if (!IsFiniteAnimationFloat(keyB.Time) || !IsFiniteAnimationFloat3(keyB.Value) || time <= keyA.Time)
			return keyA.Value;

		const float alpha = computeInterpolationAlpha(keyA.Time, keyB.Time);
		XMFLOAT3 result = fallbackValue;
		XMStoreFloat3(
			&result,
			XMVectorLerp(
				XMLoadFloat3(&keyA.Value),
				XMLoadFloat3(&keyB.Value),
				alpha));
		return IsFiniteAnimationFloat3(result) ? result : keyA.Value;
	};

	auto sampleQuaternionKeys =
		[&](const auto& keys, const DirectX::XMFLOAT4& fallbackValue) -> DirectX::XMFLOAT4
	{
		if (keys.empty())
			return fallbackValue;

		const int keyIndex = findLastIndexNotAfter(keys);
		if (keyIndex < 0)
			return IsFiniteAnimationFloat4(keys.front().Value) ? keys.front().Value : fallbackValue;

		const auto& keyA = keys[static_cast<size_t>(keyIndex)];
		if (keyIndex + 1 >= static_cast<int>(keys.size()) ||
			!IsFiniteAnimationFloat(keyA.Time) ||
			!IsFiniteAnimationFloat4(keyA.Value))
		{
			return IsFiniteAnimationFloat4(keyA.Value) ? keyA.Value : fallbackValue;
		}

		const auto& keyB = keys[static_cast<size_t>(keyIndex + 1)];
		if (!IsFiniteAnimationFloat(keyB.Time) || !IsFiniteAnimationFloat4(keyB.Value) || time <= keyA.Time)
			return keyA.Value;

		const float alpha = computeInterpolationAlpha(keyA.Time, keyB.Time);
		XMVECTOR quatA = XMQuaternionNormalize(XMLoadFloat4(&keyA.Value));
		XMVECTOR quatB = XMQuaternionNormalize(XMLoadFloat4(&keyB.Value));
		if (XMVectorGetX(XMVector4Dot(quatA, quatB)) < 0.0f)
			quatB = XMVectorNegate(quatB);

		XMFLOAT4 result = fallbackValue;
		XMStoreFloat4(&result, XMQuaternionNormalize(XMQuaternionSlerp(quatA, quatB, alpha)));
		return IsFiniteAnimationFloat4(result) ? result : keyA.Value;
	};

	if (!track.TranslationKeys.empty())
	{
		pose.Translation = sampleFloat3Keys(track.TranslationKeys, pose.Translation);
	}

	if (!track.RotationKeys.empty())
	{
		pose.Rotation = sampleQuaternionKeys(track.RotationKeys, pose.Rotation);
	}

	if (!track.ScaleKeys.empty())
	{
		pose.Scale = sampleFloat3Keys(track.ScaleKeys, pose.Scale);
	}

	return pose;
}

void AnimationSystem::EvaluateAnimatedEntity(
	WitchcraECS* ecs,
	SceneEntityBase* entity,
	Witchcraft::Animation::SkeletonData* skeletonState,
	AnimatorComponent* animatorComponent,
	SkinningRuntimeComponent* runtimeComponent,
	float deltaTime)
{
	if (skeletonState == nullptr || animatorComponent == nullptr || runtimeComponent == nullptr)
		return;

	SkeletonCacheEntry skeletonAssetData;
	const bool skeletonLoaded = TryLoadSkeleton(skeletonState->SkeletonAssetPath, &skeletonAssetData);

	const Witchcraft::Animation::SkeletonTopology* topologyPtr = nullptr;
	if (skeletonLoaded)
		topologyPtr = &skeletonAssetData.Topology;
	else if (!skeletonState->Topology.Bones.empty())
		topologyPtr = &skeletonState->Topology;

	if (topologyPtr == nullptr || topologyPtr->Bones.empty())
		return;

	const auto& topology = *topologyPtr;
	std::vector<Witchcraft::Animation::BoneLocalPose> localPose;
	localPose.reserve(topology.Bones.size());
	for (const auto& bone : topology.Bones)
		localPose.push_back(bone.BindLocalPose);
	std::vector<DirectX::XMFLOAT4X4> localMatrixPose;
	localMatrixPose.reserve(topology.Bones.size());
	for (const auto& bone : topology.Bones)
	{
		localMatrixPose.push_back(
			bone.BindLocalPose.HasMatrix
			? bone.BindLocalPose.Matrix
			: ComposeAnimationLocalPoseMatrix(bone.BindLocalPose));
	}
	std::vector<std::wstring> boneNames;
	boneNames.reserve(topology.Bones.size());
	for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(topology.Bones.size()); ++boneIndex)
	{
		boneNames.push_back(topology.Bones[boneIndex].Name);
	}

	const bool shouldEvaluateAnimation =
		!animatorComponent->GetClipAssetPath().empty() &&
		(animatorComponent->IsPlaying() || animatorComponent->ShouldEvaluateWhenPaused());
	bool shouldCommitPose = animatorComponent->GetClipAssetPath().empty();

	if (shouldEvaluateAnimation)
	{
		AnimationCacheEntry animationData;
		if (TryLoadAnimation(animatorComponent->GetClipAssetPath(), &animationData))
		{
			shouldCommitPose = true;
			float currentTime = animatorComponent->GetTime();
			if (animatorComponent->IsPlaying())
			{
				const float ticksPerSecond = animationData.Clip.TicksPerSecond > 0.0f
					? animationData.Clip.TicksPerSecond
					: 1.0f;
				currentTime += deltaTime * animatorComponent->GetSpeed() * ticksPerSecond;
				if (animationData.Clip.Duration > 0.0f)
				{
					if (animatorComponent->IsLoop())
					{
						while (currentTime > animationData.Clip.Duration)
							currentTime -= animationData.Clip.Duration;
					}
					else if (currentTime > animationData.Clip.Duration)
					{
						currentTime = animationData.Clip.Duration;
						animatorComponent->SetPlaying(false);
					}
				}
				animatorComponent->SetTime(currentTime);
			}

			std::uint32_t matchedTrackCount = 0u;
			for (const auto& track : animationData.Clip.Tracks)
			{
				std::int32_t boneIndex = -1;
				if (!track.BoneName.empty())
				{
					const auto boneIt = topology.BoneNameToIndex.find(track.BoneName);
					if (boneIt != topology.BoneNameToIndex.end())
						boneIndex = static_cast<std::int32_t>(boneIt->second);
				}
				if (boneIndex < 0)
					boneIndex = track.BoneIndex;

				if (boneIndex < 0 || boneIndex >= static_cast<std::int32_t>(localPose.size()))
					continue;

				++matchedTrackCount;
				localPose[boneIndex] =
					SampleTrack(track, currentTime, localPose[boneIndex]);
				localMatrixPose[boneIndex] = ComposeAnimationLocalPoseMatrix(localPose[boneIndex]);
			}

			// 无论动画轨道是否能映射到骨架，都必须提交当前姿态并刷新 palette。
			// 否则导入动画的骨骼名称或索引不匹配时，时间虽会推进，渲染端却一直拿到旧骨矩阵。
		}
	}

	if (shouldCommitPose)
	{
		// 无动画时以资源导入时的 bind global 作为静止基准。
		// 它与 inverse bind 同源，避免运行时重建误差掩盖动画求值问题。
		skeletonState->Topology = topology;
		skeletonState->BoneNames = boneNames;
		skeletonState->LocalPose = localPose;
		skeletonState->LocalMatrixPose = localMatrixPose;
		skeletonState->Dirty = true;
		if (animatorComponent->GetClipAssetPath().empty())
		{
			skeletonState->GlobalPose.clear();
			skeletonState->GlobalPose.reserve(topology.Bones.size());
			for (const Witchcraft::Animation::SkeletonBone& bone : topology.Bones)
				skeletonState->GlobalPose.push_back(bone.BindGlobalMatrix);
		}
		else
		{
			mPoseSystem.UpdateGlobalPose(skeletonState, topology);
		}
		mSkinningPaletteSystem.UpdatePalette(skeletonState, runtimeComponent, topology);
		skeletonState->Dirty = false;
		(void)ecs->SyncSkeletonDataToComponent(entity);
	}
}

void AnimationSystem::UpdateEntityRecursive(WitchcraECS * ecs, SceneEntityBase * entity, float deltaTime)
{
	if (ecs == nullptr || entity == nullptr)
		return;

	Witchcraft::Animation::SkeletonData* skeletonData = ecs->GetSkeletonData(entity);
	AnimatorComponent* animatorComponent = ecs->GetComponent<AnimatorComponent>(entity);
	SkinningRuntimeComponent* runtimeComponent = ecs->GetComponent<SkinningRuntimeComponent>(entity);
	if (skeletonData != nullptr && animatorComponent != nullptr && runtimeComponent != nullptr)
		EvaluateAnimatedEntity(ecs, entity, skeletonData, animatorComponent, runtimeComponent, deltaTime);

	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
	{
		BoneAttachmentComponent* boneAttach = ecs->GetComponent<BoneAttachmentComponent>(childEntity);
		if (boneAttach != nullptr && skeletonData != nullptr &&
			boneAttach->GetBoneIndex() >= 0 &&
			static_cast<std::size_t>(boneAttach->GetBoneIndex()) < skeletonData->GlobalPose.size())
		{
			TransformComponent* tc = ecs->GetComponent<TransformComponent>(childEntity);
			if (tc != nullptr)
			{
				const DirectX::XMFLOAT4X4& boneMatrix = skeletonData->GlobalPose[boneAttach->GetBoneIndex()];
				DirectX::XMVECTOR s, r, t;
				DirectX::XMMatrixDecompose(&s, &r, &t, DirectX::XMLoadFloat4x4(&boneMatrix));
				DirectX::XMFLOAT3 pos;
				DirectX::XMStoreFloat3(&pos, t);
				DirectX::XMFLOAT4 quat;
				DirectX::XMStoreFloat4(&quat, r);
				DirectX::XMFLOAT3 euler;
				euler.x = DirectX::XMConvertToDegrees(atan2f(2.0f * (quat.w * quat.x + quat.y * quat.z), 1.0f - 2.0f * (quat.x * quat.x + quat.y * quat.y)));
				euler.y = DirectX::XMConvertToDegrees(asinf(std::clamp(-2.0f * (quat.w * quat.y - quat.z * quat.x), -1.0f, 1.0f)));
				euler.z = DirectX::XMConvertToDegrees(atan2f(2.0f * (quat.w * quat.z + quat.x * quat.y), 1.0f - 2.0f * (quat.y * quat.y + quat.z * quat.z)));
				tc->SetPosition3f(pos);
				tc->SetRotation3f(euler);
				DirectX::XMFLOAT3 scale;
				DirectX::XMStoreFloat3(&scale, s);
				tc->SetScale3f(scale);
			}
		}
		UpdateEntityRecursive(ecs, childEntity, deltaTime);
	}
}
void AnimationSystem::Update(WitchcraECS* ecs, float deltaTime)
{
	if (ecs == nullptr)
		return;

	// deltaTime < 0 时强制归零，但不跳过整个系统。
	// 即使 deltaTime == 0，仍需遍历实体：EvaluateWhenPaused 的实体
	// 需要按当前时间求值以支持编辑器预览/拖动时间轴等场景。
	const float safeDeltaTime = (std::max)(0.0f, deltaTime);

	for (SceneEntityBase* rootEntity : ecs->GetSceneRootEntities())
		UpdateEntityRecursive(ecs, rootEntity, safeDeltaTime);
}
