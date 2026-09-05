#include "System/Animation/AnimationClipEditing.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace DirectX;

namespace Witchcraft::Animation
{
	namespace AnimationClipEditingDetail
	{
		float SanitizeKeyTime(float time)
		{
			return std::isfinite(time) ? (std::max)(0.0f, time) : 0.0f;
		}

		bool IsSameKeyTime(float lhs, float rhs, float epsilon)
		{
			return std::abs(lhs - rhs) <= (std::max)(epsilon, 0.0f);
		}

		DirectX::XMFLOAT4 NormalizeQuaternionValue(const DirectX::XMFLOAT4& value)
		{
			DirectX::XMFLOAT4 result(0.0f, 0.0f, 0.0f, 1.0f);
			const DirectX::XMVECTOR quaternion = DirectX::XMLoadFloat4(&value);
			const DirectX::XMVECTOR lengthSq = DirectX::XMVector4LengthSq(quaternion);
			if (DirectX::XMVectorGetX(lengthSq) <= 0.0f)
				return result;

			DirectX::XMStoreFloat4(&result, DirectX::XMQuaternionNormalize(quaternion));
			if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
				!std::isfinite(result.z) || !std::isfinite(result.w))
				return DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
			return result;
		}

		template<typename TKey>
		void SortKeysByTime(std::vector<TKey>& keys)
		{
			std::sort(
				keys.begin(),
				keys.end(),
				[](const TKey& lhs, const TKey& rhs)
				{
					return lhs.Time < rhs.Time;
				});
		}

		template<typename TKey, typename TValue>
		bool InsertOrReplaceKey(
			std::vector<TKey>& keys,
			float time,
			const TValue& value,
			float timeEpsilon)
		{
			const float sanitizedTime = SanitizeKeyTime(time);
			for (TKey& key : keys)
			{
				if (IsSameKeyTime(key.Time, sanitizedTime, timeEpsilon))
				{
					key.Time = sanitizedTime;
					key.Value = value;
					SortKeysByTime(keys);
					return false;
				}
			}

			TKey key;
			key.Time = sanitizedTime;
			key.Value = value;
			keys.push_back(key);
			SortKeysByTime(keys);
			return true;
		}

