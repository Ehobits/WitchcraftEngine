#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include "ECS/ServicesContainer/ServicesContainer.h"

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

	bool CreateComponentWindow(bool* pOpen, std::wstring* name, Transform* transform = nullptr, std::wstring* materialFilePath = nullptr);

	SceneEntityBase* GetCurrentEntity();

	void NeedRender(bool render);
private:
	// 渲染实体树和递归子节点。
	void RenderTree();
	void RenderNode(SceneEntityBase* ent);
	void RenderDeleteImpactTree(SceneEntityBase* ent);

private:
	bool renderHierarchy = true;
	bool openme = false;
	bool openCreateWindow = false;
	std::wstring name = L"";
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
	std::wstring m_pendingDeleteEntityName;
	bool m_pendingDeleteChildren = true;

	ConsoleWindow* m_consoleWindow = nullptr;
	AssimpLoader* m_assimpLoader = nullptr;
	WitchcraECS* m_ecs = nullptr;
	D3DWindow* m_dx = nullptr;
};
