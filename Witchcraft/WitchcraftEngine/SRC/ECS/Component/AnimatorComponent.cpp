#include "AnimatorComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>

float AnimatorComponent::SanitizeNonNegative(float value)
{
	return std::isfinite(value) ? (std::max)(0.0f, value) : 0.0f;
}

float AnimatorComponent::SanitizeWeight(float value)
{
	if (!std::isfinite(value))
		return 0.0f;
	return (std::max)(0.0f, (std::min)(1.0f, value));
}

std::wstring AnimatorComponent::BuildDefaultLayerName(std::size_t layerIndex)
{
	if (layerIndex == 0)
		return L"Base";
	return L"Layer " + std::to_wstring(layerIndex);
}

const std::vector<AnimatorComponent::AnimationLayer>& AnimatorComponent::GetLayers() const
{
	return mLayers;
}

std::vector<AnimatorComponent::AnimationLayer>& AnimatorComponent::GetMutableLayers()
{
	return mLayers;
}

bool AnimatorComponent::HasPlayableLayers() const
{
	for (const AnimationLayer& layer : mLayers)
	{
		if (layer.Enabled && (!layer.ClipAssetPath.empty() || !layer.TransitionClipAssetPath.empty()))
			return true;
	}
	return false;
}

bool AnimatorComponent::ShouldEvaluateWhenPaused() const
{
	return mEvaluateWhenPaused;
}

void AnimatorComponent::SetEvaluateWhenPaused(bool evaluateWhenPaused)
{
	mEvaluateWhenPaused = evaluateWhenPaused;
}

std::size_t AnimatorComponent::AddLayer(const std::wstring& name)
{
	AnimationLayer layer;
	layer.Name = name.empty() ? BuildDefaultLayerName(mLayers.size()) : name;
	mLayers.push_back(std::move(layer));
	return mLayers.size() - 1;
}

bool AnimatorComponent::RemoveLayer(std::size_t layerIndex)
{
	if (layerIndex >= mLayers.size())
		return false;

	mLayers.erase(mLayers.begin() + static_cast<std::ptrdiff_t>(layerIndex));
	return true;
}

bool AnimatorComponent::MoveLayer(std::size_t fromLayerIndex, std::size_t toLayerIndex)
{
	if (fromLayerIndex >= mLayers.size() || toLayerIndex >= mLayers.size())
		return false;
	if (fromLayerIndex == toLayerIndex)
		return true;

	AnimationLayer layer = std::move(mLayers[fromLayerIndex]);
	mLayers.erase(mLayers.begin() + static_cast<std::ptrdiff_t>(fromLayerIndex));
	mLayers.insert(mLayers.begin() + static_cast<std::ptrdiff_t>(toLayerIndex), std::move(layer));
	return true;
}

void AnimatorComponent::ClearLayers()
{
	mLayers.clear();
}

AnimatorComponent::AnimationLayer* AnimatorComponent::GetLayer(std::size_t layerIndex)
{
	return layerIndex < mLayers.size() ? &mLayers[layerIndex] : nullptr;
}

const AnimatorComponent::AnimationLayer* AnimatorComponent::GetLayer(std::size_t layerIndex) const
{
	return layerIndex < mLayers.size() ? &mLayers[layerIndex] : nullptr;
}

AnimatorComponent::AnimationLayer& AnimatorComponent::EnsureBaseLayer()
{
	if (mLayers.empty())
		(void)AddLayer(L"Base");
	return mLayers.front();
}

const AnimatorComponent::AnimationLayer* AnimatorComponent::GetBaseLayer() const
{
	return mLayers.empty() ? nullptr : &mLayers.front();
}

bool AnimatorComponent::SetLayerName(std::size_t layerIndex, const std::wstring& name)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->Name = name.empty() ? BuildDefaultLayerName(layerIndex) : name;
	return true;
}

bool AnimatorComponent::SetLayerClipAssetPath(std::size_t layerIndex, const std::wstring& assetPath)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->ClipAssetPath = assetPath;
	return true;
}

bool AnimatorComponent::SetLayerMaskRootBoneName(std::size_t layerIndex, const std::wstring& boneName)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->MaskRootBoneName = boneName;
	return true;
}

bool AnimatorComponent::SetLayerTime(std::size_t layerIndex, float time)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->Time = SanitizeNonNegative(time);
	return true;
}

bool AnimatorComponent::SetLayerSpeed(std::size_t layerIndex, float speed)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->Speed = std::isfinite(speed) ? speed : 1.0f;
	return true;
}

bool AnimatorComponent::SetLayerWeight(std::size_t layerIndex, float weight)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->Weight = SanitizeWeight(weight);
	return true;
}

bool AnimatorComponent::SetLayerLoop(std::size_t layerIndex, bool loop)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->Loop = loop;
	return true;
}

bool AnimatorComponent::SetLayerPlaying(std::size_t layerIndex, bool playing)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->Playing = playing;
	return true;
}

bool AnimatorComponent::SetLayerEnabled(std::size_t layerIndex, bool enabled)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->Enabled = enabled;
	return true;
}

bool AnimatorComponent::SetLayerBlendMode(std::size_t layerIndex, AnimationLayerBlendMode blendMode)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->BlendMode = blendMode;
	return true;
}

bool AnimatorComponent::StopLayer(std::size_t layerIndex)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->Playing = false;
	layer->Time = 0.0f;
	return true;
}

bool AnimatorComponent::PlayLayer(std::size_t layerIndex)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr || layer->ClipAssetPath.empty())
		return false;

	layer->Playing = true;
	layer->Enabled = true;
	return true;
}

bool AnimatorComponent::CrossFadeLayerTo(std::size_t layerIndex, const std::wstring& assetPath, float duration, float startTime)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	if (assetPath.empty() || duration <= 0.0f)
	{
		layer->ClipAssetPath = assetPath;
		layer->Time = SanitizeNonNegative(startTime);
		layer->Playing = !assetPath.empty();
		(void)ClearLayerTransition(layerIndex);
		return true;
	}

	layer->TransitionClipAssetPath = assetPath;
	layer->TransitionTime = SanitizeNonNegative(startTime);
	layer->TransitionSpeed = layer->Speed;
	layer->TransitionLoop = layer->Loop;
	layer->TransitionDuration = SanitizeNonNegative(duration);
	layer->TransitionElapsed = 0.0f;
	layer->Playing = true;
	layer->Enabled = true;
	return true;
}

bool AnimatorComponent::HasLayerTransition(std::size_t layerIndex) const
{
	const AnimationLayer* layer = GetLayer(layerIndex);
	return layer != nullptr && !layer->TransitionClipAssetPath.empty();
}

bool AnimatorComponent::ClearLayerTransition(std::size_t layerIndex)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;

	layer->TransitionClipAssetPath.clear();
	layer->TransitionTime = 0.0f;
	layer->TransitionSpeed = 1.0f;
	layer->TransitionDuration = 0.0f;
	layer->TransitionElapsed = 0.0f;
	layer->TransitionLoop = true;
	return true;
}

bool AnimatorComponent::FinishLayerTransition(std::size_t layerIndex)
{
	AnimationLayer* layer = GetLayer(layerIndex);
	if (layer == nullptr)
		return false;
	if (layer->TransitionClipAssetPath.empty())
	{
		(void)ClearLayerTransition(layerIndex);
		return false;
	}

	layer->ClipAssetPath = layer->TransitionClipAssetPath;
	layer->Time = layer->TransitionTime;
	layer->Speed = layer->TransitionSpeed;
	layer->Loop = layer->TransitionLoop;
	(void)ClearLayerTransition(layerIndex);
	return true;
}
