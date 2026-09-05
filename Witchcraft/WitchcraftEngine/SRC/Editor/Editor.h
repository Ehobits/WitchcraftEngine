#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

#include "Common/MeshSharedTypes.h"
#include "Common/SceneEntityType.h"
#include "Common/TransformSharedTypes.h"
#include "SkeletonEditorTool.h"
#include "EditorTransformGizmo.h"
#include "D3DWindow/D3DWindow.h"
#include "Window/ScreenSettingsWindow.h"
#include "Window/FileWindow.h"
#include "Window/AssetsWindow.h"
#include "Window/MaterialEditorWindow.h"
#include "Window/AnimationEditorWindow.h"
#include "Window/ScriptEditorWindow.h"
#include "Window/InspectorWindow.h"
#include "Window/HierarchyWindow.h"
#include "Window/ConsoleWindow.h"
#include "Window/AboutWindow.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "System/WitchcraftFile/WModelFile.h"
#include "SYSTEM/ProjectSceneSystem.h"
#include "SYSTEM/ScriptingSystem.h"
#include "HELPERS/Helpers.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <IconsFontAwesome5.h>

#define WINDOW_DOWN 32

enum PrimitiveState : BYTE
{
	PrimitiveTriangle = 0x01,
	PrimitiveLine = 0x02,
	PrimitivePoint = 0x03,
};

enum CreateItem : UINT
{
	UnknownItem = 0,
	EmptyItem,
	SkyItem,
	BoxItem,
	SphereItem,
	CapsuleItem,
	PlaneItem,
	BillboardItem,
	CameraItem,
	LightItem,
	SkeletonItem
};

enum CreateLightType : UINT
{
	CreateAmbientLight = 0,
	CreateDirectionalLight,
	CreateSpotLight,
	CreatePointLight
};

enum PendingSceneAction : UINT
{
	PendingSceneAction_None = 0,
	PendingSceneAction_New,
	PendingSceneAction_Open,
	PendingSceneAction_Save,
	PendingSceneAction_Reload
};

struct ImGuiWindow;

class Engine;
class WitchcraECS;
class KeyboardClass;
class MouseClass;
class MouseEvent;
class SceneEntityBase;

class DescriptorPool {
public:
	void Init(UINT total) {
		m_freeList.clear();
		m_freeList.reserve(total);
		for (UINT i = 0; i < total; ++i)
			m_freeList.push_back(i);
	}
	bool Allocate(UINT& outIndex) {
		if (m_freeList.empty()) return false;
		outIndex = m_freeList.back();
		m_freeList.pop_back();
		return true;
	}
	void Free(UINT index) {
		m_freeList.push_back(index);
	}
private:
	std::vector<UINT> m_freeList;
};

class Editor
{
public:
	bool Init(HWND hWnd, Engine* engine, std::wstring path);
	void Update();
	void Render();
	void NotifyDisplayResize(float width, float height);
	void Shutdown();

public:
	void SetProcHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	bool ApplyImGuiCursorForClientArea() const;
	bool OpenMaterialEditor(const std::wstring& path);
	bool OpenAnimationEditor(const std::wstring& path);
	bool OpenScriptEditor(const std::wstring& path);
	ConsoleWindow* GetConsoleWindow();
	void CreateLuaScriptAsset(const std::wstring& filePath, const std::wstring& tableName);

	//// - Ray System //////////////////////////////////////
	void RayVector(float mouseX, float mouseY, DirectX::XMVECTOR& pickRayInWorldSpacePos, DirectX::XMVECTOR& pickRayInWorldSpaceDir);
	bool PointInTriangle(DirectX::XMVECTOR& triV1, DirectX::XMVECTOR& triV2, DirectX::XMVECTOR& triV3, DirectX::XMVECTOR& point);
	float PickMesh(DirectX::XMVECTOR pickRayInWorldSpacePos, DirectX::XMVECTOR pickRayInWorldSpaceDir, const std::vector<Vertex>& vertPosArray, const std::vector<std::uint32_t>& indexPosArray, DirectX::XMMATRIX worldSpace);
	void RunRay(POINT mousePoint, bool additiveSelection = false);

	WModelFileData* GetBrushWeightModelDataMutable();
	const WModelFileData* GetBrushWeightModelData() const;
	void MarkBrushWeightModelDirty();
	std::uint64_t GetBrushWeightVisualRevision() const;
	SceneEntityBase* GetBrushWeightVisualizationTargetEntity() const;

