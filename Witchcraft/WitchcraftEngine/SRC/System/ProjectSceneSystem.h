#pragma once

#include "Common/SceneEntityType.h"
#include "Engine/EngineUtils.h"
#include "../D3DWindow/D3DWindow.h"
#include "System/WitchcraftFile/WSceneFile.h"
#include "System/WitchcraftFile/WProjectFile.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <vector>

class SceneEntityBase;
class WitchcraECS;
class Engine;

class ProjectSceneSystem
{
public:
	void Init(D3DWindow* dx, WitchcraECS* ecs, Engine* engine = nullptr);
	void SetSceneLoadedCallback(std::function<void()> callback);
	void SetSceneDirtyCallback(std::function<void()> callback);
	void SetProjectFileChangedCallback(std::function<void(const std::filesystem::path&)> callback);
	bool NewProject();
	bool NewScene(
		std::wstring _name,
		const std::array<DirectX::XMFLOAT4, static_cast<std::size_t>(SceneEntityType::Count)>* sceneTypeColors = nullptr);
	bool OpenScene();
	bool ReloadCurrentScene();
	bool SaveScene();
	bool SaveSkeletonToModel(SceneEntityBase* entity);
	bool ConfirmLeaveCurrentSceneIfNeeded();
	bool OpenProject();
	bool OpenProjectFromPath(const std::filesystem::path& projectFilePath);
	bool SaveProject();
	bool RenameProject(const std::wstring& newName);
	bool AddProjectSceneFromFile();
	bool RenameProjectSceneByIndex(std::size_t sceneIndex, const std::wstring& newName);
	bool DeleteProjectSceneByIndex(std::size_t sceneIndex);
	void ClearScene(std::wstring _name);
	bool CaptureSceneSnapshot(WSceneFileData* outData) const;
	bool RestoreSceneSnapshot(const WSceneFileData& sceneFileData);
	bool RestoreSceneSnapshotDiff(const WSceneFileData& sceneFileData);

	std::wstring GetSceneNmae();
	std::wstring GetProjectName() const;
	std::filesystem::path GetProjectFilePath() const;
	std::filesystem::path GetSceneFilePath() const;
	std::wstring GetCurrentProjectSceneId() const;
	const std::vector<WProjectSceneData>& GetProjectScenes() const;
	bool IsProjectOpen() const;
	bool IsProjectDirty() const;
	bool IsCurrentSceneDirty() const;
	void MarkCurrentSceneDirty();
	void ClearCurrentSceneDirty();
	void BeginSceneDirtySuppression();
	void EndSceneDirtySuppression();
	bool OpenProjectSceneByIndex(std::size_t sceneIndex);
	bool SetProjectEntrySceneByIndex(std::size_t sceneIndex);
	bool RemoveProjectSceneByIndex(std::size_t sceneIndex);

private:
	std::wstring sceneName;
	std::wstring m_sceneCreatedAt;
	std::filesystem::path m_sceneFilePath;
	std::filesystem::path m_projectFilePath;
	WProjectFileData m_projectFileData;
	bool m_projectDirty = false;
	bool m_currentSceneDirty = false;
	std::size_t m_sceneDirtySuspensionDepth = 0;
	bool m_hasCommittedSceneState = false;
	std::wstring m_committedSceneStateToken;

private:
	D3DWindow* m_dx = nullptr;
	WitchcraECS* m_ecs = nullptr;
	Engine* m_engine = nullptr;
	std::function<void()> m_sceneLoadedCallback;
	std::function<void(const std::filesystem::path&)> m_projectFileChangedCallback;

private:
	// 场景 dirty / committed state 管理：
	// 当前方案不是简单 bool，而是基于“规范化场景快照 token”比较。
	bool SaveSceneInternal();
	bool ConfirmSceneSwitchIfNeeded();
	bool BuildCurrentSceneStateToken(std::wstring* outToken) const;
	void CommitSceneStateFromData(const WSceneFileData& sceneFileData);
	void CommitCurrentSceneState();
	bool BuildSceneFileData(WSceneFileData* outData) const;
	bool LoadSceneFileData(const std::filesystem::path& path);
	bool BuildProjectFileData(WProjectFileData* outData) const;
	bool LoadProjectFileData(const std::filesystem::path& path);
	bool ApplyProjectFileData(const WProjectFileData& projectFileData);
	void MarkProjectDirty();
	void ClearProjectDirty();
	void CommitCurrentProjectState();
	void NotifySceneLoaded();
	std::filesystem::path ResolveProjectRelativePath(const std::wstring& pathText, const std::filesystem::path& baseDirectory) const;
	std::wstring BuildProjectRelativePathText(const std::filesystem::path& path, const std::filesystem::path& baseDirectory) const;
	void AppendSceneEntityData(
		SceneEntityBase* entity,
		SceneEntityBase* parent,
		WSceneFileData* outData,
		std::unordered_map<std::wstring, std::wstring>* modelIdByPath,
		std::uint32_t* nextModelIndex) const;
	bool ApplySceneFileData(const WSceneFileData& sceneFileData);
	std::filesystem::path BuildDefaultSceneFilePath() const;
	std::filesystem::path BuildUniqueProjectSceneFilePath(const std::wstring& requestedSceneName) const;
};
