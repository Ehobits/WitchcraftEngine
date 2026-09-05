#pragma once

#include <cstddef>
#include <vector>
#include <xstring>

#include "BaseComponent.h"

class AnimatorComponent : public BaseComponent
{
public:
	enum class AnimationLayerBlendMode
	{
		Override,
		Additive
	};

	struct AnimationLayer
	{
		std::wstring Name;
		std::wstring ClipAssetPath;
		std::wstring MaskRootBoneName;
		float Time = 0.0f;
		float Speed = 1.0f;
		float Weight = 1.0f;
		bool Loop = true;
		bool Playing = false;
		bool Enabled = true;
		AnimationLayerBlendMode BlendMode = AnimationLayerBlendMode::Override;

		std::wstring TransitionClipAssetPath;
		float TransitionTime = 0.0f;
		float TransitionSpeed = 1.0f;
		float TransitionDuration = 0.0f;
		float TransitionElapsed = 0.0f;
		bool TransitionLoop = true;
	};

	const std::vector<AnimationLayer>& GetLayers() const;
	std::vector<AnimationLayer>& GetMutableLayers();
	bool HasPlayableLayers() const;
	bool ShouldEvaluateWhenPaused() const;
	void SetEvaluateWhenPaused(bool evaluateWhenPaused);

	std::size_t AddLayer(const std::wstring& name = L"");
	bool RemoveLayer(std::size_t layerIndex);
	bool MoveLayer(std::size_t fromLayerIndex, std::size_t toLayerIndex);
	void ClearLayers();
	AnimationLayer* GetLayer(std::size_t layerIndex);
	const AnimationLayer* GetLayer(std::size_t layerIndex) const;
	AnimationLayer& EnsureBaseLayer();
	const AnimationLayer* GetBaseLayer() const;

	bool SetLayerName(std::size_t layerIndex, const std::wstring& name);
	bool SetLayerClipAssetPath(std::size_t layerIndex, const std::wstring& assetPath);
	bool SetLayerMaskRootBoneName(std::size_t layerIndex, const std::wstring& boneName);
	bool SetLayerTime(std::size_t layerIndex, float time);
	bool SetLayerSpeed(std::size_t layerIndex, float speed);
	bool SetLayerWeight(std::size_t layerIndex, float weight);
	bool SetLayerLoop(std::size_t layerIndex, bool loop);
	bool SetLayerPlaying(std::size_t layerIndex, bool playing);
	bool SetLayerEnabled(std::size_t layerIndex, bool enabled);
	bool SetLayerBlendMode(std::size_t layerIndex, AnimationLayerBlendMode blendMode);
	bool StopLayer(std::size_t layerIndex);
	bool PlayLayer(std::size_t layerIndex);
	bool CrossFadeLayerTo(std::size_t layerIndex, const std::wstring& assetPath, float duration, float startTime = 0.0f);
	bool HasLayerTransition(std::size_t layerIndex) const;
	bool ClearLayerTransition(std::size_t layerIndex);
	bool FinishLayerTransition(std::size_t layerIndex);

	virtual ComponentType GetComponentType() override { return mComponentType; }

private:
	static float SanitizeNonNegative(float value);
	static float SanitizeWeight(float value);
	static std::wstring BuildDefaultLayerName(std::size_t layerIndex);

	std::vector<AnimationLayer> mLayers;
	bool mEvaluateWhenPaused = false;
	ComponentType mComponentType = ComponentType::Co_Animator;
};
