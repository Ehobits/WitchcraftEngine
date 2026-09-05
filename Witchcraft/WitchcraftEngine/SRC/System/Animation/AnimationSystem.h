#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "Common/AnimationSharedTypes.h"
#include "Common/SkeletonSharedTypes.h"
#include "System/Animation/Assets/AnimationClipAsset.h"
#include "System/Animation/SkeletonPoseSystem.h"
#include "System/Animation/SkinningPaletteSystem.h"

class WitchcraECS;
class SceneEntityBase;
class AnimatorComponent;
class SkinningRuntimeComponent;
class ScriptingSystem;

class AnimationSystem
{
public:
	void Update(WitchcraECS* ecs, float deltaTime, ScriptingSystem* scriptingSystem = nullptr);

private:
	struct SkeletonCacheEntry
	{
		Witchcraft::Animation::SkeletonTopology Topology;
		std::filesystem::file_time_type LastWriteTime = {};
	};

	struct AnimationCacheEntry
	{
		Witchcraft::Animation::AnimationClipAsset Asset;
		std::filesystem::file_time_type LastWriteTime = {};
	};

	bool TryLoadSkeleton(
		const std::wstring& assetPath,
		SkeletonCacheEntry* outSkeleton);
	bool TryLoadAnimation(
		const std::wstring& assetPath,
		const AnimationCacheEntry** outAnimation);
	void UpdateEntityRecursive(WitchcraECS* ecs, SceneEntityBase* entity, float deltaTime, ScriptingSystem* scriptingSystem);
	void EvaluateAnimatedEntity(
		WitchcraECS* ecs,
		SceneEntityBase* entity,
		Witchcraft::Animation::SkeletonData* skeletonData,
		AnimatorComponent* animatorComponent,
		SkinningRuntimeComponent* runtimeComponent,
		float deltaTime,
		ScriptingSystem* scriptingSystem);

	static std::filesystem::path ResolveAssetPath(const std::wstring& assetPath);
	static Witchcraft::Animation::BoneLocalPose SampleTrack(
		const Witchcraft::Animation::BoneAnimationTrack& track,
		const Witchcraft::Animation::AnimationTrackCache* trackCache,
		float time,
		const Witchcraft::Animation::BoneLocalPose& fallbackPose);
	static bool SampleClip(
		const Witchcraft::Animation::AnimationClipDesc& clip,
		const Witchcraft::Animation::AnimationClipCache* clipCache,
		const Witchcraft::Animation::SkeletonTopology& topology,
		float time,
		std::vector<Witchcraft::Animation::BoneLocalPose>* inOutLocalPose,
		std::vector<DirectX::XMFLOAT4X4>* inOutLocalMatrixPose,
		std::uint32_t* outMatchedTrackCount);

	std::unordered_map<std::wstring, SkeletonCacheEntry> mSkeletonCache;
	std::unordered_map<std::wstring, AnimationCacheEntry> mAnimationCache;
	SkeletonPoseSystem mPoseSystem;
	SkinningPaletteSystem mSkinningPaletteSystem;
};
