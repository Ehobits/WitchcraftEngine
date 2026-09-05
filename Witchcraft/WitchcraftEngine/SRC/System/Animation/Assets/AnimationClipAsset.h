#pragma once

#include "System/Animation/AnimationClipCache.h"
#include "Common/AnimationSharedTypes.h"

namespace Witchcraft::Animation
{
	class AnimationClipAsset
	{
	public:
		AnimationClipAsset() = default;

		void SetDescription(const AnimationClipDesc& description);
		void SetDescription(AnimationClipDesc&& description);
		AnimationClipDesc& GetDescription();
		const AnimationClipDesc& GetDescription() const;
		const AnimationClipCache& GetCache() const;

		void MarkCacheDirty();
		void RebuildCaches();
		bool IsCacheReady() const;
		bool IsValid() const;
		std::uint32_t GetTrackCount() const;
		float GetDurationSeconds() const;

	private:
		AnimationClipDesc m_description;
		AnimationClipCache m_cache;
	};
}
