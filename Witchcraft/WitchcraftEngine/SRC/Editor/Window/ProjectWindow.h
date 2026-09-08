#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include <set>

#include <DirectXMath.h>
#include <imgui.h>

#include "Common/SceneEntityType.h"

class Engine;
class ProjectSceneSystem;
class WitchcraECS;

struct ProjectWindowActiveResourceEntry
{
	std::wstring DisplayPath;
	std::wstring FullPath;
};

struct ProjectWindowActiveResourceBuckets
{
	std::vector<ProjectWindowActiveResourceEntry> Images;
	std::vector<ProjectWindowActiveResourceEntry> Audio;
	std::vector<ProjectWindowActiveResourceEntry> Models;
	std::vector<ProjectWindowActiveResourceEntry> Materials;
	std::vector<ProjectWindowActiveResourceEntry> SkeletalAnimation;
	std::set<std::wstring> SeenKeys;
};

class ProjectWindow
{
public:
	void Init(Engine* engine, ProjectSceneSystem* projectSceneSystem);
	void Render();
	void RenderDialogs();
	void NeedRender(bool render);
	bool IsRendering() const;
	void MarkActiveResourcesDirty();

	void OpenNewSceneDialog();
	void OpenProjectRenameDialog();
	void OpenSceneRenameDialog(std::size_t sceneIndex);
	void OpenSceneDeleteDialog(std::size_t sceneIndex, const std::wstring& sceneName);

	bool ConsumePendingNewScene(
		std::wstring* sceneName,
		std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)>* typeColorDraft,
		bool* hasTypeColorOverride);

private:
	bool IsPlayModeActive() const;
	bool IsProjectOpen() const;
	void RenderNewSceneDialog();
	void RenderProjectRenameDialog();
	void RenderSceneRenameDialog();
	void RenderSceneDeleteDialog();
	void RenderSceneTypeColorDraftControls(
		std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)>& colorDraft,
		bool* dirty,
		const char* resetButtonLabel);
	void RenderActiveResources(const std::filesystem::path& projectRootPath);
	void RefreshActiveResourcesCache(const std::filesystem::path& projectRootPath);
	void ResetSceneRenameDialog();
	void ResetSceneDeleteDialog();

private:
	Engine* m_engine = nullptr;
	ProjectSceneSystem* m_projectSceneSystem = nullptr;
	bool m_renderProject = true;

	bool m_openNewSceneDialog = false;
	std::array<char, 256> m_newSceneNameBuffer = {};
	std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)> m_newSceneTypeColorDraft = {};
	bool m_newSceneTypeColorDraftDirty = false;
	std::wstring m_newSceneErrorMessage;

	bool m_openProjectRenameDialog = false;
	std::array<char, 256> m_projectRenameNameBuffer = {};
	std::wstring m_projectRenameErrorMessage;

	bool m_openSceneRenameDialog = false;
	std::size_t m_sceneRenameTargetIndex = static_cast<std::size_t>(-1);
	std::array<char, 256> m_sceneRenameNameBuffer = {};
	std::wstring m_sceneRenameErrorMessage;

	bool m_openSceneDeleteDialog = false;
	std::size_t m_sceneDeleteTargetIndex = static_cast<std::size_t>(-1);
	std::wstring m_sceneDeleteTargetName;

	bool m_pendingNewSceneRequested = false;
	std::wstring m_pendingNewSceneName;
	std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)> m_pendingNewSceneTypeColorDraft = {};
	bool m_pendingNewSceneTypeColorOverride = false;

	bool m_activeResourcesCacheDirty = true;
	std::filesystem::path m_activeResourcesCacheProjectRootPath;
	ProjectWindowActiveResourceBuckets m_activeResourcesCache;
};
