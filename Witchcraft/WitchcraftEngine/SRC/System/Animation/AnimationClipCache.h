#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <xstring>

#include <DirectXMath.h>

#include "Common/AnimationSharedTypes.h"

namespace Witchcraft::Animation
{
	enum class AnimationCacheChannelKind : std::uint8_t
	{
		Translation,
		Rotation,
		Scale,
		Matrix
	};

	struct AnimationFloat3CubicSplineSegmentCache
	{
		std::size_t SegmentIndex = 0;
		DirectX::XMFLOAT3 StartValue = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 EndValue = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 StartTangent = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 EndTangent = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 CoeffA = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 CoeffB = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 CoeffC = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 CoeffD = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		bool Valid = false;
	};

	struct AnimationQuaternionCubicSplineSegmentCache
	{
		std::size_t SegmentIndex = 0;
		DirectX::XMFLOAT4 StartValue = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		DirectX::XMFLOAT4 EndValue = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		DirectX::XMFLOAT4 StartTangent = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4 EndTangent = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4 CoeffA = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4 CoeffB = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4 CoeffC = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT4 CoeffD = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		bool Valid = false;
	};

	struct AnimationKeySegmentCache
	{
		static constexpr std::size_t InvalidCubicSplineDataIndex = static_cast<std::size_t>(-1);

		std::size_t StartKeyIndex = 0;
		std::size_t EndKeyIndex = 0;
		std::size_t CubicSplineDataIndex = InvalidCubicSplineDataIndex;
		float StartTime = 0.0f;
		float EndTime = 0.0f;
		float Duration = 0.0f;
		float InverseDuration = 0.0f;
		AnimationInterpolationType Interpolation = AnimationInterpolationType::Linear;
		bool HasNextKey = false;
		bool HasValidTimeRange = false;
		bool UsesCubicSpline = false;
	};

	struct AnimationChannelCache
	{
		AnimationCacheChannelKind ChannelKind = AnimationCacheChannelKind::Translation;
		std::size_t KeyCount = 0;
		std::size_t SegmentCount = 0;
		std::size_t CubicSplineSegmentCount = 0;
		float FirstKeyTime = 0.0f;
		float LastKeyTime = 0.0f;
		bool HasKeys = false;
		bool HasSegments = false;
		bool HasCubicSpline = false;
		bool HasFloat3CubicSplineData = false;
		bool HasQuaternionCubicSplineData = false;
		std::vector<AnimationKeySegmentCache> Segments;
		std::vector<std::size_t> CubicSplineSegmentIndices;
		std::vector<AnimationFloat3CubicSplineSegmentCache> Float3CubicSplineSegments;
		std::vector<AnimationQuaternionCubicSplineSegmentCache> QuaternionCubicSplineSegments;

		void Clear();
		const AnimationKeySegmentCache* FindSegment(float time, std::size_t* outSegmentIndex = nullptr) const;
		const AnimationFloat3CubicSplineSegmentCache* FindFloat3CubicSplineData(
			const AnimationKeySegmentCache& segment) const;
		const AnimationQuaternionCubicSplineSegmentCache* FindQuaternionCubicSplineData(
			const AnimationKeySegmentCache& segment) const;
	};

	struct AnimationTrackCache
	{
		std::wstring BoneName;
		std::int32_t BoneIndex = -1;
		std::size_t KeyCount = 0;
		std::size_t SegmentCount = 0;
		std::size_t CubicSplineSegmentCount = 0;
		bool HasKeys = false;
		bool HasCubicSpline = false;
		AnimationChannelCache Translation = AnimationChannelCache{ AnimationCacheChannelKind::Translation };
		AnimationChannelCache Rotation = AnimationChannelCache{ AnimationCacheChannelKind::Rotation };
		AnimationChannelCache Scale = AnimationChannelCache{ AnimationCacheChannelKind::Scale };
		AnimationChannelCache Matrix = AnimationChannelCache{ AnimationCacheChannelKind::Matrix };

		void Clear();
	};

	struct AnimationClipCache
	{
		bool Built = false;
		bool Dirty = true;
		bool HasCubicSpline = false;
		float Duration = 0.0f;
		float TicksPerSecond = 0.0f;
		std::size_t TrackCount = 0;
		std::size_t TotalKeyCount = 0;
		std::size_t TotalSegmentCount = 0;
		std::size_t TotalCubicSplineSegmentCount = 0;
		std::vector<AnimationTrackCache> Tracks;

		void Clear();
		void MarkDirty();
		bool IsReady() const;
	};

	class AnimationClipCacheBuilder
	{
	public:
		static void Rebuild(const AnimationClipDesc& clip, AnimationClipCache* outCache);
	};
}