	D3DWindow* GetD3DWindow() const;
	Engine* GetEngine() const;

public:
	std::map<std::wstring, ImFont*> fonts;
	ImFont* icons = nullptr;
	ImVec4 myColor = ImVec4(ImGui::ColorConvertU32ToFloat4(IM_COL32(0xE2, 0x52, 0x52, 0xFF)));

private:
	bool IsDockingBackgroundWindow(const ImGuiWindow* window) const;
	bool IsImGuiWindowFocusedByName(const char* windowName) const;
	bool IsSceneMouseBlockedByImGui() const;
	bool IsMousePointBlockedByImGui(const POINT& mousePoint) const;
	void EnqueueImGuiWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	void FlushImGuiWindowMessages();
	void UpdateKeyboard(const ImGuiIO& io, bool captureKeyboard);
	void HandleHotkeys(KeyboardClass* keyboard, bool captureKeyboard);
	void HandleKeyboardMove(KeyboardClass* keyboard, const ImGuiIO& io, bool captureKeyboard);
	void UpdateMouse(const ImGuiIO& io, bool captureSceneMouse);
	void QueuePickRequest(const MouseEvent& me);
	void HandleBlockedMouseEvent(const MouseEvent& me);
	bool HandleGizmoMouse(const MouseEvent& me, MouseClass* mouse);
	void HandleHoverMouse(const MouseEvent& me, MouseClass* mouse);
	void HandlePanMouse(const MouseEvent& me, MouseClass* mouse, const ImGuiIO& io);
	void HandleRotateMouse(const MouseEvent& me, MouseClass* mouse, const ImGuiIO& io);
	void HandleWheelMouse(const MouseEvent& me, const ImGuiIO& io);
	void UpdateGizmoData();
	void UpdateEditUI();
	void ProcessPendingPick(const ImGuiIO& io);

	void SetStyle();
	void SetFont();
	void UpdateImGuiDPIScale();
	float GetScaledWindowDown() const;
	void RefreshSkyTextureFiles();
	void RegisterWindowVisibilitySettingsHandler();
	void ApplyWindowVisibilityState();
	void MarkWindowVisibilitySettingsDirty();
	void ReportPendingScriptRuntimeErrors();
	static void* WindowVisibilitySettingsReadOpen(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name);
	static void WindowVisibilitySettingsReadLine(ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line);
	static void WindowVisibilitySettingsWriteAll(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* outBuf);
	void OpenCreateEntityWindow(CreateItem item, const std::wstring& defaultName, bool refreshSkyTextures = false);
	void OpenQueuedCreateEntityWindow(std::uint32_t createKind, const std::wstring& defaultName, bool refreshSkyTextures, bool clearSelectionFirst);
	bool RenderCreateObjectWindow();
	bool RenderCreateSkyWindow();
	void RenderProjectSettingsWindow();
	void RenderBar();
	void RenderDownBar();
	void RenderUpBar();
	void RenderToolBar();
	SceneEntityBase* ResolveSkeletonOwnerEntity(SceneEntityBase* entity) const;
	SceneEntityBase* ResolveBrushWeightTargetEntity(SceneEntityBase* entity) const;
	bool LoadBrushWeightModelForEntity(SceneEntityBase* entity);
	void ClearBrushWeightModel();
	const std::filesystem::path& GetBrushWeightModelPath() const;
	bool IsBrushWeightModelLoaded() const;
	bool IsBrushWeightModelDirty() const;

private:
	void SetDocking();
	ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
	bool opt_fullscreen = true;
	bool opt_padding = false;
	BYTE renderState = PrimitiveState::PrimitiveTriangle;

	UINT m_DpiScale = 1u;
	ImGuiStyle m_imguiBaseStyle;

private:
	void RenderFileMenuBar();
	void RenderProjectMenuBar();
	void RenderEditMenuBar();
	void RenderAssetsMenuBar();
	void RenderEntityMenuBar();
	void RenderWindowMenuBar();
	void RenderHelpMenuBar();
	void RenderScriptMenuBar();
	bool ApplySkeletonJointsToData(
		const std::vector<SkeletonJoint>& joints,
		Witchcraft::Animation::SkeletonData* skeletonData);

	// 分配一个描述符的回调函数
	static void ImgSrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle);
	// 释放一个描述符的回调函数
	static void ImgSrvDescriptorFree(ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle);

	static DescriptorPool m_imguiDescPool;

