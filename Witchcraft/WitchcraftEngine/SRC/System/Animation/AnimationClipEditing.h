#pragma once

#include <cstddef>
#include <cstdint>
#include <xstring>

#include <DirectXMath.h>

#include "Common/AnimationSharedTypes.h"
#include "Common/SkeletonSharedTypes.h"

namespace Witchcraft::Animation
{
	enum class AnimationKeyChannel
	{
		Translation,
		Rotation,
		Scale,
		Matrix
	};

	class AnimationClipEditing
	{
	public:
		static constexpr float DefaultKeyTimeEpsilon = 1e-4f;

		static BoneAnimationTrack* FindTrack(
			AnimationClipDesc& clip,
			const std::wstring& boneName,
			std::int32_t boneIndex);
		static const BoneAnimationTrack* FindTrack(
			const AnimationClipDesc& clip,
			const std::wstring& boneName,
			std::int32_t boneIndex);
		static BoneAnimationTrack& FindOrCreateTrack(
			AnimationClipDesc& clip,
			const std::wstring& boneName,
			std::int32_t boneIndex);

		static bool InsertTranslationKey(
			BoneAnimationTrack& track,
			float time,
			const DirectX::XMFLOAT3& value,
			float timeEpsilon = DefaultKeyTimeEpsilon);
		static bool InsertRotationKey(
			BoneAnimationTrack& track,
			float time,
			const DirectX::XMFLOAT4& value,
			float timeEpsilon = DefaultKeyTimeEpsilon);
		static bool InsertScaleKey(
			BoneAnimationTrack& track,
			float time,
			const DirectX::XMFLOAT3& value,
			float timeEpsilon = DefaultKeyTimeEpsilon);
		static bool InsertMatrixKey(
			BoneAnimationTrack& track,
			float time,
			const DirectX::XMFLOAT4X4& value,
			float timeEpsilon = DefaultKeyTimeEpsilon);

		static std::size_t InitializeTracksFromSkeletonTopology(
			AnimationClipDesc& clip,
			const SkeletonTopology& topology,
			bool createBindPoseKeys = true);

		static bool DeleteKey(
			BoneAnimationTrack& track,
			AnimationKeyChannel channel,
			std::size_t keyIndex);
		static bool MoveKey(
			BoneAnimationTrack& track,
			AnimationKeyChannel channel,
			std::size_t keyIndex,
			float newTime);

		static bool InsertOrUpdateEvent(
			AnimationClipDesc& clip,
			float time,
			const std::wstring& name,
			const std::wstring& parameter = L"",
			float timeEpsilon = DefaultKeyTimeEpsilon);
		static bool DeleteEvent(
			AnimationClipDesc& clip,
			std::size_t eventIndex);
		static bool MoveEvent(
			AnimationClipDesc& clip,
			std::size_t eventIndex,
			float newTime);

		static void SortEvents(AnimationClipDesc& clip);
		static void SortTrackKeys(BoneAnimationTrack& track);
		static void SortClipKeys(AnimationClipDesc& clip);
		static float ComputeDuration(const AnimationClipDesc& clip);
		static float RecalculateDuration(AnimationClipDesc& clip);
		static void NormalizeClip(AnimationClipDesc& clip, bool recalculateDuration);
	};
}
