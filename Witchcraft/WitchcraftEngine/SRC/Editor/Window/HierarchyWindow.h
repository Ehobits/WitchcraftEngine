#pragma once

#include <array>
#include <cstdint>
#include <xstring>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "Common/SceneEntityType.h"
#include "Common/TransformSharedTypes.h"

enum CreateLightType;

class ConsoleWindow;
class AssimpLoader;
class WitchcraECS;
class SceneEntityBase;
class D3DWindow;
class Engine;

// 层级窗口
class HierarchyWindow
{
public:
	void Init(ConsoleWindow* consoleWindow, AssimpLoader* assimpLoader, WitchcraECS* ecs, D3DWindow* dx, Engine* engine = nullptr);
	void Render();
	// 处理延期行动
	void ProcessDeferredActions();
	bool ConsumePendingCreateRequest(std::uint32_t* createKind, std::wstring* defaultName, bool* refreshSkyTextures, bool* clearSelectionFirst);
	bool TryGetSelectedSkeletonBone(SceneEntityBase** outOwnerEntity, std::int32_t* outBoneIndex) const;
	void EnsureSkeletonHierarchyForEntity(SceneEntityBase* ownerEntity, std::int32_t preferredBoneIndex = -1, bool createIfMissing = true);

	bool CreateComponentWindow(
		bool* pOpen,
		std::wstring* name,
		Transform* transform = nullptr,
		bool showRotation = true,
		const char* scaleLabel = "缩放",
		std::wstring* materialFilePath = nullptr,
		CreateLightType* lightType = nullptr,
		SceneEntityType* sceneType = nullptr,
		SceneEntityBase* siblingScopeParent = nullptr);

	SceneEntityBase* GetCurrentEntity();
	void RequestFocusSelectedEntity();
	void RequestDeleteSelectedEntity();
	void RequestRenameSelectedEntity();
	void RequestDuplicateSelectedEntities();

