#pragma once

#include "Engine/EngineUtils.h"
#include "../D3DWindow/D3DWindow.h"
#include "System/WitchcraftFile/WSceneFile.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>

class SceneEntityBase;
class WitchcraECS;
class Engine;

class ProjectSceneSystem
{
public:
	void Init(D3DWindow* dx, WitchcraECS* ecs, Engine* engine = nullptr);
	bool NewScene(std::wstring _name);
	bool OpenScene();
	bool ReloadCurrentScene();
	bool SaveScene();
	bool SaveSkeletonToModel(SceneEntityBase* entity);
	bool ConfirmLeaveCurrentSceneIfNeeded();
	void OpenProject();
	void SaveProject();
	void ClearScene(std::wstring _name);
	bool CaptureSceneSnapshot(WSceneFileData* outData) const;
	bool RestoreSceneSnapshot(const WSceneFileData& sceneFileData);

	std::wstring GetSceneNmae();

private:
	std::wstring sceneName;
	std::wstring m_sceneCreatedAt;
	std::filesystem::path m_sceneFilePath;
	bool m_hasCommittedSceneState = false;
	std::wstring m_committedSceneStateToken;

private:
	D3DWindow* m_dx = nullptr;
	WitchcraECS* m_ecs = nullptr;
	Engine* m_engine = nullptr;

private:
	// 场景 dirty / committed state 管理：
	// 当前方案不是简单 bool，而是基于“规范化场景快照 token”比较。
	bool SaveSceneInternal();
	bool ConfirmSceneSwitchIfNeeded();
	bool IsCurrentSceneDirty() const;
	bool BuildCurrentSceneStateToken(std::wstring* outToken) const;
	void CommitSceneStateFromData(const WSceneFileData& sceneFileData);
	void CommitCurrentSceneState();
	bool BuildSceneFileData(WSceneFileData* outData) const;
	bool LoadSceneFileData(const std::filesystem::path& path);
	void AppendSceneEntityData(
		SceneEntityBase* entity,
		SceneEntityBase* parent,
		WSceneFileData* outData,
		std::unordered_map<std::wstring, std::wstring>* modelIdByPath,
		std::uint32_t* nextModelIndex) const;
	bool ApplySceneFileData(const WSceneFileData& sceneFileData);
	std::filesystem::path BuildDefaultSceneFilePath() const;
};
