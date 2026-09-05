#include "System/Animation/AnimationSystem.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "ECS/Component/BoneAttachmentComponent.h"
#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/SkinningRuntimeComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/WitchcraECS.h"
#include "Engine/EngineUtils.h"
#include "System/Animation/AnimationClipCache.h"
#include "System/Animation/AnimationLayerMask.h"
#include "System/ScriptingSystem.h"
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

	float Clamp01(float value)
	{
		if (!std::isfinite(value))
			return 0.0f;
		return (std::max)(0.0f, (std::min)(1.0f, value));
	}

	float GetClipTicksPerSecond(const Witchcraft::Animation::AnimationClipDesc& clip)
	{
		return clip.TicksPerSecond > 0.0f ? clip.TicksPerSecond : 1.0f;
	}

	float AdvanceAnimationClipTime(
		const Witchcraft::Animation::AnimationClipDesc& clip,
		float time,
		float deltaTime,
		float speed,
		bool loop,
		bool* outReachedEnd)
	{
		if (outReachedEnd != nullptr)
			*outReachedEnd = false;

		float currentTime = time + deltaTime * speed * GetClipTicksPerSecond(clip);
		if (clip.Duration > 0.0f)
		{
			if (loop)
			{
				while (currentTime > clip.Duration)
					currentTime -= clip.Duration;
			}
			else if (currentTime > clip.Duration)
			{
				currentTime = clip.Duration;
				if (outReachedEnd != nullptr)
					*outReachedEnd = true;
			}
		}

		return currentTime;
	}

	bool DidWrapAnimationTime(float previousTime, float rawTargetTime, float duration)
	{
		return duration > 0.0f && rawTargetTime > duration && rawTargetTime > previousTime;
	}

	bool IsAnimationEventTimeInRange(float eventTime, float startExclusive, float endInclusive)
	{
		return eventTime > startExclusive && eventTime <= endInclusive;
	}

	bool DidCrossAnimationEventTime(
		float eventTime,
		float previousTime,
		float rawTargetTime,
		float duration)
	{
		if (rawTargetTime <= previousTime)
			return false;
		if (duration <= 0.0f || !DidWrapAnimationTime(previousTime, rawTargetTime, duration))
			return IsAnimationEventTimeInRange(eventTime, previousTime, rawTargetTime);

		if (IsAnimationEventTimeInRange(eventTime, previousTime, duration))
			return true;
		const float wrappedTargetTime = std::fmod(rawTargetTime, duration);
		if (IsAnimationEventTimeInRange(eventTime, -kAnimationDefaultValueEpsilon, wrappedTargetTime))
			return true;
		return rawTargetTime - previousTime >= duration;
	}

	void NotifyAnimationEvent(
		ScriptingSystem* scriptingSystem,
		SceneEntityBase* ownerEntity,
		const Witchcraft::Animation::AnimationClipEvent& eventPoint,
		const std::wstring& clipAssetPath,
		const std::wstring& layerName,
		float playbackTime)
	{
		if (scriptingSystem == nullptr || ownerEntity == nullptr)
			return;

		AnimationScriptEventNotification notification;
		notification.OwnerEntity = ownerEntity;
		notification.EventName = eventPoint.Name;
		notification.Parameter = eventPoint.Parameter;
		notification.ScriptCallbackName = eventPoint.ScriptCallbackName;
		notification.ClipAssetPath = clipAssetPath;
		notification.LayerName = layerName;
		notification.EventTime = eventPoint.Time;
		notification.PlaybackTime = playbackTime;
		scriptingSystem->NotifyAnimationEvent(notification);
	}

	void NotifyAnimationEventsInRange(
		ScriptingSystem* scriptingSystem,
		SceneEntityBase* ownerEntity,
		const Witchcraft::Animation::AnimationClipDesc& clip,
		const std::wstring& clipAssetPath,
		const std::wstring& layerName,
		float previousTime,
		float rawTargetTime,
		float playbackTime)
	{
		if (scriptingSystem == nullptr || ownerEntity == nullptr || clip.Events.empty())
			return;

		for (const Witchcraft::Animation::AnimationClipEvent& eventPoint : clip.Events)
		{
			if (DidCrossAnimationEventTime(eventPoint.Time, previousTime, rawTargetTime, clip.Duration))
				NotifyAnimationEvent(scriptingSystem, ownerEntity, eventPoint, clipAssetPath, layerName, playbackTime);
		}
	}

	Witchcraft::Animation::BoneLocalPose BlendBoneLocalPose(
		const Witchcraft::Animation::BoneLocalPose& currentPose,
		const Witchcraft::Animation::BoneLocalPose& targetPose,
		float weight)
	{
		const float blendWeight = Clamp01(weight);
		if (blendWeight <= 0.0f)
			return currentPose;
		if (blendWeight >= 1.0f)
			return targetPose;

		Witchcraft::Animation::BoneLocalPose result;
		DirectX::XMStoreFloat3(
			&result.Translation,
			DirectX::XMVectorLerp(
				DirectX::XMLoadFloat3(&currentPose.Translation),
				DirectX::XMLoadFloat3(&targetPose.Translation),
				blendWeight));
		DirectX::XMStoreFloat3(
			&result.Scale,
			DirectX::XMVectorLerp(
				DirectX::XMLoadFloat3(&currentPose.Scale),
				DirectX::XMLoadFloat3(&targetPose.Scale),
				blendWeight));

		DirectX::XMVECTOR currentRotation = DirectX::XMLoadFloat4(&currentPose.Rotation);
		DirectX::XMVECTOR targetRotation = DirectX::XMLoadFloat4(&targetPose.Rotation);
		if (DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(currentRotation)) <= 1e-8f)
			currentRotation = DirectX::XMQuaternionIdentity();
		else
			currentRotation = DirectX::XMQuaternionNormalize(currentRotation);
		if (DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(targetRotation)) <= 1e-8f)
			targetRotation = DirectX::XMQuaternionIdentity();
		else
			targetRotation = DirectX::XMQuaternionNormalize(targetRotation);
		if (DirectX::XMVectorGetX(DirectX::XMVector4Dot(currentRotation, targetRotation)) < 0.0f)
			targetRotation = DirectX::XMVectorNegate(targetRotation);

		DirectX::XMStoreFloat4(
			&result.Rotation,
			DirectX::XMQuaternionNormalize(DirectX::XMQuaternionSlerp(currentRotation, targetRotation, blendWeight)));
		if (!IsFiniteAnimationFloat4(result.Rotation))
			result.Rotation = blendWeight < 0.5f ? currentPose.Rotation : targetPose.Rotation;

		result.Matrix = ComposeAnimationLocalPoseMatrix(result);
		result.HasMatrix = true;
		return result;
	}

	bool BlendLocalPoseBuffers(
		const std::vector<Witchcraft::Animation::BoneLocalPose>& currentPose,
		const std::vector<Witchcraft::Animation::BoneLocalPose>& targetPose,
		float weight,
		std::vector<Witchcraft::Animation::BoneLocalPose>* outLocalPose,
		std::vector<DirectX::XMFLOAT4X4>* outLocalMatrixPose)
	{
		if (outLocalPose == nullptr ||
			currentPose.size() != targetPose.size())
		{
			return false;
		}

		outLocalPose->clear();
		outLocalPose->reserve(currentPose.size());
		if (outLocalMatrixPose != nullptr)
		{
			outLocalMatrixPose->clear();
			outLocalMatrixPose->reserve(currentPose.size());
		}

		for (std::size_t boneIndex = 0; boneIndex < currentPose.size(); ++boneIndex)
		{
			Witchcraft::Animation::BoneLocalPose blendedPose =
				BlendBoneLocalPose(currentPose[boneIndex], targetPose[boneIndex], weight);
			outLocalPose->push_back(blendedPose);
			if (outLocalMatrixPose != nullptr)
				outLocalMatrixPose->push_back(blendedPose.Matrix);
		}

		return true;
	}

	Witchcraft::Animation::BoneLocalPose AdditiveBlendBoneLocalPose(
		const Witchcraft::Animation::BoneLocalPose& currentPose,
		const Witchcraft::Animation::BoneLocalPose& bindPose,
		const Witchcraft::Animation::BoneLocalPose& additivePose,
		float weight)
	{
		const float blendWeight = Clamp01(weight);
		if (blendWeight <= 0.0f)
			return currentPose;

		Witchcraft::Animation::BoneLocalPose result = currentPose;
		result.Translation.x += (additivePose.Translation.x - bindPose.Translation.x) * blendWeight;
		result.Translation.y += (additivePose.Translation.y - bindPose.Translation.y) * blendWeight;
		result.Translation.z += (additivePose.Translation.z - bindPose.Translation.z) * blendWeight;
		result.Scale.x += (additivePose.Scale.x - bindPose.Scale.x) * blendWeight;
		result.Scale.y += (additivePose.Scale.y - bindPose.Scale.y) * blendWeight;
		result.Scale.z += (additivePose.Scale.z - bindPose.Scale.z) * blendWeight;

		DirectX::XMVECTOR currentRotation = DirectX::XMLoadFloat4(&currentPose.Rotation);
		DirectX::XMVECTOR bindRotation = DirectX::XMLoadFloat4(&bindPose.Rotation);
		DirectX::XMVECTOR additiveRotation = DirectX::XMLoadFloat4(&additivePose.Rotation);
		if (DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(currentRotation)) <= 1e-8f)
			currentRotation = DirectX::XMQuaternionIdentity();
		else
			currentRotation = DirectX::XMQuaternionNormalize(currentRotation);
		if (DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(bindRotation)) <= 1e-8f)
			bindRotation = DirectX::XMQuaternionIdentity();
		else
			bindRotation = DirectX::XMQuaternionNormalize(bindRotation);
		if (DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(additiveRotation)) <= 1e-8f)
			additiveRotation = DirectX::XMQuaternionIdentity();
		else
			additiveRotation = DirectX::XMQuaternionNormalize(additiveRotation);

		DirectX::XMVECTOR deltaRotation =
			DirectX::XMQuaternionMultiply(additiveRotation, DirectX::XMQuaternionInverse(bindRotation));
		if (DirectX::XMVectorGetX(DirectX::XMVector4Dot(DirectX::XMQuaternionIdentity(), deltaRotation)) < 0.0f)
			deltaRotation = DirectX::XMVectorNegate(deltaRotation);
		const DirectX::XMVECTOR weightedDeltaRotation =
			DirectX::XMQuaternionSlerp(DirectX::XMQuaternionIdentity(), deltaRotation, blendWeight);
		DirectX::XMStoreFloat4(
			&result.Rotation,
			DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(weightedDeltaRotation, currentRotation)));
		if (!IsFiniteAnimationFloat4(result.Rotation))
			result.Rotation = currentPose.Rotation;

		result.Matrix = ComposeAnimationLocalPoseMatrix(result);
		result.HasMatrix = true;
		return result;
	}

	void ApplyAnimationLayerPose(
		const std::vector<Witchcraft::Animation::BoneLocalPose>& bindLocalPose,
		const std::vector<Witchcraft::Animation::BoneLocalPose>& layerLocalPose,
		const std::vector<bool>& boneMask,
		const AnimatorComponent::AnimationLayer& layer,
		std::vector<Witchcraft::Animation::BoneLocalPose>* inOutLocalPose,
		std::vector<DirectX::XMFLOAT4X4>* inOutLocalMatrixPose)
	{
		if (inOutLocalPose == nullptr ||
			bindLocalPose.size() != layerLocalPose.size() ||
			inOutLocalPose->size() != layerLocalPose.size())
		{
			return;
		}

		const float layerWeight = Clamp01(layer.Weight);
		if (layerWeight <= 0.0f)
			return;

		for (std::size_t boneIndex = 0; boneIndex < layerLocalPose.size(); ++boneIndex)
		{
			if (boneIndex >= boneMask.size() || !boneMask[boneIndex])
				continue;

			Witchcraft::Animation::BoneLocalPose blendedPose;
			if (layer.BlendMode == AnimatorComponent::AnimationLayerBlendMode::Additive)
			{
				blendedPose = AdditiveBlendBoneLocalPose(
					(*inOutLocalPose)[boneIndex],
					bindLocalPose[boneIndex],
					layerLocalPose[boneIndex],
					layerWeight);
			}
			else
			{
				blendedPose = BlendBoneLocalPose(
					(*inOutLocalPose)[boneIndex],
					layerLocalPose[boneIndex],
					layerWeight);
			}

			(*inOutLocalPose)[boneIndex] = blendedPose;
			if (inOutLocalMatrixPose != nullptr && boneIndex < inOutLocalMatrixPose->size())
				(*inOutLocalMatrixPose)[boneIndex] = blendedPose.Matrix;
		}
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

	std::int32_t ResolveAnimationTrackBoneIndex(
		const Witchcraft::Animation::BoneAnimationTrack& track,
		const Witchcraft::Animation::SkeletonTopology& topology)
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

		return topology.IsValidBoneIndex(boneIndex) ? boneIndex : -1;
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
	const AnimationCacheEntry** outAnimation)
{
	if (outAnimation == nullptr)
		return false;
	*outAnimation = nullptr;
	if (assetPath.empty())
		return false;

	const std::filesystem::path resolvedPath = ResolveAssetPath(assetPath);
	std::error_code fileTimeError;
	const std::filesystem::file_time_type currentWriteTime =
		std::filesystem::last_write_time(resolvedPath, fileTimeError);

	const auto cacheIt = mAnimationCache.find(assetPath);
	if (cacheIt != mAnimationCache.end() &&
		(fileTimeError || cacheIt->second.LastWriteTime == currentWriteTime))
	{
		if (!cacheIt->second.Asset.IsCacheReady())
			cacheIt->second.Asset.RebuildCaches();
		*outAnimation = &cacheIt->second;
		return true;
	}

	WAnimationFileData fileData;
	if (!WAnimationFile::LoadFromFile(resolvedPath, &fileData))
		return false;

	AnimationCacheEntry entry;
	entry.Asset.SetDescription(std::move(fileData.Clip));
	if (!fileTimeError)
		entry.LastWriteTime = currentWriteTime;
	auto insertResult = mAnimationCache.insert_or_assign(assetPath, std::move(entry));
	*outAnimation = &insertResult.first->second;
	return true;
}

