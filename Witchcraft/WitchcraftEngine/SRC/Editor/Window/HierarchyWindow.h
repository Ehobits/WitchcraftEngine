#pragma once

#include <xstring>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "Common/TransformSharedTypes.h"

class ConsoleWindow;
class AssimpLoader;
class WitchcraECS;
class SceneEntityBase;
class D3DWindow;

// 层级窗口
class HierarchyWindow
{
public:
	void Init(ConsoleWindow* consoleWindow, AssimpLoader* assimpLoader, WitchcraECS* ecs, D3DWindow* dx);
	void Render();
	void ProcessDeferredActions();

	bool CreateComponentWindow(
		bool* pOpen,
		std::wstring* name,
		Transform* transform = nullptr,
		std::wstring* materialFilePath = nullptr,
		UINT* lightType = nullptr,
		SceneEntityBase* siblingScopeParent = nullptr);

	SceneEntityBase* GetCurrentEntity();

	void NeedRender(bool render);
private:
	// 渲染实体树和递归子节点。
	void RenderTree();
	void RenderNode(SceneEntityBase* ent, SceneEntityBase* selectedEntity);
	void RenderDeleteImpactTree(SceneEntityBase* ent);
	void DrawHierarchyDropTargetHighlight(bool valid);
	void QueueImportRequest(const std::wstring& filePath, const std::wstring& fileName, SceneEntityBase* entityToSelect = nullptr);
	void QueueDeleteRequest(SceneEntityBase* entity);
	void QueueReparentRequest(SceneEntityBase* entity, SceneEntityBase* newParent);
	void ProcessPendingDeleteRequest();
	void ProcessPendingImportRequest();
	void ProcessPendingReparentRequest();

private:
	bool renderHierarchy = true;
	// 拖放导入模型时缓存的临时参数。
	bool m_openImportWindow = false;
	std::wstring m_importFilePath;
	std::wstring m_importEntityName;
	Transform m_importTransform;
	bool m_hasPendingImportRequest = false;
	std::wstring m_pendingImportFilePath;
	std::wstring m_pendingImportEntityName;
	Transform m_pendingImportTransform;
	bool m_openDeleteConfirmPopup = false;
	SceneEntityBase* m_deleteCandidateEntity = nullptr;
	std::wstring m_deleteCandidateEntityName;
	bool m_deleteCandidateHasChildren = false;
	bool m_hasPendingDeleteRequest = false;
	SceneEntityBase* m_pendingDeleteEntity = nullptr;
	bool m_pendingDeleteChildren = true;
	bool m_hasPendingReparentRequest = false;
	SceneEntityBase* m_pendingReparentEntity = nullptr;
	SceneEntityBase* m_pendingReparentNewParent = nullptr;

	ConsoleWindow* m_consoleWindow = nullptr;
	AssimpLoader* m_assimpLoader = nullptr;
	WitchcraECS* m_ecs = nullptr;
	D3DWindow* m_dx = nullptr;
};
