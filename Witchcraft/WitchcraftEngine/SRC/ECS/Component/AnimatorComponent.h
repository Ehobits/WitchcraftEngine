#pragma once

#include <xstring>

#include "BaseComponent.h"

class AnimatorComponent : public BaseComponent
{
public:
	void SetClipAssetPath(const std::wstring& assetPath);
	const std::wstring& GetClipAssetPath() const;

	void SetTime(float time);
	float GetTime() const;

	void SetSpeed(float speed);
	float GetSpeed() const;

	void SetLoop(bool loop);
	bool IsLoop() const;

	void SetPlaying(bool playing);
	bool IsPlaying() const;

	void SetEvaluateWhenPaused(bool evaluateWhenPaused);
	bool ShouldEvaluateWhenPaused() const;

	virtual ComponentType GetComponentType() override { return mComponentType; }

private:
	std::wstring mClipAssetPath;
	float mTime = 0.0f;
	float mSpeed = 1.0f;
	bool mLoop = true;
	bool mPlaying = false;
	bool mEvaluateWhenPaused = false;
	ComponentType mComponentType = ComponentType::Co_Animator;
};