Witchcraft::Animation::BoneLocalPose AnimationSystem::SampleTrack(
	const Witchcraft::Animation::BoneAnimationTrack& track,
	const Witchcraft::Animation::AnimationTrackCache* trackCache,
	float time,
	const Witchcraft::Animation::BoneLocalPose& fallbackPose)
{
	Witchcraft::Animation::BoneLocalPose pose = fallbackPose;

	struct ResolvedAnimationSegment
	{
		std::size_t StartKeyIndex = 0;
		std::size_t EndKeyIndex = 0;
		float Alpha = 0.0f;
		Witchcraft::Animation::AnimationInterpolationType Interpolation =
			Witchcraft::Animation::AnimationInterpolationType::Linear;
		const Witchcraft::Animation::AnimationKeySegmentCache* CachedSegment = nullptr;
		bool HasStartKey = false;
		bool HasNextKey = false;
	};

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

	auto shouldUseStepInterpolation =
		[](Witchcraft::Animation::AnimationInterpolationType interpolation) -> bool
	{
		return interpolation == Witchcraft::Animation::AnimationInterpolationType::Step;
	};

	auto shouldUseCubicSplineInterpolation =
		[](Witchcraft::Animation::AnimationInterpolationType interpolation) -> bool
	{
		return interpolation == Witchcraft::Animation::AnimationInterpolationType::CubicSpline;
	};

	auto evaluateFloat3CubicSpline =
		[](const Witchcraft::Animation::AnimationFloat3CubicSplineSegmentCache& cubicData, float alpha) -> DirectX::XMFLOAT3
	{
		const float t = (std::max)(0.0f, (std::min)(1.0f, alpha));
		const float t2 = t * t;
		const float t3 = t2 * t;
		return DirectX::XMFLOAT3(
			cubicData.CoeffA.x * t3 + cubicData.CoeffB.x * t2 + cubicData.CoeffC.x * t + cubicData.CoeffD.x,
			cubicData.CoeffA.y * t3 + cubicData.CoeffB.y * t2 + cubicData.CoeffC.y * t + cubicData.CoeffD.y,
			cubicData.CoeffA.z * t3 + cubicData.CoeffB.z * t2 + cubicData.CoeffC.z * t + cubicData.CoeffD.z);
	};

	auto evaluateQuaternionCubicSpline =
		[](const Witchcraft::Animation::AnimationQuaternionCubicSplineSegmentCache& cubicData, float alpha) -> DirectX::XMFLOAT4
	{
		const float t = (std::max)(0.0f, (std::min)(1.0f, alpha));
		const float t2 = t * t;
		const float t3 = t2 * t;
		DirectX::XMFLOAT4 value(
			cubicData.CoeffA.x * t3 + cubicData.CoeffB.x * t2 + cubicData.CoeffC.x * t + cubicData.CoeffD.x,
			cubicData.CoeffA.y * t3 + cubicData.CoeffB.y * t2 + cubicData.CoeffC.y * t + cubicData.CoeffD.y,
			cubicData.CoeffA.z * t3 + cubicData.CoeffB.z * t2 + cubicData.CoeffC.z * t + cubicData.CoeffD.z,
			cubicData.CoeffA.w * t3 + cubicData.CoeffB.w * t2 + cubicData.CoeffC.w * t + cubicData.CoeffD.w);
		if (!IsFiniteAnimationFloat4(value))
			return cubicData.StartValue;

		const DirectX::XMVECTOR quaternion = DirectX::XMLoadFloat4(&value);
		const float lengthSq = DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(quaternion));
		if (!std::isfinite(lengthSq) || lengthSq <= 1e-8f)
			return cubicData.StartValue;

		DirectX::XMStoreFloat4(&value, DirectX::XMQuaternionNormalize(quaternion));
		return value;
	};

	auto resolveAnimationSegment =
		[&](const auto& keys, const Witchcraft::Animation::AnimationChannelCache* channelCache) -> ResolvedAnimationSegment
	{
		ResolvedAnimationSegment result;
		if (keys.empty())
			return result;

		if (channelCache != nullptr &&
			channelCache->KeyCount == keys.size() &&
			channelCache->HasSegments)
		{
			if (const Witchcraft::Animation::AnimationKeySegmentCache* cachedSegment =
				channelCache->FindSegment(time))
			{
				if (cachedSegment->HasNextKey &&
					cachedSegment->StartKeyIndex < keys.size() &&
					cachedSegment->EndKeyIndex < keys.size() &&
					IsFiniteAnimationFloat(keys[cachedSegment->StartKeyIndex].Time) &&
					IsFiniteAnimationFloat(keys[cachedSegment->EndKeyIndex].Time))
				{
					result.StartKeyIndex = cachedSegment->StartKeyIndex;
					result.EndKeyIndex = cachedSegment->EndKeyIndex;
					result.Interpolation = cachedSegment->Interpolation;
					result.CachedSegment = cachedSegment;
					result.HasStartKey = true;
					result.HasNextKey = true;

					if (std::isfinite(cachedSegment->InverseDuration) &&
						std::abs(cachedSegment->InverseDuration) > 0.0f)
					{
						const float alpha = (time - cachedSegment->StartTime) * cachedSegment->InverseDuration;
						result.Alpha = std::isfinite(alpha)
							? (std::max)(0.0f, (std::min)(1.0f, alpha))
							: 0.0f;
					}
					return result;
				}
			}
		}

		const int keyIndex = findLastIndexNotAfter(keys);
		if (keyIndex < 0)
			return result;

		result.StartKeyIndex = static_cast<std::size_t>(keyIndex);
		result.HasStartKey = true;
		if (keyIndex + 1 < static_cast<int>(keys.size()))
		{
			const auto& keyA = keys[static_cast<std::size_t>(keyIndex)];
			const auto& keyB = keys[static_cast<std::size_t>(keyIndex + 1)];
			if (IsFiniteAnimationFloat(keyA.Time) &&
				IsFiniteAnimationFloat(keyB.Time) &&
				time > keyA.Time)
			{
				result.EndKeyIndex = static_cast<std::size_t>(keyIndex + 1);
				result.Interpolation = keyA.Interpolation;
				result.HasNextKey = true;
				result.Alpha = computeInterpolationAlpha(keyA.Time, keyB.Time);
			}
		}
		return result;
	};

	// MatrixKeys 保存的是导入后重建的局部矩阵。
	// 这里优先使用它们，但必须做“相邻关键帧插值”，不能再退回到“取最近上一帧”的阶梯采样；
	// 否则就会表现成抽搐，而完全禁用它又会让部分资源退回接近 T Pose。
	if (!track.MatrixKeys.empty())
	{
		const ResolvedAnimationSegment segment = resolveAnimationSegment(
			track.MatrixKeys,
			trackCache != nullptr ? &trackCache->Matrix : nullptr);
		const std::size_t keyIndex = segment.HasStartKey ? segment.StartKeyIndex : 0;
		const Witchcraft::Animation::BoneMatrixKey& keyA =
			track.MatrixKeys[keyIndex];

		Witchcraft::Animation::BoneLocalPose poseA = fallbackPose;
		if (TryDecomposeAnimationMatrixToBoneLocalPose(keyA.Value, &poseA))
		{
			Witchcraft::Animation::BoneLocalPose resultPose = poseA;
			if (segment.HasNextKey)
			{
				const Witchcraft::Animation::BoneMatrixKey& keyB =
					track.MatrixKeys[segment.EndKeyIndex];
				Witchcraft::Animation::BoneLocalPose poseB = poseA;
				if (IsFiniteAnimationFloat(keyA.Time) &&
					IsFiniteAnimationFloat(keyB.Time) &&
					time > keyA.Time &&
					!shouldUseStepInterpolation(segment.Interpolation) &&
					TryDecomposeAnimationMatrixToBoneLocalPose(keyB.Value, &poseB))
				{
					const float alpha = segment.Alpha;

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
		[&](
			const auto& keys,
			const Witchcraft::Animation::AnimationChannelCache* channelCache,
			const DirectX::XMFLOAT3& fallbackValue) -> DirectX::XMFLOAT3
	{
		if (keys.empty())
			return fallbackValue;

		const ResolvedAnimationSegment segment = resolveAnimationSegment(keys, channelCache);
		if (!segment.HasStartKey)
			return IsFiniteAnimationFloat3(keys.front().Value) ? keys.front().Value : fallbackValue;

		const auto& keyA = keys[segment.StartKeyIndex];
		if (!segment.HasNextKey ||
			!IsFiniteAnimationFloat3(keyA.Value))
		{
			return IsFiniteAnimationFloat3(keyA.Value) ? keyA.Value : fallbackValue;
		}

		const auto& keyB = keys[segment.EndKeyIndex];
		if (!IsFiniteAnimationFloat(keyB.Time) || !IsFiniteAnimationFloat3(keyB.Value) || time <= keyA.Time)
			return keyA.Value;

		if (shouldUseStepInterpolation(segment.Interpolation))
			return keyA.Value;

		if (shouldUseCubicSplineInterpolation(segment.Interpolation) &&
			channelCache != nullptr &&
			segment.CachedSegment != nullptr)
		{
			if (const Witchcraft::Animation::AnimationFloat3CubicSplineSegmentCache* cubicData =
				channelCache->FindFloat3CubicSplineData(*segment.CachedSegment))
			{
				const DirectX::XMFLOAT3 cubicValue = evaluateFloat3CubicSpline(*cubicData, segment.Alpha);
				if (IsFiniteAnimationFloat3(cubicValue))
					return cubicValue;
			}
		}

		const float alpha = segment.Alpha;
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
		[&](
			const auto& keys,
			const Witchcraft::Animation::AnimationChannelCache* channelCache,
			const DirectX::XMFLOAT4& fallbackValue) -> DirectX::XMFLOAT4
	{
		if (keys.empty())
			return fallbackValue;

		const ResolvedAnimationSegment segment = resolveAnimationSegment(keys, channelCache);
		if (!segment.HasStartKey)
			return IsFiniteAnimationFloat4(keys.front().Value) ? keys.front().Value : fallbackValue;

		const auto& keyA = keys[segment.StartKeyIndex];
		if (!segment.HasNextKey ||
			!IsFiniteAnimationFloat4(keyA.Value))
		{
			return IsFiniteAnimationFloat4(keyA.Value) ? keyA.Value : fallbackValue;
		}

		const auto& keyB = keys[segment.EndKeyIndex];
		if (!IsFiniteAnimationFloat(keyB.Time) || !IsFiniteAnimationFloat4(keyB.Value) || time <= keyA.Time)
			return keyA.Value;

		if (shouldUseStepInterpolation(segment.Interpolation))
			return keyA.Value;

		if (shouldUseCubicSplineInterpolation(segment.Interpolation) &&
			channelCache != nullptr &&
			segment.CachedSegment != nullptr)
		{
			if (const Witchcraft::Animation::AnimationQuaternionCubicSplineSegmentCache* cubicData =
				channelCache->FindQuaternionCubicSplineData(*segment.CachedSegment))
			{
				const DirectX::XMFLOAT4 cubicValue = evaluateQuaternionCubicSpline(*cubicData, segment.Alpha);
				if (IsFiniteAnimationFloat4(cubicValue))
					return cubicValue;
			}
		}

		const float alpha = segment.Alpha;
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
		pose.Translation = sampleFloat3Keys(
			track.TranslationKeys,
			trackCache != nullptr ? &trackCache->Translation : nullptr,
			pose.Translation);
	}

	if (!track.RotationKeys.empty())
	{
		pose.Rotation = sampleQuaternionKeys(
			track.RotationKeys,
			trackCache != nullptr ? &trackCache->Rotation : nullptr,
			pose.Rotation);
	}

	if (!track.ScaleKeys.empty())
	{
		pose.Scale = sampleFloat3Keys(
			track.ScaleKeys,
			trackCache != nullptr ? &trackCache->Scale : nullptr,
			pose.Scale);
	}

	return pose;
}

bool AnimationSystem::SampleClip(
	const Witchcraft::Animation::AnimationClipDesc& clip,
	const Witchcraft::Animation::AnimationClipCache* clipCache,
	const Witchcraft::Animation::SkeletonTopology& topology,
	float time,
	std::vector<Witchcraft::Animation::BoneLocalPose>* inOutLocalPose,
	std::vector<DirectX::XMFLOAT4X4>* inOutLocalMatrixPose,
	std::uint32_t* outMatchedTrackCount)
{
	if (outMatchedTrackCount != nullptr)
		*outMatchedTrackCount = 0u;
	if (inOutLocalPose == nullptr || topology.Bones.empty())
		return false;
	if (inOutLocalPose->size() != topology.Bones.size())
		return false;
	if (inOutLocalMatrixPose != nullptr &&
		inOutLocalMatrixPose->size() != inOutLocalPose->size())
	{
		return false;
	}

	std::uint32_t matchedTrackCount = 0u;
	for (std::size_t trackIndex = 0; trackIndex < clip.Tracks.size(); ++trackIndex)
	{
		const Witchcraft::Animation::BoneAnimationTrack& track = clip.Tracks[trackIndex];
		const std::int32_t boneIndex = ResolveAnimationTrackBoneIndex(track, topology);
		if (boneIndex < 0)
			continue;

		const Witchcraft::Animation::AnimationTrackCache* trackCache = nullptr;
		if (clipCache != nullptr &&
			clipCache->IsReady() &&
			trackIndex < clipCache->Tracks.size())
		{
			trackCache = &clipCache->Tracks[trackIndex];
		}

		const std::size_t poseIndex = static_cast<std::size_t>(boneIndex);
		++matchedTrackCount;
		(*inOutLocalPose)[poseIndex] =
			SampleTrack(track, trackCache, time, (*inOutLocalPose)[poseIndex]);
		if (inOutLocalMatrixPose != nullptr)
			(*inOutLocalMatrixPose)[poseIndex] = ComposeAnimationLocalPoseMatrix((*inOutLocalPose)[poseIndex]);
	}

	if (outMatchedTrackCount != nullptr)
		*outMatchedTrackCount = matchedTrackCount;
	return true;
}

void AnimationSystem::EvaluateAnimatedEntity(
	WitchcraECS* ecs,
	SceneEntityBase* entity,
	Witchcraft::Animation::SkeletonData* skeletonState,
	AnimatorComponent* animatorComponent,
	SkinningRuntimeComponent* runtimeComponent,
	float deltaTime,
	ScriptingSystem* scriptingSystem)
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

	const std::vector<Witchcraft::Animation::BoneLocalPose> bindLocalPose = localPose;
	const std::vector<DirectX::XMFLOAT4X4> bindLocalMatrixPose = localMatrixPose;
	bool shouldCommitPose = !animatorComponent->HasPlayableLayers();
	bool sampledAnyLayer = false;

	std::vector<AnimatorComponent::AnimationLayer>& layers = animatorComponent->GetMutableLayers();
	for (std::size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex)
	{
		AnimatorComponent::AnimationLayer& layer = layers[layerIndex];
		if (!layer.Enabled || layer.ClipAssetPath.empty())
			continue;
		const bool hasLayerTransition = !layer.TransitionClipAssetPath.empty();
		if (!layer.Playing && !animatorComponent->ShouldEvaluateWhenPaused())
			continue;

		const AnimationCacheEntry* animationData = nullptr;
		if (!TryLoadAnimation(layer.ClipAssetPath, &animationData) || animationData == nullptr)
			continue;

		const Witchcraft::Animation::AnimationClipDesc& animationClip =
			animationData->Asset.GetDescription();
		const Witchcraft::Animation::AnimationClipCache& animationCache =
			animationData->Asset.GetCache();

		if (layer.Playing)
		{
			bool reachedEnd = false;
			const float previousLayerTime = layer.Time;
			const float rawTargetTime = previousLayerTime + deltaTime * layer.Speed * GetClipTicksPerSecond(animationClip);
			const float eventScanTargetTime = layer.Loop || animationClip.Duration <= 0.0f
				? rawTargetTime
				: (std::min)(rawTargetTime, animationClip.Duration);
			layer.Time = AdvanceAnimationClipTime(
				animationClip,
				layer.Time,
				deltaTime,
				layer.Speed,
				layer.Loop,
				&reachedEnd);
			if (layer.Speed > 0.0f)
			{
				NotifyAnimationEventsInRange(
					scriptingSystem,
					entity,
					animationClip,
					layer.ClipAssetPath,
					layer.Name,
					previousLayerTime,
					eventScanTargetTime,
					layer.Time);
			}
			if (reachedEnd && !layer.Loop)
				layer.Playing = false;
		}

		std::vector<Witchcraft::Animation::BoneLocalPose> layerLocalPose = bindLocalPose;
		std::vector<DirectX::XMFLOAT4X4> layerLocalMatrixPose = bindLocalMatrixPose;
		if (!SampleClip(
			animationClip,
			&animationCache,
			topology,
			layer.Time,
			&layerLocalPose,
			&layerLocalMatrixPose,
			nullptr))
		{
			continue;
		}

		if (hasLayerTransition)
		{
			const AnimationCacheEntry* transitionAnimationData = nullptr;
			if (TryLoadAnimation(layer.TransitionClipAssetPath, &transitionAnimationData) &&
				transitionAnimationData != nullptr)
			{
				const Witchcraft::Animation::AnimationClipDesc& transitionClip =
					transitionAnimationData->Asset.GetDescription();
				const Witchcraft::Animation::AnimationClipCache& transitionCache =
					transitionAnimationData->Asset.GetCache();

				bool transitionReachedEnd = false;
				if (layer.Playing)
				{
					layer.TransitionTime = AdvanceAnimationClipTime(
						transitionClip,
						layer.TransitionTime,
						deltaTime,
						layer.TransitionSpeed,
						layer.TransitionLoop,
						&transitionReachedEnd);
					layer.TransitionElapsed += deltaTime;
				}

				std::vector<Witchcraft::Animation::BoneLocalPose> transitionLocalPose = bindLocalPose;
				std::vector<DirectX::XMFLOAT4X4> transitionLocalMatrixPose = bindLocalMatrixPose;
				if (SampleClip(
					transitionClip,
					&transitionCache,
					topology,
					layer.TransitionTime,
					&transitionLocalPose,
					&transitionLocalMatrixPose,
					nullptr))
				{
					const float transitionWeight = layer.TransitionDuration > 0.0f
						? Clamp01(layer.TransitionElapsed / layer.TransitionDuration)
						: 1.0f;
					std::vector<Witchcraft::Animation::BoneLocalPose> blendedLayerLocalPose;
					std::vector<DirectX::XMFLOAT4X4> blendedLayerLocalMatrixPose;
					if (BlendLocalPoseBuffers(
						layerLocalPose,
						transitionLocalPose,
						transitionWeight,
						&blendedLayerLocalPose,
						&blendedLayerLocalMatrixPose))
					{
						layerLocalPose = std::move(blendedLayerLocalPose);
						layerLocalMatrixPose = std::move(blendedLayerLocalMatrixPose);
					}

					if (transitionWeight >= 1.0f)
					{
						const bool transitionLoop = layer.TransitionLoop;
						(void)animatorComponent->FinishLayerTransition(layerIndex);
						if (transitionReachedEnd && !transitionLoop)
							(void)animatorComponent->SetLayerPlaying(layerIndex, false);
					}
				}
			}
			else
			{
				(void)animatorComponent->ClearLayerTransition(layerIndex);
			}
		}

		std::vector<bool> boneMask;
		if (!Witchcraft::Animation::BuildAnimationLayerBoneMask(topology, layer.MaskRootBoneName, &boneMask))
			continue;

		const AnimatorComponent::AnimationLayer layerForApply = layer;
		ApplyAnimationLayerPose(
			bindLocalPose,
			layerLocalPose,
			boneMask,
			layerForApply,
			&localPose,
			&localMatrixPose);
		sampledAnyLayer = true;
		shouldCommitPose = true;
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
		if (!sampledAnyLayer)
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

void AnimationSystem::UpdateEntityRecursive(WitchcraECS * ecs, SceneEntityBase * entity, float deltaTime, ScriptingSystem* scriptingSystem)
{
	if (ecs == nullptr || entity == nullptr)
		return;

	Witchcraft::Animation::SkeletonData* skeletonData = ecs->GetSkeletonData(entity);
	AnimatorComponent* animatorComponent = ecs->GetComponent<AnimatorComponent>(entity);
	SkinningRuntimeComponent* runtimeComponent = ecs->GetComponent<SkinningRuntimeComponent>(entity);
	if (skeletonData != nullptr && animatorComponent != nullptr && runtimeComponent != nullptr)
		EvaluateAnimatedEntity(ecs, entity, skeletonData, animatorComponent, runtimeComponent, deltaTime, scriptingSystem);

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
		UpdateEntityRecursive(ecs, childEntity, deltaTime, scriptingSystem);
	}
}
void AnimationSystem::Update(WitchcraECS* ecs, float deltaTime, ScriptingSystem* scriptingSystem)
{
	if (ecs == nullptr)
		return;

	// deltaTime < 0 时强制归零，但不跳过整个系统。
	// 即使 deltaTime == 0，仍需遍历实体：EvaluateWhenPaused 的实体
	// 需要按当前时间求值以支持编辑器预览/拖动时间轴等场景。
	const float safeDeltaTime = (std::max)(0.0f, deltaTime);

	for (SceneEntityBase* rootEntity : ecs->GetSceneRootEntities())
		UpdateEntityRecursive(ecs, rootEntity, safeDeltaTime, scriptingSystem);
}
