#include "System/Animation/AnimationClipCache.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Witchcraft::Animation
{
	namespace AnimationClipCacheDetail
	{
		constexpr float kCacheTimeEpsilon = 1e-6f;

		float SanitizeTime(float time)
		{
			return std::isfinite(time) ? (std::max)(0.0f, time) : 0.0f;
		}

		float ComputeInverseDuration(float duration)
		{
			if (!std::isfinite(duration) || std::abs(duration) <= kCacheTimeEpsilon)
				return 0.0f;
			return 1.0f / duration;
		}

		bool IsFiniteFloat3(const DirectX::XMFLOAT3& value)
		{
			return
				std::isfinite(value.x) &&
				std::isfinite(value.y) &&
				std::isfinite(value.z);
		}

		bool IsFiniteFloat4(const DirectX::XMFLOAT4& value)
		{
			return
				std::isfinite(value.x) &&
				std::isfinite(value.y) &&
				std::isfinite(value.z) &&
				std::isfinite(value.w);
		}

		DirectX::XMFLOAT3 AddFloat3(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs)
		{
			return DirectX::XMFLOAT3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
		}

		DirectX::XMFLOAT3 SubtractFloat3(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs)
		{
			return DirectX::XMFLOAT3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
		}

		DirectX::XMFLOAT3 ScaleFloat3(const DirectX::XMFLOAT3& value, float scale)
		{
			return DirectX::XMFLOAT3(value.x * scale, value.y * scale, value.z * scale);
		}

		DirectX::XMFLOAT4 AddFloat4(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs)
		{
			return DirectX::XMFLOAT4(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w);
		}

		DirectX::XMFLOAT4 SubtractFloat4(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs)
		{
			return DirectX::XMFLOAT4(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w);
		}

		DirectX::XMFLOAT4 ScaleFloat4(const DirectX::XMFLOAT4& value, float scale)
		{
			return DirectX::XMFLOAT4(value.x * scale, value.y * scale, value.z * scale, value.w * scale);
		}

		DirectX::XMFLOAT4 NormalizeFloat4(const DirectX::XMFLOAT4& value)
		{
			DirectX::XMFLOAT4 result = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
			const DirectX::XMVECTOR vector = DirectX::XMLoadFloat4(&value);
			const float lengthSq = DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(vector));
			if (!std::isfinite(lengthSq) || lengthSq <= kCacheTimeEpsilon)
				return result;

			const DirectX::XMVECTOR normalized = DirectX::XMQuaternionNormalize(vector);
			DirectX::XMStoreFloat4(&result, normalized);
			return result;
		}

		DirectX::XMFLOAT4 AlignQuaternionHemisphere(
			const DirectX::XMFLOAT4& reference,
			const DirectX::XMFLOAT4& value)
		{
			const DirectX::XMFLOAT4 normalizedReference = NormalizeFloat4(reference);
			DirectX::XMFLOAT4 result = NormalizeFloat4(value);
			const DirectX::XMVECTOR refVector = DirectX::XMLoadFloat4(&normalizedReference);
			DirectX::XMVECTOR resultVector = DirectX::XMLoadFloat4(&result);
			if (DirectX::XMVectorGetX(DirectX::XMVector4Dot(refVector, resultVector)) < 0.0f)
				resultVector = DirectX::XMVectorNegate(resultVector);
			DirectX::XMStoreFloat4(&result, resultVector);
			return result;
		}

		DirectX::XMFLOAT3 ComputeFloat3KeyTangent(
			const std::vector<BoneTranslationKey>& keys,
			std::size_t keyIndex,
			float segmentDuration)
		{
			if (keys.empty() || keyIndex >= keys.size() || !std::isfinite(segmentDuration))
				return DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

			std::size_t previousIndex = keyIndex;
			std::size_t nextIndex = keyIndex;
			if (keyIndex > 0)
				previousIndex = keyIndex - 1;
			if (keyIndex + 1 < keys.size())
				nextIndex = keyIndex + 1;

			const float previousTime = SanitizeTime(keys[previousIndex].Time);
			const float nextTime = SanitizeTime(keys[nextIndex].Time);
			const float timeRange = nextTime - previousTime;
			if (std::abs(timeRange) <= kCacheTimeEpsilon)
				return DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

			const DirectX::XMFLOAT3 slope =
				ScaleFloat3(SubtractFloat3(keys[nextIndex].Value, keys[previousIndex].Value), 1.0f / timeRange);
			return ScaleFloat3(slope, (std::max)(0.0f, segmentDuration));
		}

		DirectX::XMFLOAT3 ComputeFloat3KeyTangent(
			const std::vector<BoneScaleKey>& keys,
			std::size_t keyIndex,
			float segmentDuration)
		{
			if (keys.empty() || keyIndex >= keys.size() || !std::isfinite(segmentDuration))
				return DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

			std::size_t previousIndex = keyIndex;
			std::size_t nextIndex = keyIndex;
			if (keyIndex > 0)
				previousIndex = keyIndex - 1;
			if (keyIndex + 1 < keys.size())
				nextIndex = keyIndex + 1;

			const float previousTime = SanitizeTime(keys[previousIndex].Time);
			const float nextTime = SanitizeTime(keys[nextIndex].Time);
			const float timeRange = nextTime - previousTime;
			if (std::abs(timeRange) <= kCacheTimeEpsilon)
				return DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

			const DirectX::XMFLOAT3 slope =
				ScaleFloat3(SubtractFloat3(keys[nextIndex].Value, keys[previousIndex].Value), 1.0f / timeRange);
			return ScaleFloat3(slope, (std::max)(0.0f, segmentDuration));
		}

		DirectX::XMFLOAT4 ComputeFloat4KeyTangent(
			const std::vector<BoneRotationKey>& keys,
			std::size_t keyIndex,
			float segmentDuration)
		{
			if (keys.empty() || keyIndex >= keys.size() || !std::isfinite(segmentDuration))
				return DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

			std::size_t previousIndex = keyIndex;
			std::size_t nextIndex = keyIndex;
			if (keyIndex > 0)
				previousIndex = keyIndex - 1;
			if (keyIndex + 1 < keys.size())
				nextIndex = keyIndex + 1;

			const float previousTime = SanitizeTime(keys[previousIndex].Time);
			const float nextTime = SanitizeTime(keys[nextIndex].Time);
			const float timeRange = nextTime - previousTime;
			if (std::abs(timeRange) <= kCacheTimeEpsilon)
				return DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

			const DirectX::XMFLOAT4 startValue = NormalizeFloat4(keys[previousIndex].Value);
			const DirectX::XMFLOAT4 endValue = AlignQuaternionHemisphere(startValue, keys[nextIndex].Value);
			const DirectX::XMFLOAT4 slope = ScaleFloat4(SubtractFloat4(endValue, startValue), 1.0f / timeRange);
			return ScaleFloat4(slope, (std::max)(0.0f, segmentDuration));
		}

		template<typename TKey>
		AnimationFloat3CubicSplineSegmentCache BuildFloat3CubicSplineSegment(
			const std::vector<TKey>& keys,
			const AnimationKeySegmentCache& segment,
			std::size_t segmentIndex)
		{
			AnimationFloat3CubicSplineSegmentCache result;
			result.SegmentIndex = segmentIndex;
			if (!segment.HasValidTimeRange ||
				segment.StartKeyIndex >= keys.size() ||
				segment.EndKeyIndex >= keys.size())
			{
				return result;
			}

			result.StartValue = keys[segment.StartKeyIndex].Value;
			result.EndValue = keys[segment.EndKeyIndex].Value;
			result.StartTangent = ComputeFloat3KeyTangent(keys, segment.StartKeyIndex, segment.Duration);
			result.EndTangent = ComputeFloat3KeyTangent(keys, segment.EndKeyIndex, segment.Duration);

			const DirectX::XMFLOAT3 twoStart = ScaleFloat3(result.StartValue, 2.0f);
			const DirectX::XMFLOAT3 twoEnd = ScaleFloat3(result.EndValue, 2.0f);
			result.CoeffA = AddFloat3(
				AddFloat3(SubtractFloat3(twoStart, twoEnd), result.StartTangent),
				result.EndTangent);
			result.CoeffB = SubtractFloat3(
				AddFloat3(SubtractFloat3(ScaleFloat3(result.EndValue, 3.0f), ScaleFloat3(result.StartValue, 3.0f)), ScaleFloat3(result.StartTangent, -2.0f)),
				result.EndTangent);
			result.CoeffC = result.StartTangent;
			result.CoeffD = result.StartValue;
			result.Valid =
				IsFiniteFloat3(result.StartValue) &&
				IsFiniteFloat3(result.EndValue) &&
				IsFiniteFloat3(result.StartTangent) &&
				IsFiniteFloat3(result.EndTangent) &&
				IsFiniteFloat3(result.CoeffA) &&
				IsFiniteFloat3(result.CoeffB) &&
				IsFiniteFloat3(result.CoeffC) &&
				IsFiniteFloat3(result.CoeffD);
			return result;
		}

		template<typename TKey>
		AnimationQuaternionCubicSplineSegmentCache BuildQuaternionCubicSplineSegment(
			const std::vector<TKey>& keys,
			const AnimationKeySegmentCache& segment,
			std::size_t segmentIndex)
		{
			AnimationQuaternionCubicSplineSegmentCache result;
			result.SegmentIndex = segmentIndex;
			if (!segment.HasValidTimeRange ||
				segment.StartKeyIndex >= keys.size() ||
				segment.EndKeyIndex >= keys.size())
			{
				return result;
			}

			result.StartValue = NormalizeFloat4(keys[segment.StartKeyIndex].Value);
			result.EndValue = AlignQuaternionHemisphere(result.StartValue, keys[segment.EndKeyIndex].Value);
			result.StartTangent = ComputeFloat4KeyTangent(keys, segment.StartKeyIndex, segment.Duration);
			result.EndTangent = ComputeFloat4KeyTangent(keys, segment.EndKeyIndex, segment.Duration);
			result.CoeffA = AddFloat4(
				AddFloat4(SubtractFloat4(ScaleFloat4(result.StartValue, 2.0f), ScaleFloat4(result.EndValue, 2.0f)), result.StartTangent),
				result.EndTangent);
			result.CoeffB = AddFloat4(
				AddFloat4(SubtractFloat4(ScaleFloat4(result.EndValue, 3.0f), ScaleFloat4(result.StartValue, 3.0f)), ScaleFloat4(result.StartTangent, -2.0f)),
				ScaleFloat4(result.EndTangent, -1.0f));
			result.CoeffC = result.StartTangent;
			result.CoeffD = result.StartValue;
			result.Valid =
				IsFiniteFloat4(result.StartValue) &&
				IsFiniteFloat4(result.EndValue) &&
				IsFiniteFloat4(result.StartTangent) &&
				IsFiniteFloat4(result.EndTangent) &&
				IsFiniteFloat4(result.CoeffA) &&
				IsFiniteFloat4(result.CoeffB) &&
				IsFiniteFloat4(result.CoeffC) &&
				IsFiniteFloat4(result.CoeffD);
			return result;
		}

		void AccumulateTrackCacheStats(AnimationTrackCache* trackCache)
		{
			if (trackCache == nullptr)
				return;

			trackCache->KeyCount =
				trackCache->Translation.KeyCount +
				trackCache->Rotation.KeyCount +
				trackCache->Scale.KeyCount +
				trackCache->Matrix.KeyCount;
			trackCache->SegmentCount =
				trackCache->Translation.SegmentCount +
				trackCache->Rotation.SegmentCount +
				trackCache->Scale.SegmentCount +
				trackCache->Matrix.SegmentCount;
			trackCache->CubicSplineSegmentCount =
				trackCache->Translation.CubicSplineSegmentCount +
				trackCache->Rotation.CubicSplineSegmentCount +
				trackCache->Scale.CubicSplineSegmentCount +
				trackCache->Matrix.CubicSplineSegmentCount;
			trackCache->HasKeys = trackCache->KeyCount > 0;
			trackCache->HasCubicSpline =
				trackCache->Translation.HasCubicSpline ||
				trackCache->Rotation.HasCubicSpline ||
				trackCache->Scale.HasCubicSpline ||
				trackCache->Matrix.HasCubicSpline;
		}

		template<typename TKey>
		void BuildChannelCacheFromKeys(
			AnimationCacheChannelKind channelKind,
			const std::vector<TKey>& keys,
			AnimationChannelCache* outCache)
		{
			if (outCache == nullptr)
				return;

			outCache->Clear();
			outCache->ChannelKind = channelKind;
			outCache->KeyCount = keys.size();
			outCache->HasKeys = !keys.empty();
			if (keys.empty())
				return;

			outCache->FirstKeyTime = SanitizeTime(keys.front().Time);
			outCache->LastKeyTime = outCache->FirstKeyTime;
			for (const TKey& key : keys)
			{
				const float sanitizedTime = SanitizeTime(key.Time);
				outCache->FirstKeyTime = (std::min)(outCache->FirstKeyTime, sanitizedTime);
				outCache->LastKeyTime = (std::max)(outCache->LastKeyTime, sanitizedTime);
			}
			if (keys.size() < 2)
				return;

			outCache->Segments.reserve(keys.size() - 1);
			for (std::size_t keyIndex = 0; keyIndex + 1 < keys.size(); ++keyIndex)
			{
				AnimationKeySegmentCache segment;
				segment.StartKeyIndex = keyIndex;
				segment.EndKeyIndex = keyIndex + 1;
				segment.StartTime = SanitizeTime(keys[keyIndex].Time);
				segment.EndTime = SanitizeTime(keys[keyIndex + 1].Time);
				segment.Duration = segment.EndTime - segment.StartTime;
				segment.InverseDuration = ComputeInverseDuration(segment.Duration);
				segment.Interpolation = keys[keyIndex].Interpolation;
				segment.HasNextKey = true;
				segment.HasValidTimeRange = segment.Duration >= 0.0f;
				segment.UsesCubicSpline = segment.Interpolation == AnimationInterpolationType::CubicSpline;
				if (segment.UsesCubicSpline)
				{
					segment.CubicSplineDataIndex = outCache->CubicSplineSegmentIndices.size();
					outCache->CubicSplineSegmentIndices.push_back(outCache->Segments.size());
				}
				outCache->Segments.push_back(segment);
			}

			outCache->SegmentCount = outCache->Segments.size();
			outCache->CubicSplineSegmentCount = outCache->CubicSplineSegmentIndices.size();
			outCache->HasSegments = outCache->SegmentCount > 0;
			outCache->HasCubicSpline = outCache->CubicSplineSegmentCount > 0;
		}

		template<typename TKey>
		void BuildFloat3CubicSplineCacheFromKeys(
			const std::vector<TKey>& keys,
			AnimationChannelCache* outCache)
		{
			if (outCache == nullptr || outCache->CubicSplineSegmentIndices.empty())
				return;

			outCache->Float3CubicSplineSegments.reserve(outCache->CubicSplineSegmentIndices.size());
			for (std::size_t segmentIndex : outCache->CubicSplineSegmentIndices)
			{
				if (segmentIndex >= outCache->Segments.size())
					continue;

				const AnimationKeySegmentCache& segment = outCache->Segments[segmentIndex];
				AnimationFloat3CubicSplineSegmentCache cubicData =
					BuildFloat3CubicSplineSegment(keys, segment, segmentIndex);
				if (cubicData.Valid)
					outCache->HasFloat3CubicSplineData = true;
				outCache->Float3CubicSplineSegments.push_back(cubicData);
			}
		}

		template<typename TKey>
		void BuildQuaternionCubicSplineCacheFromKeys(
			const std::vector<TKey>& keys,
			AnimationChannelCache* outCache)
		{
			if (outCache == nullptr || outCache->CubicSplineSegmentIndices.empty())
				return;

			outCache->QuaternionCubicSplineSegments.reserve(outCache->CubicSplineSegmentIndices.size());
			for (std::size_t segmentIndex : outCache->CubicSplineSegmentIndices)
			{
				if (segmentIndex >= outCache->Segments.size())
					continue;

				const AnimationKeySegmentCache& segment = outCache->Segments[segmentIndex];
				AnimationQuaternionCubicSplineSegmentCache cubicData =
					BuildQuaternionCubicSplineSegment(keys, segment, segmentIndex);
				if (cubicData.Valid)
					outCache->HasQuaternionCubicSplineData = true;
				outCache->QuaternionCubicSplineSegments.push_back(cubicData);
			}
		}
	}

	void AnimationChannelCache::Clear()
	{
		KeyCount = 0;
		SegmentCount = 0;
		CubicSplineSegmentCount = 0;
		FirstKeyTime = 0.0f;
		LastKeyTime = 0.0f;
		HasKeys = false;
		HasSegments = false;
		HasCubicSpline = false;
		HasFloat3CubicSplineData = false;
		HasQuaternionCubicSplineData = false;
		Segments.clear();
		CubicSplineSegmentIndices.clear();
		Float3CubicSplineSegments.clear();
		QuaternionCubicSplineSegments.clear();
	}

	const AnimationKeySegmentCache* AnimationChannelCache::FindSegment(float time, std::size_t* outSegmentIndex) const
	{
		if (outSegmentIndex != nullptr)
			*outSegmentIndex = static_cast<std::size_t>(-1);

		if (Segments.empty() || !std::isfinite(time))
			return nullptr;

		if (time < Segments.front().StartTime || time > Segments.back().EndTime)
			return nullptr;

		std::size_t left = 0;
		std::size_t right = Segments.size();
		std::size_t resolvedIndex = Segments.size();
		while (left < right)
		{
			const std::size_t middle = left + (right - left) / 2;
			if (Segments[middle].StartTime <= time)
			{
				resolvedIndex = middle;
				left = middle + 1;
			}
			else
			{
				right = middle;
			}
		}

		if (resolvedIndex >= Segments.size())
			return nullptr;

		const AnimationKeySegmentCache& segment = Segments[resolvedIndex];
		if (!segment.HasValidTimeRange || time < segment.StartTime || time > segment.EndTime)
			return nullptr;

		if (outSegmentIndex != nullptr)
			*outSegmentIndex = resolvedIndex;
		return &segment;
	}

	const AnimationFloat3CubicSplineSegmentCache* AnimationChannelCache::FindFloat3CubicSplineData(
		const AnimationKeySegmentCache& segment) const
	{
		if (!HasFloat3CubicSplineData ||
			segment.CubicSplineDataIndex == AnimationKeySegmentCache::InvalidCubicSplineDataIndex ||
			segment.CubicSplineDataIndex >= Float3CubicSplineSegments.size())
		{
			return nullptr;
		}

		const AnimationFloat3CubicSplineSegmentCache& cubicData =
			Float3CubicSplineSegments[segment.CubicSplineDataIndex];
		if (!cubicData.Valid ||
			cubicData.SegmentIndex >= Segments.size() ||
			&Segments[cubicData.SegmentIndex] != &segment)
		{
			return nullptr;
		}

		return &cubicData;
	}

	const AnimationQuaternionCubicSplineSegmentCache* AnimationChannelCache::FindQuaternionCubicSplineData(
		const AnimationKeySegmentCache& segment) const
	{
		if (!HasQuaternionCubicSplineData ||
			segment.CubicSplineDataIndex == AnimationKeySegmentCache::InvalidCubicSplineDataIndex ||
			segment.CubicSplineDataIndex >= QuaternionCubicSplineSegments.size())
		{
			return nullptr;
		}

		const AnimationQuaternionCubicSplineSegmentCache& cubicData =
			QuaternionCubicSplineSegments[segment.CubicSplineDataIndex];
		if (!cubicData.Valid ||
			cubicData.SegmentIndex >= Segments.size() ||
			&Segments[cubicData.SegmentIndex] != &segment)
		{
			return nullptr;
		}

		return &cubicData;
	}

	void AnimationTrackCache::Clear()
	{
		BoneName.clear();
		BoneIndex = -1;
		KeyCount = 0;
		SegmentCount = 0;
		CubicSplineSegmentCount = 0;
		HasKeys = false;
		HasCubicSpline = false;
		Translation.Clear();
		Rotation.Clear();
		Scale.Clear();
		Matrix.Clear();
		Translation.ChannelKind = AnimationCacheChannelKind::Translation;
		Rotation.ChannelKind = AnimationCacheChannelKind::Rotation;
		Scale.ChannelKind = AnimationCacheChannelKind::Scale;
		Matrix.ChannelKind = AnimationCacheChannelKind::Matrix;
	}

	void AnimationClipCache::Clear()
	{
		Built = false;
		Dirty = true;
		HasCubicSpline = false;
		Duration = 0.0f;
		TicksPerSecond = 0.0f;
		TrackCount = 0;
		TotalKeyCount = 0;
		TotalSegmentCount = 0;
		TotalCubicSplineSegmentCount = 0;
		Tracks.clear();
	}

	void AnimationClipCache::MarkDirty()
	{
		Dirty = true;
	}

	bool AnimationClipCache::IsReady() const
	{
		return Built && !Dirty;
	}

	void AnimationClipCacheBuilder::Rebuild(const AnimationClipDesc& clip, AnimationClipCache* outCache)
	{
		if (outCache == nullptr)
			return;

		outCache->Clear();
		outCache->Duration = std::isfinite(clip.Duration) ? (std::max)(0.0f, clip.Duration) : 0.0f;
		outCache->TicksPerSecond = std::isfinite(clip.TicksPerSecond) ? (std::max)(0.0f, clip.TicksPerSecond) : 0.0f;
		outCache->TrackCount = clip.Tracks.size();
		outCache->Tracks.reserve(clip.Tracks.size());

		for (const BoneAnimationTrack& track : clip.Tracks)
		{
			AnimationTrackCache trackCache;
			trackCache.BoneName = track.BoneName;
			trackCache.BoneIndex = track.BoneIndex;

			AnimationClipCacheDetail::BuildChannelCacheFromKeys(
				AnimationCacheChannelKind::Translation,
				track.TranslationKeys,
				&trackCache.Translation);
			AnimationClipCacheDetail::BuildFloat3CubicSplineCacheFromKeys(
				track.TranslationKeys,
				&trackCache.Translation);
			AnimationClipCacheDetail::BuildChannelCacheFromKeys(
				AnimationCacheChannelKind::Rotation,
				track.RotationKeys,
				&trackCache.Rotation);
			AnimationClipCacheDetail::BuildQuaternionCubicSplineCacheFromKeys(
				track.RotationKeys,
				&trackCache.Rotation);
			AnimationClipCacheDetail::BuildChannelCacheFromKeys(
				AnimationCacheChannelKind::Scale,
				track.ScaleKeys,
				&trackCache.Scale);
			AnimationClipCacheDetail::BuildFloat3CubicSplineCacheFromKeys(
				track.ScaleKeys,
				&trackCache.Scale);
			AnimationClipCacheDetail::BuildChannelCacheFromKeys(
				AnimationCacheChannelKind::Matrix,
				track.MatrixKeys,
				&trackCache.Matrix);
			AnimationClipCacheDetail::AccumulateTrackCacheStats(&trackCache);

			outCache->TotalKeyCount += trackCache.KeyCount;
			outCache->TotalSegmentCount += trackCache.SegmentCount;
			outCache->TotalCubicSplineSegmentCount += trackCache.CubicSplineSegmentCount;
			outCache->HasCubicSpline = outCache->HasCubicSpline || trackCache.HasCubicSpline;
			outCache->Tracks.push_back(std::move(trackCache));
		}

		outCache->Built = true;
		outCache->Dirty = false;
	}
}
