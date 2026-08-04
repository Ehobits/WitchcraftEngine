#pragma once

#include <filesystem>
#include <unordered_map>

#include "Common/AnimationSharedTypes.h"
#include "Common/SkeletonSharedTypes.h"
#include "System/Animation/SkeletonPoseSystem.h"
#include "System/Animation/SkinningPaletteSystem.h"

class WitchcraECS;
class SceneEntityBase;
class AnimatorComponent;
class SkinningRuntimeComponent;

class AnimationSystem
{
public:
	void Update(WitchcraECS* ecs, float deltaTime);

private:
	struct SkeletonCacheEntry
	{
		Witchcraft::Animation::SkeletonTopology Topology;
		std::filesystem::file_time_type LastWriteTime = {};
	};

	struct AnimationCacheEntry
	{
		Witchcraft::Animation::AnimationClipDesc Clip;
		std::filesystem::file_time_type LastWriteTime = {};
	};

	bool TryLoadSkeleton(
		const std::wstring& assetPath,
		SkeletonCacheEntry* outSkeleton);
	bool TryLoadAnimation(
		const std::wstring& assetPath,
		AnimationCacheEntry* outAnimation);
	void UpdateEntityRecursive(WitchcraECS* ecs, SceneEntityBase* entity, float deltaTime);
	void EvaluateAnimatedEntity(
		WitchcraECS* ecs,
		SceneEntityBase* entity,
		Witchcraft::Animation::SkeletonData* skeletonData,
		AnimatorComponent* animatorComponent,
		SkinningRuntimeComponent* runtimeComponent,
		float deltaTime);

	static std::filesystem::path ResolveAssetPath(const std::wstring& assetPath);
	static Witchcraft::Animation::BoneLocalPose SampleTrack(
		const Witchcraft::Animation::BoneAnimationTrack& track,
		float time,
		const Witchcraft::Animation::BoneLocalPose& fallbackPose);

	std::unordered_map<std::wstring, SkeletonCacheEntry> mSkeletonCache;
	std::unordered_map<std::wstring, AnimationCacheEntry> mAnimationCache;
	SkeletonPoseSystem mPoseSystem;
	SkinningPaletteSystem mSkinningPaletteSystem;
};
