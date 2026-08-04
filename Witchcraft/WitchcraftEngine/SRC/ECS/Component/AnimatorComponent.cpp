#include "AnimatorComponent.h"

void AnimatorComponent::SetClipAssetPath(const std::wstring& assetPath)
{
	mClipAssetPath = assetPath;
}

const std::wstring& AnimatorComponent::GetClipAssetPath() const
{
	return mClipAssetPath;
}

void AnimatorComponent::SetTime(float time)
{
	mTime = time;
}

float AnimatorComponent::GetTime() const
{
	return mTime;
}

void AnimatorComponent::SetSpeed(float speed)
{
	mSpeed = speed;
}

float AnimatorComponent::GetSpeed() const
{
	return mSpeed;
}

void AnimatorComponent::SetLoop(bool loop)
{
	mLoop = loop;
}

bool AnimatorComponent::IsLoop() const
{
	return mLoop;
}

void AnimatorComponent::SetPlaying(bool playing)
{
	mPlaying = playing;
}

bool AnimatorComponent::IsPlaying() const
{
	return mPlaying;
}

void AnimatorComponent::SetEvaluateWhenPaused(bool evaluateWhenPaused)
{
	mEvaluateWhenPaused = evaluateWhenPaused;
}

bool AnimatorComponent::ShouldEvaluateWhenPaused() const
{
	return mEvaluateWhenPaused;
}
