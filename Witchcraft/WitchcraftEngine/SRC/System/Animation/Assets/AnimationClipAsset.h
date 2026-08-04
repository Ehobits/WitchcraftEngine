#pragma once

#include "Common/AnimationSharedTypes.h"

namespace Witchcraft::Animation
{
	class AnimationClipAsset
	{
	public:
		AnimationClipAsset() = default;

		void SetDescription(const AnimationClipDesc& description);
		AnimationClipDesc& GetDescription();
		const AnimationClipDesc& GetDescription() const;

		bool IsValid() const;
		std::uint32_t GetTrackCount() const;
		float GetDurationSeconds() const;

	private:
		AnimationClipDesc m_description;
	};
}