	void NeedRender(bool render);
private:
	// 渲染实体树和递归子节点。
	void RenderTree();
	void RenderNode(SceneEntityBase* ent);
	void RenderDeleteImpactTree(SceneEntityBase* ent);
	void DrawHierarchyDropTargetHighlight(bool valid);
	void ClearSelectedSkeletonBone();
	void QueueFocusEntity(SceneEntityBase* entity);
	SceneEntityBase* FindSkeletonHierarchyNode(SceneEntityBase* ownerEntity, std::int32_t boneIndex) const;
	void ApplyPendingFocusForNode(SceneEntityBase* entity);
	bool IsHiddenSkeletonHierarchyNode(SceneEntityBase* entity) const;
	void BeginInlineRename(SceneEntityBase* entity);
	void CancelInlineRename();
	bool CommitInlineRename(SceneEntityBase* entity);
	void QueueCreateRequest(std::uint32_t createKind, const std::wstring& defaultName, bool refreshSkyTextures = false, bool clearSelectionFirst = false);
	void OpenImportModelBrowser(SceneEntityBase* entityToSelect = nullptr);
	void RenderImportModelBrowser();
	void InitializeTransformInputBuffer(const Transform& transform);
	void RenderCreateComponentNameField(std::wstring* name);
	void RenderCreateComponentErrorMessage() const;
	void RenderCreateComponentTransformFields(Transform* transform, bool showRotation, const char* scaleLabel);
	void RenderCreateComponentMaterialField(std::wstring* materialFilePath);
	void RenderCreateComponentSceneTypeField(SceneEntityType* sceneType);
	void RenderCreateComponentLightTypeField(CreateLightType* lightType);
	bool ValidateCreateComponentRequest(const std::wstring& requestedName, SceneEntityBase* siblingScopeParent);
	void ResetCreateComponentWindowState();
	void QueueImportRequest(const std::wstring& filePath, const std::wstring& fileName, SceneEntityBase* entityToSelect = nullptr);
	void QueueDeleteRequest(SceneEntityBase* entity);
	void QueueDeleteSelectedEntitiesRequest();
	void DuplicateSelectedEntities();
	void QueueReparentRequest(SceneEntityBase* entity, SceneEntityBase* newParent);
	void QueueReparentSelectedEntitiesRequest(SceneEntityBase* dropTargetEntity, bool dropToRoot);
	// 处理一些未处理的请求
	void ProcessPendingDeleteRequest();
	void ProcessPendingImportRequest();
	void ProcessPendingReparentRequest();
	const wchar_t* GetCreateLightTypeLabel(UINT lightType);
	std::string FormatTransformValue(float value);
	bool TryParseFloatInput(const std::string& text, float* outValue);
	bool IsImportableModelFileType(UINT fileType);

private:
	const char* HIERARCHY_ENTITY_PAYLOAD = "DND_HIERARCHY_ENTITY";
	// 与 Editor.h 中的 CreateItem 枚举值保持一致，Hierarchy 只负责发请求，不直接依赖 Editor 类型。
	std::uint32_t kCreateItemEmpty = 1;
	std::uint32_t kCreateItemSky = 2;
	std::uint32_t kCreateItemBox = 3;
	std::uint32_t kCreateItemSphere = 4;
	std::uint32_t kCreateItemCapsule = 5;
	std::uint32_t kCreateItemPlane = 6;
	std::uint32_t kCreateItemBillboard = 7;
	std::uint32_t kCreateItemCamera = 8;
	std::uint32_t kCreateItemLight = 9;
	std::uint32_t kCreateItemSkeleton = 10;
	bool renderHierarchy = true;
	// 拖放导入模型时缓存的临时参数。
	bool m_openImportWindow = false;
	std::wstring m_importFilePath;
	std::wstring m_importEntityName;
	Transform m_importTransform;
	SceneEntityType m_importSceneType = SceneEntityType::StaticScenery;
	bool m_openImportBrowserPopup = false;
	std::wstring m_importBrowserCurrentDir;
	std::wstring m_importBrowserSelectedPath;
	SceneEntityBase* m_importBrowserTargetEntity = nullptr;
	bool m_hasPendingCreateRequest = false;
	std::uint32_t m_pendingCreateKind = 0;
	std::wstring m_pendingCreateDefaultName;
	bool m_pendingCreateRefreshSkyTextures = false;
	bool m_pendingCreateClearSelectionFirst = false;
	bool m_hasPendingImportRequest = false;
	std::wstring m_pendingImportFilePath;
	std::wstring m_pendingImportEntityName;
	Transform m_pendingImportTransform;
	SceneEntityType m_pendingImportSceneType = SceneEntityType::StaticScenery;
	bool m_openDeleteConfirmPopup = false;
	SceneEntityBase* m_deleteCandidateEntity = nullptr;
	std::vector<SceneEntityBase*> m_deleteCandidateEntities;
	std::wstring m_deleteCandidateEntityName;
	bool m_deleteCandidateHasChildren = false;
	bool m_hasPendingDeleteRequest = false;
	SceneEntityBase* m_pendingDeleteEntity = nullptr;
	std::vector<SceneEntityBase*> m_pendingDeleteEntities;
	bool m_pendingDeleteChildren = true;
	bool m_hasPendingReparentRequest = false;
	SceneEntityBase* m_pendingReparentEntity = nullptr;
	std::vector<SceneEntityBase*> m_pendingReparentEntities;
	SceneEntityBase* m_pendingReparentNewParent = nullptr;
	std::wstring m_createComponentErrorMessage;
	std::array<std::string, 9> m_transformInputBuffer = {};
	bool m_transformInputBufferInitialized = false;
	bool m_openOperationErrorPopup = false;
	std::wstring m_operationErrorMessage;
	SceneEntityBase* m_pendingFocusEntity = nullptr;
	bool m_pendingFocusScroll = false;
	SceneEntityBase* m_inlineRenameEntity = nullptr;
	std::string m_inlineRenameBuffer;
	std::wstring m_inlineRenameErrorMessage;
	bool m_focusInlineRenameInput = false;
	std::vector<SceneEntityBase*> m_dragSelectionEntities;
	SceneEntityBase* m_selectedSkeletonOwnerEntity = nullptr;
	std::int32_t m_selectedSkeletonBoneIndex = -1;

	ConsoleWindow* m_consoleWindow = nullptr;
	AssimpLoader* m_assimpLoader = nullptr;
	WitchcraECS* m_ecs = nullptr;
	D3DWindow* m_dx = nullptr;
	Engine* m_engine = nullptr;
};
