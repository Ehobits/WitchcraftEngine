#pragma once

#include <filesystem>
#include <xstring>

#include <imgui.h>

#include "System/WitchcraftFile/WAnimationFile.h"

class SceneEntityBase;

class AnimationEditorWindow
{
public:
	void Init(class WitchcraECS* ecs);
	void Render();

	void NeedRender(bool render);
	bool OpenAnimationFile(const std::wstring& path);

private:
	static void WriteUtf8Buffer(const std::wstring& text, char* buffer, size_t bufferSize);
	static std::wstring ReadUtf8Buffer(const char* buffer);

	bool HasAnimationRelatedComponent(WitchcraECS* ecs, SceneEntityBase* entity);

	template<typename TKey>
	void SortKeysByTime(std::vector<TKey>& keys);

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
	// 预览是一次显式的编辑操作，不能因为层级选择变化而转移到另一个实体。
	SceneEntityBase* m_previewTargetEntity = nullptr;

	char m_clipName[256] = {};
	float m_previewTime = 0.0f;

	std::wstring m_statusMessage;
	ImVec4 m_statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
};
