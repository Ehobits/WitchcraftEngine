#include "System/Animation/Assets/AnimationClipAsset.h"

#include <utility>

namespace Witchcraft::Animation
{
	void AnimationClipAsset::SetDescription(const AnimationClipDesc& description)
	{
		m_description = description;
		RebuildCaches();
	}

	void AnimationClipAsset::SetDescription(AnimationClipDesc&& description)
	{
		m_description = std::move(description);
		RebuildCaches();
	}

	AnimationClipDesc& AnimationClipAsset::GetDescription()
	{
		MarkCacheDirty();
		return m_description;
	}

	const AnimationClipDesc& AnimationClipAsset::GetDescription() const
	{
		return m_description;
	}

	const AnimationClipCache& AnimationClipAsset::GetCache() const
	{
		return m_cache;
	}

	void AnimationClipAsset::MarkCacheDirty()
	{
		m_cache.MarkDirty();
	}

	void AnimationClipAsset::RebuildCaches()
	{
		AnimationClipCacheBuilder::Rebuild(m_description, &m_cache);
	}

	bool AnimationClipAsset::IsCacheReady() const
	{
		return m_cache.IsReady();
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
