#pragma once

#include <filesystem>
#include <vector>
#include <xstring>

#include <imgui.h>

#include "Common/SkeletonSharedTypes.h"
#include "System/Animation/AnimationClipEditing.h"
#include "System/WitchcraftFile/WAnimationFile.h"

class SceneEntityBase;
class Editor;

class AnimationEditorWindow
{
public:
	void Init(class WitchcraECS* ecs, Editor* editor);
	void Render();

	void NeedRender(bool render);
	bool OpenAnimationFile(const std::wstring& path);

private:
	struct TimelineKeySelection
	{
		bool Valid = false;
		std::size_t TrackIndex = 0;
		Witchcraft::Animation::AnimationKeyChannel Channel = Witchcraft::Animation::AnimationKeyChannel::Translation;
		std::size_t KeyIndex = 0;
	};

	static void WriteUtf8Buffer(const std::wstring& text, char* buffer, size_t bufferSize);
	static std::wstring ReadUtf8Buffer(const char* buffer);
	static std::filesystem::path EnsureAnimationFileExtension(const std::filesystem::path& path);

	bool SaveAnimationFile(const std::filesystem::path& path, bool switchCurrentPath);
	bool SaveAnimationAs();
	void ResetTimelineInteractionState();
	bool InsertKeyOnTrack(
		Witchcraft::Animation::BoneAnimationTrack& track,
		Witchcraft::Animation::AnimationKeyChannel channel,
		float time);
	bool HasAnimationRelatedComponent(WitchcraECS* ecs, SceneEntityBase* entity);
	SceneEntityBase* ResolveSkeletonOwnerEntity(SceneEntityBase* entity) const;
	const Witchcraft::Animation::SkeletonData* ResolveCurrentSkeletonData(SceneEntityBase** outOwnerEntity) const;
	bool TryGetCurrentTrackPose(
		const Witchcraft::Animation::BoneAnimationTrack& track,
		Witchcraft::Animation::BoneLocalPose* outPose) const;
	bool RenderTimelineOverview(float previewDuration);

	void CollectAnimationOwnerEntities(
		WitchcraECS* ecs,
		SceneEntityBase* entity,
		std::vector<SceneEntityBase*>& outEntities);

private:
	bool renderAnimationEditor = false;
	bool m_hasLoadedAnimation = false;
	bool m_isDirty = false;
	bool m_previewPlaying = false;

	std::filesystem::path m_currentPath;
	WAnimationFileData m_data;
	class WitchcraECS* m_ecs = nullptr;
	Editor* m_editor = nullptr;
	// 预览是一次显式的编辑操作，不能因为层级选择变化而转移到另一个实体。
	SceneEntityBase* m_previewTargetEntity = nullptr;

	char m_clipName[256] = {};
	float m_previewTime = 0.0f;
	float m_timelineZoom = 1.0f;
	float m_timelineSnapStep = 1.0f;
	char m_timelineTrackFilter[128] = {};
	bool m_timelineOnlyShowTracksWithKeys = false;
	TimelineKeySelection m_timelineKeySelection;
	bool m_timelineKeyDragging = false;
	float m_timelineContextTime = 0.0f;
	char m_previewTransitionClip[256] = {};
	float m_previewTransitionFadeTime = 0.2f;

	std::wstring m_statusMessage;
	ImVec4 m_statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
};