private:
	static Editor* this_Editor;

	HWND m_hWnd = nullptr;
	std::wstring m_imguiAssetPath;

	struct ImGuiWindowMessage
	{
		HWND hwnd = nullptr;
		UINT uMsg = 0;
		WPARAM wParam = 0;
		LPARAM lParam = 0;
	};

	std::mutex m_imguiMessageMutex;
	std::recursive_mutex m_imguiContextMutex;  // 改为递归锁,支持 IME 消息处理时的递归调用
	std::queue<ImGuiWindowMessage> m_imguiMessageQueue;

	// ImGui 光标状态（跨线程共享）
	std::atomic<int> m_lastImGuiMouseCursor{0};
	std::atomic<bool> m_hasImGuiCursorSnapshot{false};

	POINT point = { 0, 0 };
	DirectX::XMFLOAT2 mSmoothedCameraRotateDelta = { 0.0f, 0.0f };
	bool mEnableCameraRotateSmoothing = true;
	float mCameraRotateSmoothFactor = 0.10f;
	POINT m_pendingPickPoint = { 0, 0 };
	bool m_hasPendingPickRequest = false;
	bool m_pendingPickAdditiveSelection = false;
	SkeletonEditorTool m_skeletonEditorTool;
	EditorTransformGizmo m_transformGizmo;
	SceneEntityBase* m_lastHierarchySelectedSkeletonOwnerEntity = nullptr;
	std::int32_t m_lastHierarchySelectedSkeletonBoneIndex = -1;
	SceneEntityBase* m_brushWeightTargetEntity = nullptr;
	bool m_brushWeightTargetResolved = false;
	bool m_wasBrushWeightMode = false;
	std::filesystem::path m_brushWeightModelPath;
	WModelFileData m_brushWeightModelData;
	bool m_brushWeightModelLoaded = false;
	bool m_brushWeightModelDirty = false;
	std::uint64_t m_brushWeightVisualRevision = 0;
	std::uint64_t m_lastAppliedBrushWeightVisualRevision = 0;
	ComPtr<ID3D12RootSignature> mGUIRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> RtvHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> DsvHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> mGUISrvDescriptorHeap = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE editerCPUTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE editerGPUTexDescriptor;

	Engine* m_engine = nullptr;
	D3DWindow* m_dx = nullptr;
	ProjectSceneSystem* m_projectSceneSystem = nullptr;
	ScriptingSystem* m_scriptingSystem = nullptr;
	PhysicsSystem* m_physicsSystem = nullptr;
	ScreenSettingsWindow m_screenSettingsWindow;
	AssetsWindow m_assetsWindow;
	MaterialEditorWindow m_materialEditorWindow;
	AnimationEditorWindow m_animationEditorWindow;
	ScriptEditorWindow m_scriptEditorWindow;
	HierarchyWindow m_hierarchyWindow;
	InspectorWindow m_inspectorWindow;
	FileWindow m_fileWindow;
	ConsoleWindow m_consoleWindow;
	AboutWindow m_aboutWindow;
	bool m_showHierarchyWindow = true;
	bool m_showInspectorWindow = true;
	bool m_showAssetsWindow = true;
	bool m_showFileWindow = true;
	bool m_showScriptEditorWindow = false;
	bool m_showConsoleWindow = true;
	bool m_showScreenSettingsWindow = true;
	bool m_showSkeletonToolsWindow = false;
	bool m_showSkinWeightVisualization = false;
	std::uint64_t m_lastReportedScriptErrorRevision = 0;
	AssimpLoader m_assimpLoader;

	bool openCreateWindow = false;
	std::wstring name = L"";
	Transform transform;
	CreateItem CreaItem = CreateItem::UnknownItem;
	bool m_pendingFocusCreatedEntityInHierarchy = false;
	SceneEntityType m_createSceneType = SceneEntityType::StaticScenery;
	CreateLightType m_createLightType = CreateLightType::CreateDirectionalLight;
	std::vector<std::wstring> m_skyTextureFiles;
	int m_selectedSkyTextureIndex = 0;
	std::wstring m_createSkyErrorMessage;
	PendingSceneAction m_pendingSceneAction = PendingSceneAction_None;
	std::wstring m_pendingSceneName;
	bool m_openProjectSettings = false;
	std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)> m_projectSceneTypeColorDraft = {};
	bool m_projectSceneTypeColorDraftInitialized = false;
	bool m_projectSceneTypeColorDraftDirty = false;
};
