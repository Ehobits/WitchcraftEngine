#include "System/Animation/Assets/AnimationClipAsset.h"

namespace Witchcraft::Animation
{
	void AnimationClipAsset::SetDescription(const AnimationClipDesc& description)
	{
		m_description = description;
	}

	AnimationClipDesc& AnimationClipAsset::GetDescription()
	{
		return m_description;
	}

	const AnimationClipDesc& AnimationClipAsset::GetDescription() const
	{
		return m_description;
	}

	bool AnimationClipAsset::IsValid() const
	{
		if (m_description.Duration < 0.0f || m_description.TicksPerSecond < 0.0f)
			return false;

		for (const BoneAnimationTrack& track : m_description.Tracks)
		{
			if (track.BoneName.empty() && track.BoneIndex < 0)
				return false;
		}

		return true;
	}

	std::uint32_t AnimationClipAsset::GetTrackCount() const
	{
		return static_cast<std::uint32_t>(m_description.Tracks.size());
	}

	float AnimationClipAsset::GetDurationSeconds() const
	{
		if (m_description.TicksPerSecond <= 0.0f)
			return m_description.Duration;

		return m_description.Duration / m_description.TicksPerSecond;
	}
}
