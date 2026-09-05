#include "System/Animation/AnimationPlaybackController.h"

#include "ECS/Component/AnimatorComponent.h"

namespace Witchcraft::Animation
{
	bool AnimationPlaybackController::FindLayerIndex(
		const AnimatorComponent& animator,
		const std::wstring& layerName,
		std::size_t* outLayerIndex)
	{
		if (outLayerIndex == nullptr)
			return false;
		*outLayerIndex = 0u;

		const auto& layers = animator.GetLayers();
		if (layers.empty())
			return false;
		if (layerName.empty())
			return true;

		for (std::size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex)
		{
			if (layers[layerIndex].Name == layerName)
			{
				*outLayerIndex = layerIndex;
				return true;
			}
		}
		return false;
	}

	bool AnimationPlaybackController::ResolveLayerIndex(
		AnimatorComponent* animator,
		const std::wstring& layerName,
		bool createIfMissing,
		std::size_t* outLayerIndex)
	{
		if (animator == nullptr || outLayerIndex == nullptr)
			return false;
		if (FindLayerIndex(*animator, layerName, outLayerIndex))
			return true;
		if (!createIfMissing)
			return false;

		*outLayerIndex = animator->AddLayer(layerName);
		return true;
	}

	bool AnimationPlaybackController::Play(
		AnimatorComponent* animator,
		const std::wstring& clipAssetPath,
		const std::wstring& layerName,
		float startTime,
		bool loop)
	{
		std::size_t layerIndex = 0u;
		if (!ResolveLayerIndex(animator, layerName, true, &layerIndex))
			return false;
		if (!animator->SetLayerClipAssetPath(layerIndex, clipAssetPath))
			return false;
		(void)animator->SetLayerTime(layerIndex, startTime);
		(void)animator->SetLayerLoop(layerIndex, loop);
		(void)animator->ClearLayerTransition(layerIndex);
		return animator->PlayLayer(layerIndex);
	}

	bool AnimationPlaybackController::CrossFade(
		AnimatorComponent* animator,
		const std::wstring& clipAssetPath,
		float fadeDuration,
		const std::wstring& layerName,
		float startTime,
		bool loop)
	{
		std::size_t layerIndex = 0u;
		if (!ResolveLayerIndex(animator, layerName, true, &layerIndex))
			return false;
		AnimatorComponent::AnimationLayer* layer = animator->GetLayer(layerIndex);
		if (layer == nullptr)
			return false;
		if (layer->ClipAssetPath.empty() || fadeDuration <= 0.0f)
			return Play(animator, clipAssetPath, layerName, startTime, loop);
		(void)animator->SetLayerLoop(layerIndex, loop);
		return animator->CrossFadeLayerTo(layerIndex, clipAssetPath, fadeDuration, startTime);
	}

	bool AnimationPlaybackController::Stop(AnimatorComponent* animator, const std::wstring& layerName)
	{
		std::size_t layerIndex = 0u;
		return ResolveLayerIndex(animator, layerName, false, &layerIndex) && animator->StopLayer(layerIndex);
	}

	bool AnimationPlaybackController::SetLayerWeight(AnimatorComponent* animator, const std::wstring& layerName, float weight)
	{
		std::size_t layerIndex = 0u;
		return ResolveLayerIndex(animator, layerName, false, &layerIndex) && animator->SetLayerWeight(layerIndex, weight);
	}

	bool AnimationPlaybackController::SetLayerSpeed(AnimatorComponent* animator, const std::wstring& layerName, float speed)
	{
		std::size_t layerIndex = 0u;
		return ResolveLayerIndex(animator, layerName, false, &layerIndex) && animator->SetLayerSpeed(layerIndex, speed);
	}

	bool AnimationPlaybackController::SetLayerEnabled(AnimatorComponent* animator, const std::wstring& layerName, bool enabled)
	{
		std::size_t layerIndex = 0u;
		return ResolveLayerIndex(animator, layerName, false, &layerIndex) && animator->SetLayerEnabled(layerIndex, enabled);
	}
}
