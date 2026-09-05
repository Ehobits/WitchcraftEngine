#pragma once

#include <cstddef>
#include <xstring>

class AnimatorComponent;

namespace Witchcraft::Animation
{
	class AnimationPlaybackController
	{
	public:
		static bool FindLayerIndex(const AnimatorComponent& animator, const std::wstring& layerName, std::size_t* outLayerIndex);
		static bool Play(AnimatorComponent* animator, const std::wstring& clipAssetPath, const std::wstring& layerName = L"", float startTime = 0.0f, bool loop = true);
		static bool CrossFade(AnimatorComponent* animator, const std::wstring& clipAssetPath, float fadeDuration, const std::wstring& layerName = L"", float startTime = 0.0f, bool loop = true);
		static bool Stop(AnimatorComponent* animator, const std::wstring& layerName = L"");
		static bool SetLayerWeight(AnimatorComponent* animator, const std::wstring& layerName, float weight);
		static bool SetLayerSpeed(AnimatorComponent* animator, const std::wstring& layerName, float speed);
		static bool SetLayerEnabled(AnimatorComponent* animator, const std::wstring& layerName, bool enabled);

	private:
		static bool ResolveLayerIndex(AnimatorComponent* animator, const std::wstring& layerName, bool createIfMissing, std::size_t* outLayerIndex);
	};
}