		template<typename TKey>
		bool DeleteKeyAt(std::vector<TKey>& keys, std::size_t keyIndex)
		{
			if (keyIndex >= keys.size())
				return false;

			keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(keyIndex));
			return true;
		}

		template<typename TKey>
		bool MoveKeyAt(std::vector<TKey>& keys, std::size_t keyIndex, float newTime)
		{
			if (keyIndex >= keys.size())
				return false;

			keys[keyIndex].Time = SanitizeKeyTime(newTime);
			SortKeysByTime(keys);
			return true;
		}

		template<typename TKey>
		float GetLastKeyTime(const std::vector<TKey>& keys)
		{
			float maxTime = 0.0f;
			for (const TKey& key : keys)
				maxTime = (std::max)(maxTime, SanitizeKeyTime(key.Time));
			return maxTime;
		}

		bool EnsureTrackHasBindPoseKeys(
			BoneAnimationTrack* track,
			const BoneLocalPose& bindPose)
		{
			if (track == nullptr)
				return false;

			bool changed = false;
			if (track->TranslationKeys.empty())
			{
				track->TranslationKeys.push_back(BoneTranslationKey{ 0.0f, bindPose.Translation });
				changed = true;
			}
			if (track->RotationKeys.empty())
			{
				track->RotationKeys.push_back(BoneRotationKey{ 0.0f, NormalizeQuaternionValue(bindPose.Rotation) });
				changed = true;
			}
			if (track->ScaleKeys.empty())
			{
				track->ScaleKeys.push_back(BoneScaleKey{ 0.0f, bindPose.Scale });
				changed = true;
			}

			if (changed)
			{
				SortKeysByTime(track->TranslationKeys);
				SortKeysByTime(track->RotationKeys);
				SortKeysByTime(track->ScaleKeys);
			}
			return changed;
		}
	}

	BoneAnimationTrack* AnimationClipEditing::FindTrack(
		AnimationClipDesc& clip,
		const std::wstring& boneName,
		std::int32_t boneIndex)
	{
		for (BoneAnimationTrack& track : clip.Tracks)
		{
			if (boneIndex >= 0 && track.BoneIndex == boneIndex)
				return &track;
			if (!boneName.empty() && track.BoneName == boneName)
				return &track;
		}

		return nullptr;
	}

	const BoneAnimationTrack* AnimationClipEditing::FindTrack(
		const AnimationClipDesc& clip,
		const std::wstring& boneName,
		std::int32_t boneIndex)
	{
		for (const BoneAnimationTrack& track : clip.Tracks)
		{
			if (boneIndex >= 0 && track.BoneIndex == boneIndex)
				return &track;
			if (!boneName.empty() && track.BoneName == boneName)
				return &track;
		}

		return nullptr;
	}

	BoneAnimationTrack& AnimationClipEditing::FindOrCreateTrack(
		AnimationClipDesc& clip,
		const std::wstring& boneName,
		std::int32_t boneIndex)
	{
		if (BoneAnimationTrack* existingTrack = FindTrack(clip, boneName, boneIndex))
		{
			if (existingTrack->BoneName.empty())
				existingTrack->BoneName = boneName;
			if (existingTrack->BoneIndex < 0)
				existingTrack->BoneIndex = boneIndex;
			return *existingTrack;
		}

		BoneAnimationTrack track;
		track.BoneName = boneName;
		track.BoneIndex = boneIndex;
		clip.Tracks.push_back(track);
		return clip.Tracks.back();
	}

	bool AnimationClipEditing::InsertTranslationKey(
		BoneAnimationTrack& track,
		float time,
		const DirectX::XMFLOAT3& value,
		float timeEpsilon)
	{
		return AnimationClipEditingDetail::InsertOrReplaceKey(track.TranslationKeys, time, value, timeEpsilon);
	}

	bool AnimationClipEditing::InsertRotationKey(
		BoneAnimationTrack& track,
		float time,
		const DirectX::XMFLOAT4& value,
		float timeEpsilon)
	{
		return AnimationClipEditingDetail::InsertOrReplaceKey(
			track.RotationKeys,
			time,
			AnimationClipEditingDetail::NormalizeQuaternionValue(value),
			timeEpsilon);
	}

	bool AnimationClipEditing::InsertScaleKey(
		BoneAnimationTrack& track,
		float time,
		const DirectX::XMFLOAT3& value,
		float timeEpsilon)
	{
		return AnimationClipEditingDetail::InsertOrReplaceKey(track.ScaleKeys, time, value, timeEpsilon);
	}

	bool AnimationClipEditing::InsertMatrixKey(
		BoneAnimationTrack& track,
		float time,
		const DirectX::XMFLOAT4X4& value,
		float timeEpsilon)
	{
		return AnimationClipEditingDetail::InsertOrReplaceKey(track.MatrixKeys, time, value, timeEpsilon);
	}

	std::size_t AnimationClipEditing::InitializeTracksFromSkeletonTopology(
		AnimationClipDesc& clip,
		const SkeletonTopology& topology,
		bool createBindPoseKeys)
	{
		std::size_t changedTrackCount = 0;
		for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(topology.Bones.size()); ++boneIndex)
		{
			const SkeletonBone& bone = topology.Bones[boneIndex];
			BoneAnimationTrack* track = FindTrack(clip, bone.Name, static_cast<std::int32_t>(boneIndex));
			bool trackChanged = false;
			if (track == nullptr)
			{
				track = &FindOrCreateTrack(clip, bone.Name, static_cast<std::int32_t>(boneIndex));
				trackChanged = true;
			}
			else
			{
				if (track->BoneName.empty() && !bone.Name.empty())
				{
					track->BoneName = bone.Name;
					trackChanged = true;
				}
				if (track->BoneIndex < 0)
				{
					track->BoneIndex = static_cast<std::int32_t>(boneIndex);
					trackChanged = true;
				}
			}

			if (createBindPoseKeys)
			{
				trackChanged |= AnimationClipEditingDetail::EnsureTrackHasBindPoseKeys(track, bone.BindLocalPose);
			}

			if (trackChanged)
				++changedTrackCount;
		}

		return changedTrackCount;
	}

	bool AnimationClipEditing::DeleteKey(
		BoneAnimationTrack& track,
		AnimationKeyChannel channel,
		std::size_t keyIndex)
	{
		switch (channel)
		{
		case AnimationKeyChannel::Translation:
			return AnimationClipEditingDetail::DeleteKeyAt(track.TranslationKeys, keyIndex);
		case AnimationKeyChannel::Rotation:
			return AnimationClipEditingDetail::DeleteKeyAt(track.RotationKeys, keyIndex);
		case AnimationKeyChannel::Scale:
			return AnimationClipEditingDetail::DeleteKeyAt(track.ScaleKeys, keyIndex);
		case AnimationKeyChannel::Matrix:
			return AnimationClipEditingDetail::DeleteKeyAt(track.MatrixKeys, keyIndex);
		default:
			return false;
		}
	}

	bool AnimationClipEditing::MoveKey(
		BoneAnimationTrack& track,
		AnimationKeyChannel channel,
		std::size_t keyIndex,
		float newTime)
	{
		switch (channel)
		{
		case AnimationKeyChannel::Translation:
			return AnimationClipEditingDetail::MoveKeyAt(track.TranslationKeys, keyIndex, newTime);
		case AnimationKeyChannel::Rotation:
			return AnimationClipEditingDetail::MoveKeyAt(track.RotationKeys, keyIndex, newTime);
		case AnimationKeyChannel::Scale:
			return AnimationClipEditingDetail::MoveKeyAt(track.ScaleKeys, keyIndex, newTime);
		case AnimationKeyChannel::Matrix:
			return AnimationClipEditingDetail::MoveKeyAt(track.MatrixKeys, keyIndex, newTime);
		default:
			return false;
		}
	}

	bool AnimationClipEditing::InsertOrUpdateEvent(
		AnimationClipDesc& clip,
		float time,
		const std::wstring& name,
		const std::wstring& parameter,
		float timeEpsilon)
	{
		const float sanitizedTime = AnimationClipEditingDetail::SanitizeKeyTime(time);
		for (AnimationClipEvent& eventPoint : clip.Events)
		{
			if (AnimationClipEditingDetail::IsSameKeyTime(eventPoint.Time, sanitizedTime, timeEpsilon))
			{
				eventPoint.Time = sanitizedTime;
				eventPoint.Name = name;
				eventPoint.Parameter = parameter;
				SortEvents(clip);
				return false;
			}
		}

		AnimationClipEvent eventPoint;
		eventPoint.Time = sanitizedTime;
		eventPoint.Name = name;
		eventPoint.Parameter = parameter;
		clip.Events.push_back(std::move(eventPoint));
		SortEvents(clip);
		return true;
	}

	bool AnimationClipEditing::DeleteEvent(AnimationClipDesc& clip, std::size_t eventIndex)
	{
		if (eventIndex >= clip.Events.size())
			return false;

		clip.Events.erase(clip.Events.begin() + static_cast<std::ptrdiff_t>(eventIndex));
		return true;
	}

	bool AnimationClipEditing::MoveEvent(
		AnimationClipDesc& clip,
		std::size_t eventIndex,
		float newTime)
	{
		if (eventIndex >= clip.Events.size())
			return false;

		clip.Events[eventIndex].Time = AnimationClipEditingDetail::SanitizeKeyTime(newTime);
		SortEvents(clip);
		return true;
	}

	void AnimationClipEditing::SortEvents(AnimationClipDesc& clip)
	{
		AnimationClipEditingDetail::SortKeysByTime(clip.Events);
	}

	void AnimationClipEditing::SortTrackKeys(BoneAnimationTrack& track)
	{
		AnimationClipEditingDetail::SortKeysByTime(track.TranslationKeys);
		AnimationClipEditingDetail::SortKeysByTime(track.RotationKeys);
		AnimationClipEditingDetail::SortKeysByTime(track.ScaleKeys);
		AnimationClipEditingDetail::SortKeysByTime(track.MatrixKeys);
	}

	void AnimationClipEditing::SortClipKeys(AnimationClipDesc& clip)
	{
		for (BoneAnimationTrack& track : clip.Tracks)
			SortTrackKeys(track);
		SortEvents(clip);
	}

	float AnimationClipEditing::ComputeDuration(const AnimationClipDesc& clip)
	{
		float duration = 0.0f;
		for (const BoneAnimationTrack& track : clip.Tracks)
		{
			duration = (std::max)(duration, AnimationClipEditingDetail::GetLastKeyTime(track.TranslationKeys));
			duration = (std::max)(duration, AnimationClipEditingDetail::GetLastKeyTime(track.RotationKeys));
			duration = (std::max)(duration, AnimationClipEditingDetail::GetLastKeyTime(track.ScaleKeys));
			duration = (std::max)(duration, AnimationClipEditingDetail::GetLastKeyTime(track.MatrixKeys));
		}
		for (const AnimationClipEvent& eventPoint : clip.Events)
			duration = (std::max)(duration, AnimationClipEditingDetail::SanitizeKeyTime(eventPoint.Time));
		return duration;
	}

	float AnimationClipEditing::RecalculateDuration(AnimationClipDesc& clip)
	{
		clip.Duration = ComputeDuration(clip);
		return clip.Duration;
	}

	void AnimationClipEditing::NormalizeClip(AnimationClipDesc& clip, bool recalculateDuration)
	{
		for (BoneAnimationTrack& track : clip.Tracks)
		{
			for (BoneTranslationKey& key : track.TranslationKeys)
				key.Time = AnimationClipEditingDetail::SanitizeKeyTime(key.Time);
			for (BoneRotationKey& key : track.RotationKeys)
			{
				key.Time = AnimationClipEditingDetail::SanitizeKeyTime(key.Time);
				key.Value = AnimationClipEditingDetail::NormalizeQuaternionValue(key.Value);
			}
			for (BoneScaleKey& key : track.ScaleKeys)
				key.Time = AnimationClipEditingDetail::SanitizeKeyTime(key.Time);
			for (BoneMatrixKey& key : track.MatrixKeys)
				key.Time = AnimationClipEditingDetail::SanitizeKeyTime(key.Time);
		}

		for (AnimationClipEvent& eventPoint : clip.Events)
			eventPoint.Time = AnimationClipEditingDetail::SanitizeKeyTime(eventPoint.Time);

		SortClipKeys(clip);
		if (recalculateDuration)
			RecalculateDuration(clip);
	}
}
