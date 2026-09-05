#include "Editor.h"

#include "String/SStringUtils.h"
#include "D3DWindow/Texture.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/AnimatorComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineUtils.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "EditorAssetCache.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <xstring>
#include <wincodec.h>
#include <DirectXCollision.h>

#include <imgui_internal.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

///////////////////////////////////////////////////////////////

#define MAX_NUM_IMGUI_IMAGES_PER_FRAME 128

static ImVec2 mainMenuBarSize = ImVec2(NULL, NULL);
static constexpr const char* kEditorWindowVisibilitySettingsTypeName = "WitchcraftEditorWindows";

Editor* Editor::this_Editor = nullptr;
DescriptorPool Editor::m_imguiDescPool;

SceneEntityBase* Editor::ResolveSkeletonOwnerEntity(SceneEntityBase* entity) const
{
	WitchcraECS* ecs = m_engine->GetECS();
	if (!ecs->HasEntity(entity))
		return nullptr;

	// 骨架层级中的骨骼节点与网格子节点自身通常不持有 SkeletonData。
	// 沿父链查找可保证在选择任一子对象时仍持续使用同一份骨架姿势，
	// 而不会把覆盖层留在上一帧的几何数据上。
	for (SceneEntityBase* current = entity; current != nullptr; current = ecs->GetParentEntity(current))
	{
		if (ecs->GetSkeletonData(current) != nullptr)
			return current;
	}

	return nullptr;
}

SceneEntityBase* Editor::ResolveBrushWeightTargetEntity(SceneEntityBase* entity) const
{
	WitchcraECS* ecs = m_engine->GetECS();
	if (ecs == nullptr || !ecs->HasEntity(entity))
		return nullptr;

	auto isMeshOwner = [&](SceneEntityBase* candidate) -> bool
	{
		if (candidate == nullptr || ecs == nullptr || !ecs->HasEntity(candidate))
			return false;
		MeshComponent* meshComponent = ecs->GetComponent<MeshComponent>(candidate);
		return meshComponent != nullptr && !meshComponent->GetFileName().empty();
	};

	if (isMeshOwner(entity))
		return entity;

	for (SceneEntityBase* child : entity->GetChildrenEntity())
	{
		if (SceneEntityBase* resolvedChild = ResolveBrushWeightTargetEntity(child))
			return resolvedChild;
	}

	return nullptr;
}

bool Editor::LoadBrushWeightModelForEntity(SceneEntityBase* entity)
{
	ClearBrushWeightModel();
	if (entity == nullptr || m_engine == nullptr)
	{
		::OutputDebugStringW(L"[BrushLoad] fail: null entity or engine\n");
		return false;
	}

	WitchcraECS* ecs = m_engine->GetECS();
	if (ecs == nullptr || !ecs->HasEntity(entity))
	{
		::OutputDebugStringW(L"[BrushLoad] fail: ecs missing or entity not registered\n");
		return false;
	}

	MeshComponent* meshComponent = ecs->GetComponent<MeshComponent>(entity);
	if (meshComponent == nullptr)
	{
		{
			wchar_t debugText[256] = {};
			swprintf_s(debugText, L"[BrushLoad] fail: no MeshComponent entity=%p\n", entity);
			::OutputDebugStringW(debugText);
		}
		return false;
	}

	const std::wstring modelPathText = meshComponent->GetFileName();
	if (modelPathText.empty())
	{
		{
			wchar_t debugText[256] = {};
			swprintf_s(debugText, L"[BrushLoad] fail: empty model path entity=%p\n", entity);
			::OutputDebugStringW(debugText);
		}
		return false;
	}

	const std::filesystem::path modelPath = std::filesystem::path(modelPathText).lexically_normal();
	WModelFileData modelData;
	if (!WModelFile::LoadFromFile(modelPath, &modelData))
	{
		{
			wchar_t debugText[512] = {};
			swprintf_s(debugText, L"[BrushLoad] fail: load model failed path=%s\n", modelPath.wstring().c_str());
			::OutputDebugStringW(debugText);
		}
		return false;
	}

	m_brushWeightModelPath = modelPath;
	m_brushWeightModelData = std::move(modelData);
	m_brushWeightModelLoaded = true;
	m_brushWeightModelDirty = false;
	m_brushWeightVisualRevision = 0;
	{
		wchar_t debugText[512] = {};
		swprintf_s(debugText, L"[BrushLoad] success path=%s meshes=%zu\n", modelPath.wstring().c_str(), m_brushWeightModelData.Meshes.size());
		::OutputDebugStringW(debugText);
	}
	return true;
}

void Editor::ClearBrushWeightModel()
{
	m_brushWeightModelPath.clear();
	m_brushWeightModelData = {};
	m_brushWeightModelLoaded = false;
	m_brushWeightModelDirty = false;
	m_brushWeightVisualRevision = 0;
}

WModelFileData* Editor::GetBrushWeightModelDataMutable()
{
	return m_brushWeightModelLoaded ? &m_brushWeightModelData : nullptr;
}

const WModelFileData* Editor::GetBrushWeightModelData() const
{
	return m_brushWeightModelLoaded ? &m_brushWeightModelData : nullptr;
}

const std::filesystem::path& Editor::GetBrushWeightModelPath() const
{
	return m_brushWeightModelPath;
}

bool Editor::IsBrushWeightModelLoaded() const
{
	return m_brushWeightModelLoaded;
}

void Editor::MarkBrushWeightModelDirty()
{
	if (m_brushWeightModelLoaded)
	{
		m_brushWeightModelDirty = true;
		++m_brushWeightVisualRevision;
	}
}

std::uint64_t Editor::GetBrushWeightVisualRevision() const
{
	return m_brushWeightVisualRevision;
}

bool Editor::IsBrushWeightModelDirty() const
{
	return m_brushWeightModelDirty;
}

SceneEntityBase* Editor::GetBrushWeightVisualizationTargetEntity() const
{
	// 返回实际持有 MeshComponent 的实体，其 Transform 与刷权重顶点数据空间一致。
	if (m_brushWeightTargetEntity != nullptr)
		return m_brushWeightTargetEntity;
	return m_lastHierarchySelectedSkeletonOwnerEntity;
}

void* Editor::WindowVisibilitySettingsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name)
{
	return (name != nullptr && strcmp(name, "Main") == 0) ? reinterpret_cast<void*>(1) : nullptr;
}

void Editor::WindowVisibilitySettingsReadLine(ImGuiContext*, ImGuiSettingsHandler* handler, void*, const char* line)
{
	if (handler == nullptr || handler->UserData == nullptr || line == nullptr)
		return;

	Editor* editor = static_cast<Editor*>(handler->UserData);
	int value = 0;
	if (sscanf_s(line, "Hierarchy=%d", &value) == 1)
		editor->m_showHierarchyWindow = (value != 0);
	else if (sscanf_s(line, "Inspector=%d", &value) == 1)
		editor->m_showInspectorWindow = (value != 0);
	else if (sscanf_s(line, "Assets=%d", &value) == 1)
		editor->m_showAssetsWindow = (value != 0);
	else if (sscanf_s(line, "File=%d", &value) == 1)
		editor->m_showFileWindow = (value != 0);
	else if (sscanf_s(line, "ScriptEditor=%d", &value) == 1)
		editor->m_showScriptEditorWindow = (value != 0);
	else if (sscanf_s(line, "Console=%d", &value) == 1)
		editor->m_showConsoleWindow = (value != 0);
	else if (sscanf_s(line, "ScreenSettings=%d", &value) == 1)
		editor->m_showScreenSettingsWindow = (value != 0);
	else if (sscanf_s(line, "SkeletonTools=%d", &value) == 1)
		editor->m_showSkeletonToolsWindow = (value != 0);
	else if (sscanf_s(line, "SkinWeightVisualization=%d", &value) == 1)
		editor->m_showSkinWeightVisualization = (value != 0);
}

void Editor::WindowVisibilitySettingsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* outBuf)
{
	if (handler == nullptr || handler->UserData == nullptr || outBuf == nullptr)
		return;

	const Editor* editor = static_cast<const Editor*>(handler->UserData);
	outBuf->appendf("[%s]\n", "WitchcraftEditorWindows_Main");
	outBuf->appendf("Hierarchy=%d\n", editor->m_showHierarchyWindow ? 1 : 0);
	outBuf->appendf("Inspector=%d\n", editor->m_showInspectorWindow ? 1 : 0);
	outBuf->appendf("Assets=%d\n", editor->m_showAssetsWindow ? 1 : 0);
	outBuf->appendf("File=%d\n", editor->m_showFileWindow ? 1 : 0);
	outBuf->appendf("ScriptEditor=%d\n", editor->m_showScriptEditorWindow ? 1 : 0);
	outBuf->appendf("Console=%d\n", editor->m_showConsoleWindow ? 1 : 0);
	outBuf->appendf("ScreenSettings=%d\n", editor->m_showScreenSettingsWindow ? 1 : 0);
	outBuf->appendf("SkeletonTools=%d\n", editor->m_showSkeletonToolsWindow ? 1 : 0);
	outBuf->appendf("SkinWeightVisualization=%d\n", editor->m_showSkinWeightVisualization ? 1 : 0);
	outBuf->append("\n");
}

D3DWindow* Editor::GetD3DWindow() const
{
	// 直接读取不需多加判断
	return m_dx;
}

Engine* Editor::GetEngine() const
{
	return m_engine;
}

ConsoleWindow* Editor::GetConsoleWindow()
{
	return &m_consoleWindow;
}

void Editor::CreateLuaScriptAsset(const std::wstring& filePath, const std::wstring& tableName)
{
	if (m_scriptingSystem == nullptr || filePath.empty() || tableName.empty())
		return;

	m_scriptingSystem->CreateScript(filePath.c_str(), tableName.c_str());
}

bool Editor::IsDockingBackgroundWindow(const ImGuiWindow* window) const
{
	if (window == nullptr)
		return false;

	if (window->DockNode != nullptr && window->DockNode->IsCentralNode())
		return true;

	if (window->DockNodeAsHost != nullptr && window->DockNodeAsHost->IsCentralNode())
		return true;

	if (window->Name == nullptr)
		return false;

	return ImStrnicmp(window->Name, "DockSpace", 9) == 0;
}

bool Editor::IsImGuiWindowFocusedByName(const char* windowName) const
{
	ImGuiContext* context = ImGui::GetCurrentContext();
	if (context == nullptr || context->NavWindow == nullptr || context->NavWindow->Name == nullptr)
		return false;

	ImGuiWindow* window = context->NavWindow;
	while (window != nullptr)
	{
		if (window->Name != nullptr && strcmp(window->Name, windowName) == 0)
			return true;
		window = window->ParentWindow;
	}

	return false;
}

bool Editor::IsMousePointBlockedByImGui(const POINT& mousePoint) const
{
	ImGuiContext* context = ImGui::GetCurrentContext();

	const ImVec2 point(static_cast<float>(mousePoint.x), static_cast<float>(mousePoint.y));

	for (int windowIndex = context->Windows.Size - 1; windowIndex >= 0; --windowIndex)
	{
		ImGuiWindow* window = context->Windows[windowIndex];
		if (window == nullptr || window->Name == nullptr || !window->WasActive)
			continue;

		// DockSpace 中央宿主窗口本体属于场景背景，但其 TabBar 区域应视作 UI。
		if (window->DockNodeAsHost != nullptr &&
			window->DockNodeAsHost->TabBar != nullptr &&
			window->DockNodeAsHost->TabBar->BarRect.Contains(point))
		{
			return true;
		}

		if (IsDockingBackgroundWindow(window))
			continue;

		if (window->OuterRectClipped.Contains(point))
			return true;
	}

	if (context->MovingWindow != nullptr)
		return true;

	return false;
}

bool Editor::IsSceneMouseBlockedByImGui() const
{
	ImGuiContext* context = ImGui::GetCurrentContext();
	if (context == nullptr)
		return false;

	const ImGuiIO& io = ImGui::GetIO();
	if (ImGui::IsMousePosValid(&io.MousePos))
	{
		POINT mousePoint = { static_cast<LONG>(io.MousePos.x), static_cast<LONG>(io.MousePos.y) };
		if (IsMousePointBlockedByImGui(mousePoint))
			return true;
	}

	ImGuiWindow* hoveredWindow = context->HoveredWindow;
	if (hoveredWindow != nullptr && hoveredWindow->Name != nullptr && !IsDockingBackgroundWindow(hoveredWindow))
		return true;

	return context->MovingWindow != nullptr;
}

void Editor::EnqueueImGuiWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	std::lock_guard<std::mutex> lock(m_imguiMessageMutex);
	m_imguiMessageQueue.push(ImGuiWindowMessage{ hwnd, uMsg, wParam, lParam });
}

void Editor::FlushImGuiWindowMessages()
{
	std::queue<ImGuiWindowMessage> pendingMessages;
	{
		std::lock_guard<std::mutex> lock(m_imguiMessageMutex);
		pendingMessages.swap(m_imguiMessageQueue);
	}

	while (!pendingMessages.empty())
	{
		const ImGuiWindowMessage message = pendingMessages.front();
		pendingMessages.pop();

		// 跳过会导致重复输入的IME消息
		// WM_CHAR 是最终的字符消息，由TranslateMessage从WM_IME_CHAR转换而来
		// 如果同时处理WM_IME_CHAR/WM_IME_COMPOSITION和WM_CHAR，会导致字符重复
		if (message.uMsg == WM_IME_CHAR || message.uMsg == WM_IME_COMPOSITION)
		{
			// 跳过这些消息，只让WM_CHAR处理
			continue;
		}

		ImGui_ImplWin32_WndProcHandler(message.hwnd, message.uMsg, message.wParam, message.lParam);

		if (message.uMsg == WM_DPICHANGED)
			UpdateImGuiDPIScale();
	}
}

bool Editor::Init(HWND hWnd, Engine* engine, std::wstring path)
{
	this_Editor = this;

	m_hWnd = hWnd;
	m_imguiAssetPath = path;
	m_engine = engine;
	m_dx = engine->GetD3DWindow(); //我们仍然选择暂存m_dx
	m_projectSceneSystem = engine->GetprojectSceneSystem();
	m_scriptingSystem = engine->GetscriptingSystem();
	m_physicsSystem = engine->GetphysicsSystem();

	// 需要创建一个根签名
	{
		CD3DX12_DESCRIPTOR_RANGE1 texTable0;
		texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

		//根参数可以是表，根描述符或根常量。
		CD3DX12_ROOT_PARAMETER1 slotRootParameter[1];

		//创建根CBV。效果提示：从最频繁到最不频繁的顺序。
		slotRootParameter[0].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL); // sky

		auto staticSamplers = m_dx->GetStaticSamplers();

		//根签名是一个根参数的数组。
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
		rootSigDesc.Init_1_1(_countof(slotRootParameter), slotRootParameter,
			(UINT)staticSamplers.size(), staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		//使用单个插槽创建根签名，该插槽指向由单个常量缓冲区组成的描述符范围
		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc,
			D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}
		ThrowIfFailed(hr);

		ThrowIfFailed(m_dx->GetDevice()->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mGUIRootSignature.GetAddressOf())));
	}

	//
	//创建UI的SRV堆。存储每个UI窗口（不包含资源窗口）都要用到的图像资源
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	// 为 ImGui 预留一块独立的 SRV 堆，容量覆盖每帧用到的编辑器贴图资源。
	srvHeapDesc.NumDescriptors = m_dx->GetSwapChainBufferCount() * MAX_NUM_IMGUI_IMAGES_PER_FRAME + 2;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_dx->GetDevice()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mGUISrvDescriptorHeap)));

	m_imguiDescPool.Init(srvHeapDesc.NumDescriptors);

	editerCPUTexDescriptor = mGUISrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	editerGPUTexDescriptor = mGUISrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	RegisterWindowVisibilitySettingsHandler();
	ImGui_ImplWin32_EnableDpiAwareness();
	if (!ImGui_ImplWin32_Init(m_hWnd)) return false;

	ImGui_ImplDX12_InitInfo ImGuiDX12Info;
	ZeroMemory(&ImGuiDX12Info, sizeof(ImGui_ImplDX12_InitInfo));
	ImGuiDX12Info.Device = m_dx->GetDevice();
	ImGuiDX12Info.CommandQueue = m_dx->GetCommandQueue();
	ImGuiDX12Info.NumFramesInFlight = m_dx->GetSwapChainBufferCount();
	ImGuiDX12Info.RTVFormat = m_dx->GetBackBufferFormat();
	ImGuiDX12Info.DSVFormat = m_dx->GetDepthStencilFormat();
	ImGuiDX12Info.SrvDescriptorHeap = mGUISrvDescriptorHeap.Get();
	ImGuiDX12Info.LegacySingleSrvCpuDescriptor = editerCPUTexDescriptor;
	ImGuiDX12Info.LegacySingleSrvGpuDescriptor = editerGPUTexDescriptor;
	ImGuiDX12Info.SrvDescriptorAllocFn = this_Editor->ImgSrvDescriptorAlloc;
	ImGuiDX12Info.SrvDescriptorFreeFn = this_Editor->ImgSrvDescriptorFree;
	if (!ImGui_ImplDX12_Init(&ImGuiDX12Info))
		return false;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigViewportsNoAutoMerge = true;
	SetStyle();
	m_imguiBaseStyle = ImGui::GetStyle();
	SetFont();
	UpdateImGuiDPIScale();

	//下一个描述符
	editerCPUTexDescriptor.Offset(1, m_dx->GetCbvSrvUavDescriptorSize());
	editerGPUTexDescriptor.Offset(1, m_dx->GetCbvSrvUavDescriptorSize());

	m_assimpLoader.Create(m_engine);
	m_consoleWindow.Init();
	m_screenSettingsWindow.Init(m_dx, mGUISrvDescriptorHeap.Get());
	m_assetsWindow.Init(m_dx, this, mGUISrvDescriptorHeap.Get());
	m_materialEditorWindow.Init();
	m_animationEditorWindow.Init(m_engine->GetECS(), this);
	m_scriptEditorWindow.Init(m_hWnd);
	m_fileWindow.Init(m_dx, &m_assetsWindow, mGUISrvDescriptorHeap.Get());
	m_aboutWindow.Init(m_dx, mGUISrvDescriptorHeap.Get());
	m_hierarchyWindow.Init(&m_consoleWindow, &m_assimpLoader, m_engine->GetECS(), m_dx, m_engine);
	m_inspectorWindow.Init(m_dx, &m_assetsWindow, m_physicsSystem, m_engine->GetECS(), m_engine);
	ApplyWindowVisibilityState();

	return true;
}

void Editor::Update()
{
	const bool playModeActive = m_engine != nullptr && m_engine->IsPlayModeActive();
	if (playModeActive)
		m_pendingSceneAction = PendingSceneAction_None;

	// 场景切换/保存会改动 ECS 与 D3D 资源，不能在 ImGui 渲染阶段直接执行。
	// 这里统一在每帧 Update 早期处理，避免命令列表已录制后再删旧资源。
	switch (m_pendingSceneAction)
	{
	case PendingSceneAction_New:
		if (m_projectSceneSystem->NewScene(m_pendingSceneName.empty() ? L"未命名场景" : m_pendingSceneName))
			m_dx->SetPosition3f(DirectX::XMFLOAT3(0.0f, 0.0f, -5.0f));
		break;
	case PendingSceneAction_Open:
		m_projectSceneSystem->OpenScene();
		break;
	case PendingSceneAction_Save:
		m_projectSceneSystem->SaveScene();
		break;
	case PendingSceneAction_Reload:
	{
		// 项目设置中的“应用并重载场景”必须在 Update 阶段执行，
		// 避免在 ImGui 渲染阶段重建 ECS/渲染资源导致命令列表录制期资源失效。
		if (m_engine != nullptr && m_projectSceneSystem != nullptr)
		{
			WitchcraECS* ecs = m_engine->GetECS();
			if (ecs != nullptr)
			{
				for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
				{
					const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
					ecs->SetEntitySceneTypeVertexColor(sceneType, m_projectSceneTypeColorDraft[typeIndex], false);
				}

				if (m_projectSceneSystem->ReloadCurrentScene())
				{
					for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
					{
						const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
						m_projectSceneTypeColorDraft[typeIndex] = ecs->GetEntitySceneTypeVertexColor(sceneType);
					}
					m_projectSceneTypeColorDraftDirty = false;
				}
			}
		}
		break;
	}
	default:
		break;
	}
	m_pendingSceneAction = PendingSceneAction_None;
	m_pendingSceneName.clear();

	// 把导入等会改动渲染资源的编辑器操作延后到非渲染录制阶段执行。
	m_hierarchyWindow.ProcessDeferredActions();
	{
		std::uint32_t createKind = 0;
		std::wstring defaultName;
		bool refreshSkyTextures = false;
		bool clearSelectionFirst = false;
		if (m_hierarchyWindow.ConsumePendingCreateRequest(&createKind, &defaultName, &refreshSkyTextures, &clearSelectionFirst))
			OpenQueuedCreateEntityWindow(createKind, defaultName, refreshSkyTextures, clearSelectionFirst);
	}
	if (m_pendingFocusCreatedEntityInHierarchy && m_engine != nullptr)
	{
		WitchcraECS* ecs = m_engine->GetECS();
		if (ecs != nullptr && ecs->GetSelectedEntity() != nullptr)
		{
			m_hierarchyWindow.RequestFocusSelectedEntity();
			m_pendingFocusCreatedEntityInHierarchy = false;
		}
	}
	m_fileWindow.Update();

	ImGuiIO& io = ImGui::GetIO();
	const bool captureKeyboard = io.WantCaptureKeyboard;
	const bool blockSceneMouse = IsSceneMouseBlockedByImGui();
	const bool captureSceneMouse = blockSceneMouse;

	UpdateKeyboard(io, captureKeyboard);
	UpdateMouse(io, captureSceneMouse);
	UpdateGizmoData();
	m_skeletonEditorTool.SetEnabled(m_showSkeletonToolsWindow);
	m_skeletonEditorTool.SetEditor(this);
	const bool isBrushWeightMode = m_skeletonEditorTool.GetInteractionMode() == SkeletonInteractionMode::BrushWeight;
	if (isBrushWeightMode && !m_wasBrushWeightMode)
	{
		// 仅当刷权重数据未加载时才重置解析标志；已加载时保留现有数据。
		if (!IsBrushWeightModelLoaded())
			m_brushWeightTargetResolved = false;
		m_wasBrushWeightMode = true;
	}
	else if (!isBrushWeightMode && m_wasBrushWeightMode)
	{
		m_wasBrushWeightMode = false;
	}
	if (m_brushWeightVisualRevision != m_lastAppliedBrushWeightVisualRevision)
	{
		m_lastAppliedBrushWeightVisualRevision = m_brushWeightVisualRevision;
		if (m_dx != nullptr &&
			m_brushWeightTargetEntity != nullptr &&
			m_skeletonEditorTool.GetInteractionMode() != SkeletonInteractionMode::BrushWeight)
			m_dx->SetSkinWeightVisualizationTarget(m_brushWeightTargetEntity);
	}
	// 骨骼节点覆盖层与蒙皮权重着色是两条独立调试链路。
	// 打开骨骼编辑器不应隐式开启整模型的额外蒙皮绘制 pass。
	if (m_dx != nullptr)
		m_dx->SetSkinWeightVisualizationEnabled(m_showSkinWeightVisualization);
	if (m_engine != nullptr)
	{
		WitchcraECS* ecs = m_engine->GetECS();
		SceneEntityBase* brushTargetEntity = m_brushWeightTargetEntity;

		if (isBrushWeightMode)
		{
			if (!m_brushWeightTargetResolved)
			{
				SceneEntityBase* resolvedBrushTargetEntity = ResolveBrushWeightTargetEntity(m_lastHierarchySelectedSkeletonOwnerEntity);
				m_brushWeightTargetEntity = resolvedBrushTargetEntity;
				m_brushWeightTargetResolved = true;
				if (m_brushWeightTargetEntity != nullptr)
				{
					LoadBrushWeightModelForEntity(m_brushWeightTargetEntity);
					m_dx->BuildBrushWeightVisualization();
				}
			}

			if (m_dx != nullptr && m_brushWeightTargetEntity != nullptr)
				m_dx->SetSkinWeightVisualizationTarget(m_brushWeightTargetEntity);
		}
		else
		{
			SceneEntityBase* activeSkeletonOwnerEntity = nullptr;
			std::int32_t activeSkeletonBoneIndex = -1;
			const bool hasSelectedSkeletonBone =
				ecs != nullptr &&
				m_hierarchyWindow.TryGetSelectedSkeletonBone(&activeSkeletonOwnerEntity, &activeSkeletonBoneIndex);
			if (!hasSelectedSkeletonBone && ecs != nullptr)
			{
				SceneEntityBase* selectedEntity = ecs->GetSelectedEntity();
				if (selectedEntity != nullptr && !ecs->IsEnvironmentEntity(selectedEntity))
				{
					if (SceneEntityBase* skeletonOwnerEntity = ResolveSkeletonOwnerEntity(selectedEntity))
					{
						const Witchcraft::Animation::SkeletonData* skeletonData =
							ecs->GetSkeletonData(skeletonOwnerEntity);
						activeSkeletonOwnerEntity = skeletonOwnerEntity;
						if (skeletonData->Topology.IsValidBoneIndex(skeletonData->Topology.RootBoneIndex))
							activeSkeletonBoneIndex = skeletonData->Topology.RootBoneIndex;
						else
							activeSkeletonBoneIndex = -1;
					}
				}
			}

			if (ecs != nullptr &&
				activeSkeletonOwnerEntity != nullptr &&
				(activeSkeletonOwnerEntity != m_lastHierarchySelectedSkeletonOwnerEntity ||
					activeSkeletonBoneIndex != m_lastHierarchySelectedSkeletonBoneIndex))
			{
				m_hierarchyWindow.EnsureSkeletonHierarchyForEntity(
					activeSkeletonOwnerEntity,
					activeSkeletonBoneIndex,
					false);
				if (Witchcraft::Animation::SkeletonData* skeletonData = ecs->GetSkeletonData(activeSkeletonOwnerEntity))
				{
					DirectX::XMFLOAT4X4 ownerWorldMatrix{};
					DirectX::XMStoreFloat4x4(&ownerWorldMatrix, DirectX::XMMatrixIdentity());
					(void)ecs->GetEntityWorldMatrix(activeSkeletonOwnerEntity, &ownerWorldMatrix);
					m_skeletonEditorTool.SetEnabled(true);
					m_skeletonEditorTool.LoadFromTopology(
						skeletonData->Topology,
						&skeletonData->GlobalPose,
						&ownerWorldMatrix);
					m_skeletonEditorTool.SelectJoint(static_cast<int>(activeSkeletonBoneIndex));
					m_lastHierarchySelectedSkeletonOwnerEntity = activeSkeletonOwnerEntity;
					m_lastHierarchySelectedSkeletonBoneIndex = activeSkeletonBoneIndex;
					m_brushWeightTargetEntity = activeSkeletonOwnerEntity;
					m_brushWeightTargetResolved = false;
				}
			}

			if (activeSkeletonOwnerEntity != nullptr && !IsBrushWeightModelLoaded())
			{
				SceneEntityBase* resolvedMeshEntity = ResolveBrushWeightTargetEntity(activeSkeletonOwnerEntity);
				SceneEntityBase* loadTargetEntity = resolvedMeshEntity != nullptr ? resolvedMeshEntity : activeSkeletonOwnerEntity;
				const bool brushModelLoaded = LoadBrushWeightModelForEntity(loadTargetEntity);
				if (brushModelLoaded && m_dx != nullptr)
				{
					m_brushWeightTargetEntity = loadTargetEntity;
					m_dx->SetSkinWeightVisualizationTarget(loadTargetEntity);
					m_dx->BuildBrushWeightVisualization();
				}
			}
			else if (activeSkeletonOwnerEntity == nullptr)
			{
				m_lastHierarchySelectedSkeletonOwnerEntity = nullptr;
				m_lastHierarchySelectedSkeletonBoneIndex = -1;
				m_brushWeightTargetEntity = nullptr;
				m_brushWeightTargetResolved = false;
				if (m_dx != nullptr)
					m_dx->SetSkinWeightVisualizationTarget(nullptr);
			}
		}

		if (ecs != nullptr &&
			m_lastHierarchySelectedSkeletonOwnerEntity != nullptr &&
			!isBrushWeightMode &&
			m_skeletonEditorTool.HasPendingChanges())
		{
			if (Witchcraft::Animation::SkeletonData* skeletonData = ecs->GetSkeletonData(m_lastHierarchySelectedSkeletonOwnerEntity))
			{
				if (ApplySkeletonJointsToData(m_skeletonEditorTool.GetJoints(), skeletonData))
				{
					(void)ecs->SyncSkeletonDataToComponent(m_lastHierarchySelectedSkeletonOwnerEntity);
					if (m_skeletonEditorTool.GetJoints().empty())
					{
						ecs->DeleteSkeletonHierarchyForEntity(m_lastHierarchySelectedSkeletonOwnerEntity);
						m_lastHierarchySelectedSkeletonBoneIndex = -1;
					}
					else
					{
						(void)ecs->RebuildSkeletonHierarchyForEntity(m_lastHierarchySelectedSkeletonOwnerEntity);
						const std::int32_t selectedJointIndex = m_skeletonEditorTool.GetSelectedJointIndex();
						m_hierarchyWindow.EnsureSkeletonHierarchyForEntity(
							m_lastHierarchySelectedSkeletonOwnerEntity,
							selectedJointIndex,
							true);
						m_lastHierarchySelectedSkeletonBoneIndex = selectedJointIndex;
					}
					m_skeletonEditorTool.ClearPendingChanges();
				}
			}
		}

		if (m_projectSceneSystem != nullptr &&
			ecs != nullptr &&
			m_lastHierarchySelectedSkeletonOwnerEntity != nullptr &&
			m_skeletonEditorTool.ConsumeSaveToModelRequest())
		{
			(void)m_projectSceneSystem->SaveSkeletonToModel(m_lastHierarchySelectedSkeletonOwnerEntity);
		}
	}

	WitchcraECS* ecs = m_engine != nullptr ? m_engine->GetECS() : nullptr;
	if (ecs != nullptr && m_lastHierarchySelectedSkeletonOwnerEntity != nullptr)
	{
		if (const Witchcraft::Animation::SkeletonData* skeletonData =
			ecs->GetSkeletonData(m_lastHierarchySelectedSkeletonOwnerEntity))
		{
			DirectX::XMFLOAT4X4 ownerWorldMatrix{};
			DirectX::XMStoreFloat4x4(&ownerWorldMatrix, DirectX::XMMatrixIdentity());
			if (ecs->GetEntityWorldMatrix(m_lastHierarchySelectedSkeletonOwnerEntity, &ownerWorldMatrix))
			{
				const AnimatorComponent* animatorComponent =
					ecs->GetComponent<AnimatorComponent>(m_lastHierarchySelectedSkeletonOwnerEntity);
				if (animatorComponent == nullptr || !animatorComponent->HasPlayableLayers())
				{
					m_skeletonEditorTool.UpdateJointPositionsFromBindPose(
						skeletonData->Topology,
						ownerWorldMatrix);
				}
				else
				{
					m_skeletonEditorTool.UpdateJointPositionsFromGlobalPose(
						skeletonData->GlobalPose,
						ownerWorldMatrix);
				}
			}
		}
	}
	m_skeletonEditorTool.UpdateOverlay(m_dx);

	UpdateEditUI();
}

void Editor::UpdateKeyboard(const ImGuiIO& io, bool captureKeyboard)
{
	KeyboardClass* keyboard = m_engine->GetKeyboard();

	while (!keyboard->CharBufferIsEmpty())
	{
		BYTE ch = keyboard->ReadChar();
		(void)ch;
	}

	HandleHotkeys(keyboard, captureKeyboard);
	HandleKeyboardMove(keyboard, io, captureKeyboard);
}

void Editor::HandleHotkeys(KeyboardClass* keyboard, bool captureKeyboard)
{
	while (!keyboard->KeyBufferIsEmpty())
	{
		KeyboardEvent kbe = keyboard->ReadKey();
		BYTE keycode = kbe.GetKeyCode();
		if (!kbe.IsPress())
			continue;

		const bool ctrlPressed =
			keyboard->KeyIsPressed(VK_CONTROL) ||
			keyboard->KeyIsPressed(VK_LCONTROL) ||
			keyboard->KeyIsPressed(VK_RCONTROL);
		const bool shiftPressed =
			keyboard->KeyIsPressed(VK_SHIFT) ||
			keyboard->KeyIsPressed(VK_LSHIFT) ||
			keyboard->KeyIsPressed(VK_RSHIFT);
		const bool assetsWindowFocused = IsImGuiWindowFocusedByName("资源");
		const bool playModeActive = m_engine != nullptr && m_engine->IsPlayModeActive();
		if (ctrlPressed && keycode == VK_F11)
		{
			m_dx->OnResize(!m_dx->GetWindowInfo().fullscreenState);
		}
		else if (ctrlPressed && keycode == VK_F3)
		{
			// 这里要让编辑器的UI不要再绘制的同时，编辑器(主要是imgui)也不要在处理用户输入的任何消息了。
			// 否则会导致事件堆积。
			ImGui::GetIO().SetAppAcceptingEvents(m_dx->SerEditorDrawd());
		}
		else if (keycode == VK_F8)
		{
			// F8：锁定/解锁视锥剔除参考视角（仅影响剔除参考，不改相机本身）。
			m_dx->ToggleFrustumCullingReferenceLock();
		}
		else if (!captureKeyboard && assetsWindowFocused && ctrlPressed && !shiftPressed && keycode == 'R')
		{
			m_showAssetsWindow = true;
			m_assetsWindow.NeedRender(true);
			MarkWindowVisibilitySettingsDirty();
			m_assetsWindow.RefreshDir();
		}
		else if (!captureKeyboard && assetsWindowFocused && !ctrlPressed && !shiftPressed && keycode == VK_BACK)
		{
			m_showAssetsWindow = true;
			m_assetsWindow.NeedRender(true);
			MarkWindowVisibilitySettingsDirty();
			m_assetsWindow.GoBackDir();
		}
		else if (!captureKeyboard && assetsWindowFocused && !ctrlPressed && !shiftPressed && keycode == VK_F2)
		{
			m_showAssetsWindow = true;
			m_assetsWindow.NeedRender(true);
			MarkWindowVisibilitySettingsDirty();
			m_assetsWindow.RequestRenameSelectedAsset();
		}
		else if (!playModeActive && !captureKeyboard && ctrlPressed && !shiftPressed && keycode == 'S')
			m_pendingSceneAction = PendingSceneAction_Save;
		else if (!playModeActive && !captureKeyboard && ctrlPressed && !shiftPressed && keycode == 'O')
			m_pendingSceneAction = PendingSceneAction_Open;
		else if (!playModeActive && !captureKeyboard && ctrlPressed && !shiftPressed && keycode == 'N')
		{
			m_pendingSceneAction = PendingSceneAction_New;
			m_pendingSceneName = L"未命名场景";
		}
		else if (!playModeActive && !captureKeyboard && !ctrlPressed && !shiftPressed && keycode == VK_F2)
		{
			m_showHierarchyWindow = true;
			m_hierarchyWindow.NeedRender(true);
			MarkWindowVisibilitySettingsDirty();
			m_hierarchyWindow.RequestRenameSelectedEntity();
		}
		else if (!playModeActive && !captureKeyboard && ctrlPressed && !shiftPressed && keycode == 'D')
		{
			m_showHierarchyWindow = true;
			m_hierarchyWindow.NeedRender(true);
			MarkWindowVisibilitySettingsDirty();
			m_hierarchyWindow.RequestDuplicateSelectedEntities();
		}
		else if (!captureKeyboard && ctrlPressed && keycode == 'Q')
		{
			if (m_skeletonEditorTool.HasActiveJointSelection())
				m_skeletonEditorTool.SetGizmoMode(GizmoMode::Translate);
			else
				m_transformGizmo.SetMode(GizmoMode::None);
		}
		else if (!captureKeyboard && ctrlPressed && keycode == 'W')
		{
			if (m_skeletonEditorTool.HasActiveJointSelection())
				m_skeletonEditorTool.SetGizmoMode(GizmoMode::Translate);
			else
				m_transformGizmo.SetMode(GizmoMode::Translate);
		}
		else if (!captureKeyboard && ctrlPressed && keycode == 'E')
		{
			if (m_skeletonEditorTool.HasActiveJointSelection())
				m_skeletonEditorTool.SetGizmoMode(GizmoMode::Rotate);
			else
				m_transformGizmo.SetMode(GizmoMode::Rotate);
		}
		else if (!captureKeyboard && ctrlPressed && keycode == 'R')
		{
			if (!m_skeletonEditorTool.HasActiveJointSelection())
				m_transformGizmo.SetMode(GizmoMode::Scale);
		}
	}
}

void Editor::HandleKeyboardMove(KeyboardClass* keyboard, const ImGuiIO& io, bool captureKeyboard)
{
	if (captureKeyboard)
		return;

	UINT MovementDirection = MOVE_NOT_SPECIFIDE;
	bool MoveCamera = false;

	if (keyboard->KeyIsPressed('W'))
	{
		MovementDirection = MovementDirection + MOVE_DEEPEN;
		MoveCamera = true;
	}
	if (keyboard->KeyIsPressed('S'))
	{
		MovementDirection = MovementDirection + MOVE_FROMAW;
		MoveCamera = true;
	}
	if (keyboard->KeyIsPressed('A'))
	{
		MovementDirection = MovementDirection + MOVE_LEFT;
		MoveCamera = true;
	}
	if (keyboard->KeyIsPressed('D'))
	{
		MovementDirection = MovementDirection + MOVE_RIGHT;
		MoveCamera = true;
	}
	if (keyboard->KeyIsPressed(VK_SPACE))
	{

	}

	if (!MoveCamera || (MovementDirection == MOVE_NOT_SPECIFIDE))
		return;

	DirectX::XMFLOAT3 distance(0.0f, 0.0f, 0.0f);
	if (MovementDirection == MOVE_UP)
		distance.y = -1.0f;
	else if (MovementDirection == MOVE_DOWN)
		distance.y = +1.0f;
	else if (MovementDirection == MOVE_DEEPEN)
		distance.z = +1.0f;
	else if (MovementDirection == MOVE_FROMAW)
		distance.z = -1.0f;
	else if (MovementDirection == MOVE_LEFT)
		distance.x = -1.0f;
	else if (MovementDirection == MOVE_RIGHT)
		distance.x = +1.0f;
	else if (MovementDirection == (MOVE_UP + MOVE_LEFT))
	{
		distance.y = -1.0f;
		distance.x = -1.0f;
	}
	else if (MovementDirection == (MOVE_UP + MOVE_RIGHT))
	{
		distance.y = -1.0f;
		distance.x = +1.0f;
	}
	else if (MovementDirection == (MOVE_DOWN + MOVE_LEFT))
	{
		distance.y = +1.0f;
		distance.x = -1.0f;
	}
	else if (MovementDirection == (MOVE_DOWN + MOVE_RIGHT))
	{
		distance.y = +1.0f;
		distance.x = +1.0f;
	}
	else if (MovementDirection == (MOVE_LEFT + MOVE_DEEPEN))
	{
		distance.x = -1.0f;
		distance.z = +1.0f;
	}
	else if (MovementDirection == (MOVE_LEFT + MOVE_FROMAW))
	{
		distance.x = -1.0f;
		distance.z = -1.0f;
	}
	else if (MovementDirection == (MOVE_RIGHT + MOVE_DEEPEN))
	{
		distance.x = +1.0f;
		distance.z = +1.0f;
	}
	else if (MovementDirection == (MOVE_RIGHT + MOVE_FROMAW))
	{
		distance.x = +1.0f;
		distance.z = -1.0f;
	}
	m_dx->MoveCamera(io.DeltaTime, distance);
}

void Editor::UpdateMouse(const ImGuiIO& io, bool captureSceneMouse)
{
	MouseClass* mouse = m_engine->GetMouse();

	while (!mouse->EventBufferIsEmpty())
	{
		MouseEvent me = mouse->ReadEvent();
		if (!me.IsValid())
			break;

		const MouseEvent::EventType mouseEventType = me.GetType();
		if (!captureSceneMouse &&
			mouseEventType == MouseEvent::EventType::LPress &&
			m_showSkeletonToolsWindow &&
			m_skeletonEditorTool.GetInteractionMode() == SkeletonInteractionMode::AddChild)
		{
			WitchcraECS* ecs = m_engine->GetECS();
			SceneEntityBase* selectedEntity = ecs != nullptr ? ecs->GetSelectedEntity() : nullptr;
			if (ecs != nullptr &&
				selectedEntity != nullptr &&
				!ecs->IsEnvironmentEntity(selectedEntity) &&
				(selectedEntity != m_lastHierarchySelectedSkeletonOwnerEntity || m_skeletonEditorTool.GetJoints().empty()))
			{
				if (Witchcraft::Animation::SkeletonData* skeletonData = ecs->EnsureSkeletonData(selectedEntity))
				{
					DirectX::XMFLOAT4X4 ownerWorldMatrix{};
					DirectX::XMStoreFloat4x4(&ownerWorldMatrix, DirectX::XMMatrixIdentity());
					(void)ecs->GetEntityWorldMatrix(selectedEntity, &ownerWorldMatrix);
					m_hierarchyWindow.EnsureSkeletonHierarchyForEntity(selectedEntity, -1, true);
					m_skeletonEditorTool.SetEnabled(true);
					m_skeletonEditorTool.LoadFromTopology(
						skeletonData->Topology,
						&skeletonData->GlobalPose,
						&ownerWorldMatrix);
					const std::int32_t rootBoneIndex = skeletonData->Topology.IsValidBoneIndex(skeletonData->Topology.RootBoneIndex)
						? skeletonData->Topology.RootBoneIndex
						: -1;
					m_skeletonEditorTool.SelectJoint(static_cast<int>(rootBoneIndex));
					m_lastHierarchySelectedSkeletonOwnerEntity = selectedEntity;
					m_lastHierarchySelectedSkeletonBoneIndex = rootBoneIndex;
				}
			}
		}

		const bool isBrushWeightMode = m_skeletonEditorTool.GetInteractionMode() == SkeletonInteractionMode::BrushWeight;
		const bool skeletonWantsMouseCapture =
			m_skeletonEditorTool.IsBrushDragging() ||
			(!captureSceneMouse && (!isBrushWeightMode || m_skeletonEditorTool.WantsMouseCapture(me, mouse)));
		if (skeletonWantsMouseCapture &&
			m_skeletonEditorTool.HandleMouse(me, mouse, m_engine, m_dx, m_hWnd))
			continue;

		if (mouseEventType == MouseEvent::EventType::LPress &&
			!captureSceneMouse &&
			!isBrushWeightMode)
		{
			QueuePickRequest(me);
		}

		if (captureSceneMouse)
		{
			HandleBlockedMouseEvent(me);
			continue;
		}

		if (HandleGizmoMouse(me, mouse))
			continue;

		HandleHoverMouse(me, mouse);
		HandlePanMouse(me, mouse, io);
		HandleRotateMouse(me, mouse, io);
		HandleWheelMouse(me, io);
	}
}

void Editor::QueuePickRequest(const MouseEvent& me)
{
	// 左键拾取请求统一延迟到 ImGui NewFrame 后再做 UI 命中判定，
	// 避免输入线程与渲染线程时序不同步导致的穿透。
	point = { me.GetPosX(), me.GetPosY() };
	m_pendingPickPoint = point;
	KeyboardClass* keyboard = m_engine != nullptr ? m_engine->GetKeyboard() : nullptr;
	m_pendingPickAdditiveSelection =
		keyboard != nullptr &&
		(keyboard->KeyIsPressed(VK_SHIFT) ||
			keyboard->KeyIsPressed(VK_LSHIFT) ||
			keyboard->KeyIsPressed(VK_RSHIFT));
	m_hasPendingPickRequest = true;
}

void Editor::HandleBlockedMouseEvent(const MouseEvent& me)
{
	if (me.GetType() == MouseEvent::EventType::LRelease)
	{
		if (m_transformGizmo.IsDragging())
			m_transformGizmo.EndDrag(); // 结束拖动控制
	}
	else if (me.GetType() == MouseEvent::EventType::RRelease);
	else if (me.GetType() == MouseEvent::EventType::LPress);
	else if (me.GetType() == MouseEvent::EventType::RPress);

	point = { me.GetPosX(), me.GetPosY() };
}

bool Editor::HandleGizmoMouse(const MouseEvent& me, MouseClass* mouse)
{
	if (m_skeletonEditorTool.HasActiveJointSelection())
	{
		if (me.GetType() == MouseEvent::EventType::LRelease)
		{
			m_skeletonEditorTool.EndGizmoDrag();
			point = { me.GetPosX(), me.GetPosY() };
			return true;
		}

		if (me.GetType() == MouseEvent::EventType::Move && mouse->IsLeftDown())
		{
			point = { me.GetPosX(), me.GetPosY() };
			DirectX::XMVECTOR gizmoRayWorldPos = DirectX::XMVectorZero();
			DirectX::XMVECTOR gizmoRayWorldDir = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
			RayVector(static_cast<float>(point.x), static_cast<float>(point.y), gizmoRayWorldPos, gizmoRayWorldDir);
			if (m_skeletonEditorTool.UpdateGizmoDrag(gizmoRayWorldPos, gizmoRayWorldDir))
				return true;
		}

		return false;
	}

	if (me.GetType() == MouseEvent::EventType::LRelease && m_transformGizmo.IsDragging())
	{
		m_transformGizmo.EndDrag();
		point = { me.GetPosX(), me.GetPosY() };
		return true;
	}

	if (me.GetType() == MouseEvent::EventType::Move && mouse->IsLeftDown() && m_transformGizmo.IsDragging())
	{
		point = { me.GetPosX(), me.GetPosY() };
		DirectX::XMVECTOR gizmoRayWorldPos = DirectX::XMVectorZero();
		DirectX::XMVECTOR gizmoRayWorldDir = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		RayVector(static_cast<float>(point.x), static_cast<float>(point.y), gizmoRayWorldPos, gizmoRayWorldDir);
		if (WitchcraECS* ecs = m_engine != nullptr ? m_engine->GetECS() : nullptr)
		{
			m_transformGizmo.UpdateDrag(this, ecs, gizmoRayWorldPos, gizmoRayWorldDir);
			if (m_dx != nullptr)
				m_dx->UpdateRenderItemsTransformFromEntity(m_transformGizmo.GetDrag()->Entity, ecs);
		}
		return true;
	}

	return false;
}

void Editor::HandleHoverMouse(const MouseEvent& me, MouseClass* mouse)
{
	if (me.GetType() == MouseEvent::EventType::Move &&
		!mouse->IsLeftDown() &&
		!mouse->IsRightDown() &&
		!mouse->IsMiddleDown())
	{
		point = { me.GetPosX(), me.GetPosY() };
		DirectX::XMVECTOR gizmoRayWorldPos = DirectX::XMVectorZero();
		DirectX::XMVECTOR gizmoRayWorldDir = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		RayVector(static_cast<float>(point.x), static_cast<float>(point.y), gizmoRayWorldPos, gizmoRayWorldDir);
		if (m_skeletonEditorTool.HasActiveJointSelection())
			m_skeletonEditorTool.UpdateGizmoHover(gizmoRayWorldPos, gizmoRayWorldDir);
		else if (WitchcraECS* ecs = m_engine != nullptr ? m_engine->GetECS() : nullptr)
			m_transformGizmo.UpdateHover(this, ecs, gizmoRayWorldPos, gizmoRayWorldDir);
	}
}

void Editor::HandlePanMouse(const MouseEvent& me, MouseClass* mouse, const ImGuiIO& io)
{
	if (!mouse->IsRightDown())
		return;

	POINT pt = point;
	point = { me.GetPosX(), me.GetPosY() };

	if (me.GetType() != MouseEvent::EventType::Move)
		return;

	UINT MovementDirection = MOVE_NOT_SPECIFIDE;
	if ((pt.x - point.x) > 0)
		MovementDirection = MovementDirection + MOVE_RIGHT;
	else if ((pt.x - point.x) < 0)
		MovementDirection = MovementDirection + MOVE_LEFT;
	if ((pt.y - point.y) > 0)
		MovementDirection = MovementDirection + MOVE_DOWN;
	else if ((pt.y - point.y) < 0)
		MovementDirection = MovementDirection + MOVE_UP;

	if (MovementDirection != MOVE_NOT_SPECIFIDE)
		m_dx->MoveCamera(io.DeltaTime, DirectX::XMFLOAT3((pt.x - point.x), -(pt.y - point.y), 0.0f));
}

void Editor::HandleRotateMouse(const MouseEvent& me, MouseClass* mouse, const ImGuiIO& io)
{
	if (mouse->IsMiddleDown())
	{
		if (me.GetType() == MouseEvent::EventType::MPress)
		{
			point = { me.GetPosX(), me.GetPosY() };
			mSmoothedCameraRotateDelta = { 0.0f, 0.0f };
			m_dx->SetRotation3f(m_dx->GetRotation3f());
		}
		else if (me.GetType() == MouseEvent::EventType::Move)
		{
			POINT pt = point;
			point = { me.GetPosX(), me.GetPosY() };
			DirectX::XMFLOAT2 angle(
				static_cast<float>(pt.x - point.x),
				static_cast<float>(pt.y - point.y));
			if (mEnableCameraRotateSmoothing)
			{
				float smoothFactor = mCameraRotateSmoothFactor;
				if (smoothFactor < 0.0f)
					smoothFactor = 0.0f;
				else if (smoothFactor > 1.0f)
					smoothFactor = 1.0f;
				mSmoothedCameraRotateDelta.x += (angle.x - mSmoothedCameraRotateDelta.x) * smoothFactor;
				mSmoothedCameraRotateDelta.y += (angle.y - mSmoothedCameraRotateDelta.y) * smoothFactor;
				angle = mSmoothedCameraRotateDelta;
			}
			m_dx->RotateCamera(io.DeltaTime, angle);
		}
	}
	else
	{
		mSmoothedCameraRotateDelta = { 0.0f, 0.0f };
	}
}

void Editor::HandleWheelMouse(const MouseEvent& me, const ImGuiIO& io)
{
	if (me.GetType() == MouseEvent::EventType::WheelUp)
	{
		m_dx->MoveCamera(io.DeltaTime, DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));
	}
	else if (me.GetType() == MouseEvent::EventType::WheelDown)
	{
		m_dx->MoveCamera(io.DeltaTime, DirectX::XMFLOAT3(0.0f, 0.0f, -1.0f));
	}
}

void Editor::UpdateGizmoData()
{
	WitchcraECS* ecs = m_engine != nullptr ? m_engine->GetECS() : nullptr;
	if (m_skeletonEditorTool.HasActiveJointSelection())
	{
		m_transformGizmo.ClearSelection();
		m_dx->SetGizmoRenderData(m_skeletonEditorTool.BuildGizmoRenderData(m_dx));
	}
	else
	{
		m_transformGizmo.UpdateSelection(ecs != nullptr ? ecs->GetSelectedEntity() : nullptr);
		m_dx->SetGizmoRenderData(m_transformGizmo.BuildRenderData(this, ecs));
	}
}

void Editor::UpdateEditUI()
{
	ImGuiIO& io = ImGui::GetIO();
	ProcessPendingPick(io);
	NotifyDisplayResize(m_dx->GetWindowInfo().Width, m_dx->GetWindowInfo().Height);
	UpdateImGuiDPIScale();
}

void Editor::ProcessPendingPick(const ImGuiIO& io)
{
	if (!m_hasPendingPickRequest)
		return;

	// 注意：WantCaptureMouse 在 Docking 场景下会长时间保持 true，
	// 直接使用会导致场景拾取被永久屏蔽。
	// 这里只按“是否真正悬停在 UI 面板上”来屏蔽拾取。
	const bool blockPickByUi = IsSceneMouseBlockedByImGui() || IsMousePointBlockedByImGui(m_pendingPickPoint);
	if (!blockPickByUi)
	{
		DirectX::XMVECTOR pickRayWorldPos = DirectX::XMVectorZero();
		DirectX::XMVECTOR pickRayWorldDir = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		RayVector(static_cast<float>(m_pendingPickPoint.x), static_cast<float>(m_pendingPickPoint.y), pickRayWorldPos, pickRayWorldDir);
		WitchcraECS* ecs = m_engine != nullptr ? m_engine->GetECS() : nullptr;
		bool beganGizmoDrag = false;
		if (m_skeletonEditorTool.HasActiveJointSelection())
			beganGizmoDrag = m_skeletonEditorTool.TryBeginGizmoDrag(m_engine, m_dx, m_hWnd, pickRayWorldPos, pickRayWorldDir);
		else
			beganGizmoDrag =
				ecs != nullptr &&
				m_transformGizmo.TryBeginDrag(this, ecs, pickRayWorldPos, pickRayWorldDir);
		if (!beganGizmoDrag)
			RunRay(m_pendingPickPoint, m_pendingPickAdditiveSelection || io.KeyShift);
	}
	m_hasPendingPickRequest = false;
	m_pendingPickAdditiveSelection = false;
}

void Editor::Render()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_dx->GetRtv();
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dx->GetDsv();

	// 指定我们要渲染到的缓冲区。
	m_dx->GetCurrFrameResourceCommandList()->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	// 切换CBV堆
	m_dx->GetCurrFrameResourceCommandList()->SetDescriptorHeaps(1, mGUISrvDescriptorHeap.GetAddressOf());
	m_dx->GetCurrFrameResourceCommandList()->SetGraphicsRootSignature(mGUIRootSignature.Get());
	editerGPUTexDescriptor.Offset(0, m_dx->GetCbvSrvUavDescriptorSize());
	//m_dx->GetThreadCommandList(threadIndex)->SetGraphicsRootDescriptorTable(0, editerTexDescriptor);

	{
		// ImGui Win32 后端在主线程与渲染线程都会触达，这里统一串行化避免并发访问。
		std::lock_guard<std::recursive_mutex> imguiContextLock(m_imguiContextMutex);

		// 将 Win32 消息转发到渲染线程处理，确保 ImGui 后端线程归属一致。
		FlushImGuiWindowMessages();

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ApplyWindowVisibilityState();

		ImGuiStyle* style = &ImGui::GetStyle();
		ImVec4* colors = style->Colors;
		BYTE offset = 0x10;
		ImVec4 windowBg = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x2E - offset, 0x2E - offset, 0x2E - offset, 0x00));
		colors[ImGuiCol_WindowBg] = windowBg;

		{
			SetDocking();

			windowBg = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x2E - offset, 0x2E - offset, 0x2E - offset, 0xFF));
			colors[ImGuiCol_WindowBg] = windowBg;

			RenderBar();
			RenderDownBar();
			RenderUpBar();
			m_assetsWindow.Render();
			m_materialEditorWindow.Render();
			m_animationEditorWindow.Render();
			m_screenSettingsWindow.Render();
			m_hierarchyWindow.Render();
			m_inspectorWindow.Render();
			m_fileWindow.Render();
			m_scriptEditorWindow.Render();
			if (m_showScriptEditorWindow != m_scriptEditorWindow.IsRendering())
			{
				m_showScriptEditorWindow = m_scriptEditorWindow.IsRendering();
				MarkWindowVisibilitySettingsDirty();
			}
			m_consoleWindow.Render();
			m_aboutWindow.Render();
			RenderProjectSettingsWindow();
			RenderToolBar();
			m_skeletonEditorTool.RenderWindow(m_DpiScale, &m_showSkeletonToolsWindow);

			if (openCreateWindow)
			{
				if (CreaItem == CreateItem::SkyItem)
				{
					if (RenderCreateSkyWindow())
						m_engine->QueueCreateSkyEntity(name, m_skyTextureFiles[m_selectedSkyTextureIndex]);
				}
				else
				{
					RenderCreateObjectWindow();
				}
			}
		}

		m_lastImGuiMouseCursor.store(static_cast<int>(ImGui::GetMouseCursor()), std::memory_order_relaxed);
		m_hasImGuiCursorSnapshot.store(true, std::memory_order_relaxed);

		ImGui::Render();

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_dx->GetCurrFrameResourceCommandList());

		// 更新和渲染附加平台窗口
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
	}
}

bool Editor::OpenMaterialEditor(const std::wstring& path)
{
	return m_materialEditorWindow.OpenMaterialFile(path);
}

bool Editor::OpenAnimationEditor(const std::wstring& path)
{
	return m_animationEditorWindow.OpenAnimationFile(path);
}

bool Editor::OpenScriptEditor(const std::wstring& path)
{
	const bool opened = m_scriptEditorWindow.OpenScriptFile(path);
	m_showScriptEditorWindow = true;
	MarkWindowVisibilitySettingsDirty();
	return opened;
}

void Editor::Shutdown()
{
	m_hasImGuiCursorSnapshot.store(false, std::memory_order_relaxed);
	m_showSkeletonToolsWindow = false;
	m_showSkinWeightVisualization = false;
	m_lastHierarchySelectedSkeletonOwnerEntity = nullptr;
	m_lastHierarchySelectedSkeletonBoneIndex = -1;
	m_skeletonEditorTool.SetEnabled(false);
	if (m_dx != nullptr)
	{
		m_dx->SetSkinWeightVisualizationEnabled(false);
		m_dx->SetSkinWeightVisualizationTarget(nullptr);
	}
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Editor::SetProcHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// WM_SETCURSOR 需要在窗口线程立即生效，避免异步队列导致光标状态滞后。
	if (uMsg == WM_INPUT || uMsg == WM_SETCURSOR)
		return;

	// IME 相关消息必须在窗口消息线程即时交给 Win32 后端处理。
	// IME 消息不需要在窗口线程特殊处理
	// 所有消息都进入队列，由渲染线程统一处理
	// 避免多线程并发访问 ImGui 上下文导致死锁

	EnqueueImGuiWindowMessage(hwnd, uMsg, wParam, lParam);
}

bool Editor::ApplyImGuiCursorForClientArea() const
{
	if (!m_hasImGuiCursorSnapshot.load(std::memory_order_relaxed))
		return false;

	ImGuiMouseCursor mouseCursor =
		static_cast<ImGuiMouseCursor>(m_lastImGuiMouseCursor.load(std::memory_order_relaxed));

	// None 场景下回退为箭头，避免出现不可见光标导致“假卡死”错觉。
	if (mouseCursor == ImGuiMouseCursor_None)
		mouseCursor = ImGuiMouseCursor_Arrow;

	HCURSOR cursor;
	switch (mouseCursor)
	{
	case ImGuiMouseCursor_TextInput:
		cursor = LoadCursor(NULL, IDC_IBEAM);
	case ImGuiMouseCursor_ResizeAll:
		cursor = LoadCursor(NULL, IDC_SIZEALL);
	case ImGuiMouseCursor_ResizeEW:
		cursor = LoadCursor(NULL, IDC_SIZEWE);
	case ImGuiMouseCursor_ResizeNS:
		cursor = LoadCursor(NULL, IDC_SIZENS);
	case ImGuiMouseCursor_ResizeNESW:
		cursor = LoadCursor(NULL, IDC_SIZENESW);
	case ImGuiMouseCursor_ResizeNWSE:
		cursor = LoadCursor(NULL, IDC_SIZENWSE);
	case ImGuiMouseCursor_Hand:
		cursor = LoadCursor(NULL, IDC_HAND);
	case ImGuiMouseCursor_Arrow:
	default:
		cursor = LoadCursor(NULL, IDC_ARROW);
	}
	SetCursor(cursor);
	return true;
}

void Editor::SetFont()
{
	ImGuiIO& io = ImGui::GetIO();
	fonts[L"STXIHEI.ttf"] = ImGui::GetIO().Fonts->AddFontFromFileTTF("DATA\\Fonts\\STXIHEI.ttf", 18.0f, NULL, ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
	fonts[L"Roboto.ttf"] = ImGui::GetIO().Fonts->AddFontFromFileTTF("DATA\\Fonts\\Roboto.ttf", 16.0f);
	fonts[L"Cousine-Regular.ttf"] = ImGui::GetIO().Fonts->AddFontFromFileTTF("DATA\\Fonts\\Cousine-Regular.ttf", 16.0f);
	if (fonts[L"Cousine-Regular.ttf"] != nullptr)
	{
		ImFontConfig chineseMergeConfig;
		chineseMergeConfig.MergeMode = true;
		chineseMergeConfig.DstFont = fonts[L"Cousine-Regular.ttf"];
		chineseMergeConfig.GlyphOffset = ImVec2(0.0f, 0.0f);
		ImGui::GetIO().Fonts->AddFontFromFileTTF("DATA\\Fonts\\STXIHEI.TTF", 16.0f, &chineseMergeConfig, ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
	}
	io.FontDefault = fonts[L"STXIHEI.ttf"];

	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
	ImFontConfig icons_config;
	icons_config.MergeMode = true;
	icons_config.DstFont = io.FontDefault;
	icons_config.PixelSnapH = true;
	icons_config.GlyphOffset = ImVec2(0.f, 2.5f);
	io.Fonts->AddFontFromFileTTF((SString::WstringToUTF8(m_imguiAssetPath) + "\\" + FONT_ICON_FILE_NAME_FAS).c_str(), 16.0f, &icons_config, icons_ranges);
	icons = io.FontDefault;

	// 构建字体纹理 atlas (DX12 后端需要手动构建)
	io.Fonts->Build();
}
void Editor::UpdateImGuiDPIScale()
{
	const UINT dpiScale = EngineHelpers::GetDisplayDPI(m_hWnd) / 84;
	if ((dpiScale - m_DpiScale) == 0u)
		return;

	m_DpiScale = dpiScale > 0u ? dpiScale : 1u;

	ImGuiIO& io = ImGui::GetIO();
	io.FontGlobalScale = (float)m_DpiScale;

	ImGuiStyle& style = ImGui::GetStyle();
	style = m_imguiBaseStyle;
	style.ScaleAllSizes((float)m_DpiScale);
}

void Editor::NotifyDisplayResize(float width, float height)
{
	ImGuiIO& io = ImGui::GetIO();

	if (io.DisplaySize.x != width || io.DisplaySize.y != height)
	{
		io.DisplaySize = ImVec2(width, height);
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	}
}

float Editor::GetScaledWindowDown() const
{
	// 应用DPI缩放
	return WINDOW_DOWN * m_DpiScale;
}

void Editor::RefreshSkyTextureFiles()
{
	EditorAssetCache::RefreshSkyTexturesIfNeeded();
	m_skyTextureFiles.assign(EditorAssetCache::GetSkyTextures().begin(), EditorAssetCache::GetSkyTextures().end());

	if (m_selectedSkyTextureIndex < 0 || m_selectedSkyTextureIndex >= static_cast<int>(m_skyTextureFiles.size()))
		m_selectedSkyTextureIndex = m_skyTextureFiles.empty() ? 0 : 0;
}

void Editor::RegisterWindowVisibilitySettingsHandler()
{
	ImGuiContext* context = ImGui::GetCurrentContext();

	for (const ImGuiSettingsHandler& handler : context->SettingsHandlers)
	{
		if (handler.TypeName != nullptr && strcmp(handler.TypeName, kEditorWindowVisibilitySettingsTypeName) == 0)
			return;
	}

	ImGuiSettingsHandler handler = {};
	handler.TypeName = kEditorWindowVisibilitySettingsTypeName;
	handler.TypeHash = ImHashStr(kEditorWindowVisibilitySettingsTypeName);
	handler.UserData = this;
	handler.ReadOpenFn = WindowVisibilitySettingsReadOpen;
	handler.ReadLineFn = WindowVisibilitySettingsReadLine;
	handler.WriteAllFn = WindowVisibilitySettingsWriteAll;
	context->SettingsHandlers.push_back(handler);
}

void Editor::ApplyWindowVisibilityState()
{
	m_hierarchyWindow.NeedRender(m_showHierarchyWindow);
	m_inspectorWindow.NeedRender(m_showInspectorWindow);
	m_assetsWindow.NeedRender(m_showAssetsWindow);
	m_fileWindow.NeedRender(m_showFileWindow);
	m_scriptEditorWindow.NeedRender(m_showScriptEditorWindow);
	m_consoleWindow.NeedRender(m_showConsoleWindow);
	m_screenSettingsWindow.NeedRender(m_showScreenSettingsWindow);
	m_skeletonEditorTool.SetEnabled(m_showSkeletonToolsWindow);
}

void Editor::MarkWindowVisibilitySettingsDirty()
{
	ImGui::MarkIniSettingsDirty();
}

void Editor::ReportPendingScriptRuntimeErrors()
{
	if (m_scriptingSystem == nullptr)
		return;

	const ScriptingRuntimeStats& runtimeStats = m_scriptingSystem->GetRuntimeStats();
	if (runtimeStats.ErrorRevision == m_lastReportedScriptErrorRevision || runtimeStats.LastErrorMessage.empty())
		return;

	m_lastReportedScriptErrorRevision = runtimeStats.ErrorRevision;
	m_consoleWindow.AddErrorMessage(L"[Script] %s\n%s",
		runtimeStats.LastErrorScriptPath.c_str(),
		runtimeStats.LastErrorMessage.c_str());
}

void Editor::OpenCreateEntityWindow(CreateItem item, const std::wstring& defaultName, bool refreshSkyTextures)
{
	CreaItem = item;
	name = defaultName;
	transform = Transform{};
	switch (item)
	{
	case CreateItem::PlaneItem:
		m_createSceneType = SceneEntityType::Ground;
	case CreateItem::EmptyItem:
	case CreateItem::BillboardItem:
	case CreateItem::CameraItem:
	case CreateItem::LightItem:
		m_createLightType = CreateDirectionalLight;
	case CreateItem::SkeletonItem:
		m_createSceneType = SceneEntityType::Interactive;
	case CreateItem::SkyItem:
		m_createSceneType = SceneEntityType::Sky;
	case CreateItem::BoxItem:
	case CreateItem::SphereItem:
	case CreateItem::CapsuleItem:
	case CreateItem::UnknownItem:
	default:
		m_createSceneType = SceneEntityType::StaticScenery;
	}
	openCreateWindow = true;

	if (refreshSkyTextures)
	{
		RefreshSkyTextureFiles();
		m_selectedSkyTextureIndex = 0;
	}
}

void Editor::OpenQueuedCreateEntityWindow(std::uint32_t createKind, const std::wstring& defaultName, bool refreshSkyTextures, bool clearSelectionFirst)
{
	if (clearSelectionFirst && m_engine != nullptr)
	{
		WitchcraECS* ecs = m_engine->GetECS();
		if (ecs != nullptr)
			ecs->ClearHierarchySelection();
	}

	OpenCreateEntityWindow(static_cast<CreateItem>(createKind), defaultName, refreshSkyTextures);
}

bool Editor::RenderCreateObjectWindow()
{
	const bool needsTransform =
		CreaItem == CreateItem::BoxItem ||
		CreaItem == CreateItem::SphereItem ||
		CreaItem == CreateItem::CapsuleItem ||
		CreaItem == CreateItem::PlaneItem ||
		CreaItem == CreateItem::BillboardItem ||
		CreaItem == CreateItem::CameraItem ||
		CreaItem == CreateItem::LightItem ||
		CreaItem == CreateItem::SkeletonItem;

	if (!m_hierarchyWindow.CreateComponentWindow(
		&openCreateWindow,
		&name,
		needsTransform ? &transform : nullptr,
		CreaItem != CreateItem::BillboardItem,
		CreaItem == CreateItem::BillboardItem ? "尺寸" : "缩放",
		nullptr,
		CreaItem == CreateItem::LightItem ? &m_createLightType : nullptr,
		&m_createSceneType,
		nullptr))
	{
		return false;
	}

	switch (CreaItem)
	{
	case CreateItem::EmptyItem:
		m_engine->AddObject(name, nullptr, CreateItem::EmptyItem, L"autoMat", CreateDirectionalLight, m_createSceneType);
		m_pendingFocusCreatedEntityInHierarchy = true;
		return true;

	case CreateItem::SkeletonItem:
		m_engine->AddObject(name, &transform, CreateItem::SkeletonItem, L"autoMat", CreateDirectionalLight, m_createSceneType);
		m_pendingFocusCreatedEntityInHierarchy = true;
		return true;

	case CreateItem::BoxItem:
	case CreateItem::SphereItem:
	case CreateItem::CapsuleItem:
	case CreateItem::PlaneItem:
		m_engine->AddObject(name, &transform, CreaItem, L"autoMat", CreateDirectionalLight, m_createSceneType);
		m_pendingFocusCreatedEntityInHierarchy = true;
		return true;

	case CreateItem::BillboardItem:
		m_engine->AddObject(name, &transform, CreateItem::BillboardItem, L"autoMat", CreateDirectionalLight, m_createSceneType);
		m_pendingFocusCreatedEntityInHierarchy = true;
		return true;

	case CreateItem::CameraItem:
		m_engine->AddObject(name, &transform, CreateItem::CameraItem, L"autoMat", CreateDirectionalLight, m_createSceneType);
		m_pendingFocusCreatedEntityInHierarchy = true;
		return true;

	case CreateItem::LightItem:
		m_engine->AddObject(name, &transform, CreateItem::LightItem, L"autoMat", static_cast<CreateLightType>(m_createLightType), m_createSceneType);
		m_pendingFocusCreatedEntityInHierarchy = true;
		return true;

	default:
		return false;
	}
}

bool Editor::RenderCreateSkyWindow()
{
	bool createEntity = false;

	if (ImGui::Begin("创建天空", &openCreateWindow, ImGuiWindowFlags_NoDocking))
	{
		std::string tmp = SString::WstringToUTF8(name);
		ImGui::Text("名称：");
		ImGui::SameLine();
		if (ImGui::InputText("##SkyName", &tmp))
			name = SString::UTF8ToWstring(tmp);

		if (!m_createSkyErrorMessage.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", SString::WstringToUTF8(m_createSkyErrorMessage).c_str());

		if (ImGui::Button("刷新天空贴图"))
		{
			EditorAssetCache::MarkSkyTexturesDirty();
			RefreshSkyTextureFiles();
		}

		ImGui::Text("天空贴图：");
		if (m_skyTextureFiles.empty())
		{
			ImGui::TextDisabled("未找到 DATA/HDRIs 下的贴图文件。");
		}
		else
		{
			const std::wstring selectedPath = m_skyTextureFiles[m_selectedSkyTextureIndex];
			const std::string preview = SString::WstringToUTF8(std::filesystem::path(selectedPath).filename().wstring());
			if (ImGui::BeginCombo("##SkyTexture", preview.c_str()))
			{
				for (int i = 0; i < static_cast<int>(m_skyTextureFiles.size()); ++i)
				{
					const bool isSelected = (m_selectedSkyTextureIndex == i);
					const std::string itemLabel = SString::WstringToUTF8(std::filesystem::path(m_skyTextureFiles[i]).filename().wstring());
					if (ImGui::Selectable(itemLabel.c_str(), isSelected))
						m_selectedSkyTextureIndex = i;

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		if (ImGui::Button("确定"))
		{
			name = SString::UTF8ToWstring(tmp);
			if (name.empty())
			{
				m_createSkyErrorMessage = L"名称不能为空。";
				return false;
			}

			if (!m_engine->GetECS()->IsEntityNameAvailable(name, nullptr))
			{
				m_createSkyErrorMessage = L"名称不得与现有同级项目重名。";
				return false;
			}

			if (m_skyTextureFiles.empty())
			{
				m_createSkyErrorMessage = L"未找到可用的天空贴图。";
				return false;
			}

			m_createSkyErrorMessage.clear();
			createEntity = true;
			m_pendingFocusCreatedEntityInHierarchy = true;

			openCreateWindow = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("取消"))
		{
			m_createSkyErrorMessage.clear();
			openCreateWindow = false;
		}
	}
	ImGui::End();

	return createEntity;
}

void Editor::RenderProjectSettingsWindow()
{
	if (!m_openProjectSettings)
	{
		m_projectSceneTypeColorDraftInitialized = false;
		m_projectSceneTypeColorDraftDirty = false;
		return;
	}
	if (m_engine == nullptr)
		return;

	WitchcraECS* ecs = m_engine->GetECS();
	if (ecs == nullptr)
		return;

	if (!m_projectSceneTypeColorDraftInitialized)
	{
		for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
		{
			const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
			m_projectSceneTypeColorDraft[typeIndex] = ecs->GetEntitySceneTypeVertexColor(sceneType);
		}
		m_projectSceneTypeColorDraftInitialized = true;
		m_projectSceneTypeColorDraftDirty = false;
	}

	if (ImGui::Begin("项目设置", &m_openProjectSettings, ImGuiWindowFlags_NoDocking))
	{
		const bool playModeActive = m_engine != nullptr && m_engine->IsPlayModeActive();
		if (playModeActive)
			ImGui::TextDisabled("Play Mode 中项目设置可查看，但应用/重载场景已锁定。");
		if (ImGui::CollapsingHeader("实体描边颜色设置", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("修改不会立即生效，点击“应用并重载场景”后才会生效。");
			ImGui::Separator();

			for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
			{
				const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
				DirectX::XMFLOAT4 typeColor = m_projectSceneTypeColorDraft[typeIndex];
				float color[4] = { typeColor.x, typeColor.y, typeColor.z, typeColor.w };
				const std::string label = SString::WstringToUTF8(SceneEntityTypeToDisplayName(sceneType));
				if (ImGui::ColorEdit4(label.c_str(), color))
				{
					m_projectSceneTypeColorDraft[typeIndex] = DirectX::XMFLOAT4(color[0], color[1], color[2], color[3]);
					m_projectSceneTypeColorDraftDirty = true;
				}
			}

			if (ImGui::Button("恢复默认颜色（待应用）"))
			{
				m_projectSceneTypeColorDraft = WitchcraECS::BuildDefaultSceneEntityTypeColors();
				m_projectSceneTypeColorDraftDirty = true;
			}

			ImGui::SameLine();
			ImGui::BeginDisabled(!m_projectSceneTypeColorDraftDirty || playModeActive);
			if (ImGui::Button("应用并重载场景"))
			{
				// 重载放到 Update 阶段统一执行，避免在 Render 阶段重建场景导致设备异常。
				m_pendingSceneAction = PendingSceneAction_Reload;
			}
			ImGui::EndDisabled();
		}
	}
	ImGui::End();
}

void Editor::RenderBar()
{
	if (ImGui::BeginMainMenuBar())
	{
		mainMenuBarSize = ImGui::GetWindowSize();

		RenderFileMenuBar();
		RenderProjectMenuBar();
		RenderEditMenuBar();
		RenderAssetsMenuBar();
		RenderEntityMenuBar();
		RenderScriptMenuBar();
		RenderWindowMenuBar();
		RenderHelpMenuBar();

		ImGui::EndMainMenuBar();
	}
}

void Editor::SetDocking()
{
	const float scaledWindowDown = GetScaledWindowDown();
	if (opt_fullscreen)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + scaledWindowDown));
		ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - (scaledWindowDown + scaledWindowDown)));
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}

	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	if (!opt_padding)
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	ImGui::Begin("DockSpace", NULL, window_flags);

	if (!opt_padding)
		ImGui::PopStyleVar();

	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}

	ImGui::End();
}

void Editor::RenderDownBar()
{
	const float scaledWindowDown = GetScaledWindowDown();
	static ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::SetNextWindowPos(ImVec2(0.0f, (float)EngineHelpers::GetContextHeight(m_hWnd) - scaledWindowDown));
	ImGui::SetNextWindowSize(ImVec2((float)EngineHelpers::GetContextWidth(m_hWnd), scaledWindowDown));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

	ImGui::Begin("DownBar", NULL, window_flags);
	{
		ImGui::Text("当前场景：");
		ImGui::SameLine();
		ImGui::Text(SString::WstringToUTF8(m_projectSceneSystem->GetSceneNmae()).c_str());
		ImGui::SameLine();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}


void Editor::RenderUpBar()

{
	const float scaledWindowDown = GetScaledWindowDown();
	static ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::SetNextWindowPos(ImVec2(0.f, mainMenuBarSize.y));
	ImGui::SetNextWindowSize(ImVec2((float)EngineHelpers::GetContextWidth(m_hWnd), scaledWindowDown));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

	ImVec2 size = ImVec2(scaledWindowDown, scaledWindowDown);

	ImGui::Begin("UpBar", NULL, window_flags);
	{
		ImGui::PushFont(icons);
		ImGui::Button(ICON_FA_SAVE, size);
		ImGui::SameLine();
		ImGui::Button(ICON_FA_ARROW_CIRCLE_LEFT, size);
		ImGui::SameLine();
		ImGui::Button(ICON_FA_ARROW_CIRCLE_RIGHT, size);
		ImGui::SameLine();

		///////////////////////////////////////////////////////

		ImGui::SameLine();

		///////////////////////////////////////////////////////

		ImGui::SameLine();

		///////////////////////////////////////////////////////

		ImGui::SameLine();

		///////////////////////////////////////////////////////

		ImGui::SameLine();

		///////////////////////////////////////////////////////

		if (ImGui::BeginPopupContextItem())
		{
			for (auto font = fonts.begin(); font != fonts.end(); font++)
				ImGui::PushFont(font->second);
			ImGui::PushItemWidth(64.0f);
			ImGui::PopItemWidth();
			ImGui::PopFont();
			ImGui::EndPopup();
		}

		///////////////////////////////////////////////////////

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_VECTOR_SQUARE, size))
		{
			if (renderState == PrimitiveState::PrimitiveTriangle)
			{
				renderState = PrimitiveState::PrimitiveLine;
				m_dx->GetCurrFrameResourceCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
			}
			else if (renderState == PrimitiveState::PrimitiveLine)
			{
				renderState = PrimitiveState::PrimitivePoint;
				m_dx->GetCurrFrameResourceCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
			}
			else if (renderState == PrimitiveState::PrimitivePoint)
			{
				renderState = PrimitiveState::PrimitiveTriangle;
				m_dx->GetCurrFrameResourceCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			}
		}

		ImGui::SameLine();

		///////////////////////////////////////////////////////

		ImGui::SameLine();

		// Play Mode 会在进入时捕获场景快照，停止时回滚运行态 ECS 修改。
		bool playModeActive = m_engine != nullptr && m_engine->IsPlayModeActive();
		bool playModePaused = m_engine != nullptr && m_engine->IsPlayModePaused();
		const bool playModeWasActiveAtFrameStart = playModeActive;
		const ImVec2 playModeButtonSize(52.0f * m_DpiScale, 0.0f);
		if (playModeWasActiveAtFrameStart)
			ImGui::PushStyleColor(ImGuiCol_Button, myColor);
		if (ImGui::Button(playModeActive ? ICON_FA_STOP : ICON_FA_PLAY, playModeButtonSize))
		{
			if (m_engine != nullptr)
			{
				if (playModeActive)
				{
					m_engine->StopPlayMode();
					m_lastReportedScriptErrorRevision = 0;
					m_consoleWindow.AddInfoMessage(L"[Play Mode] 已退出，运行态场景已回滚。");
				}
				else if (m_engine->StartPlayMode())
				{
					m_lastReportedScriptErrorRevision = 0;
					m_consoleWindow.AddInfoMessage(L"[Play Mode] 已进入，脚本、动画事件和物理开始运行。");
					ReportPendingScriptRuntimeErrors();
				}
				else
				{
					m_consoleWindow.AddErrorMessage(L"[Play Mode] 进入失败，未能启动运行态；请检查场景快照或引擎日志。");
				}
			}
		}

		if (playModeWasActiveAtFrameStart)
			ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(playModeActive ? "停止 Play Mode 并回滚运行态场景" : "启动 Play Mode");

		ImGui::SameLine();
		ImGui::BeginDisabled(!playModeActive || !playModePaused);
		if (ImGui::Button("Apply Transform", ImVec2(96.0f * m_DpiScale, 0.0f)) && m_engine != nullptr)
		{
			if (m_engine->ApplyPlayModeRuntimeChanges())
			{
				playModeActive = false;
				playModePaused = false;
				m_lastReportedScriptErrorRevision = 0;
				m_consoleWindow.AddInfoMessage(L"[Play Mode] 暂停态运行时变更已应用：仅 Transform 已写回编辑场景，并已退出 Play Mode。");
			}
			else
			{
				m_consoleWindow.AddErrorMessage(L"[Play Mode] 应用运行时 Transform 失败；场景已保持原状态。");
			}
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("暂停后可将当前 Play runtime 的 Transform 应用回编辑场景并退出 Play Mode");
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!playModeActive);
		if (ImGui::Button(playModePaused ? ICON_FA_PLAY : ICON_FA_PAUSE, playModeButtonSize) && m_engine != nullptr)
		{
			m_engine->TogglePlayModePaused();
			m_consoleWindow.AddInfoMessage(playModePaused ? L"[Play Mode] 已继续运行。" : L"[Play Mode] 已暂停，脚本、动画事件和物理推进暂时冻结。");
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip(playModePaused ? "继续 Play Mode runtime" : "暂停 Play Mode runtime");
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!playModePaused || (m_engine != nullptr && m_engine->GetPlayModeTimeScale() <= 0.0f));
		if (ImGui::Button(ICON_FA_STEP_FORWARD, playModeButtonSize) && m_engine != nullptr)
		{
			if (m_engine->RequestPlayModeStepFrame())
				m_consoleWindow.AddInfoMessage(L"[Play Mode] 单帧步进已请求，将推进一帧脚本、动画事件和物理。");
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("仅在暂停且 TimeScale > 0 时可用：按当前 TimeScale 推进一帧 runtime");
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(!playModeActive);
		float playModeTimeScale = m_engine != nullptr ? m_engine->GetPlayModeTimeScale() : 1.0f;
		ImGui::SetNextItemWidth(92.0f * m_DpiScale);
		if (ImGui::DragFloat(ICON_FA_STOPWATCH "##PlayModeTimeScale", &playModeTimeScale, 0.05f, 0.0f, 8.0f, "%.2fx") && m_engine != nullptr)
			m_engine->SetPlayModeTimeScale(playModeTimeScale);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Play Mode runtime 时间倍率；0 会冻结 runtime，Stop 后恢复默认 1.0x");
		ImGui::EndDisabled();
		if (playModeActive)
		{
			ImGui::SameLine();
			ImGui::TextColored(
				playModePaused ? ImVec4(0.55f, 0.82f, 1.0f, 1.0f) : ImVec4(1.0f, 0.72f, 0.24f, 1.0f),
				playModePaused ? "PAUSED" : "PLAY");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Play Mode：脚本、动画事件和物理受 Pause / Step / TimeScale 控制；危险编辑入口已锁定，Stop 后恢复进入前场景快照。脚本错误会输出到控制台。");
			if (m_scriptingSystem != nullptr)
			{
				const ScriptingRuntimeStats& runtimeStats = m_scriptingSystem->GetRuntimeStats();
				ImGui::SameLine();
				ImGui::TextColored(runtimeStats.ErrorInstanceCount > 0 ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f) : ImVec4(0.72f, 0.86f, 1.0f, 1.0f),
					"脚本 %u/%u，错误 %u",
					runtimeStats.ActiveScriptEntryCount,
					runtimeStats.RuntimeInstanceCount,
					runtimeStats.ErrorInstanceCount);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("脚本运行错误会输出到控制台窗口。");
			}
			ReportPendingScriptRuntimeErrors();
		}


		///////////////////////////////////////////////////////

		ImGui::PopFont();
	}
	ImGui::End();

	ImGui::PopStyleVar(5);
}

void Editor::RenderToolBar()
{
	const float scaledWindowDown = GetScaledWindowDown();
	static ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::SetNextWindowSize(ImVec2((float)EngineHelpers::GetContextWidth(m_hWnd), scaledWindowDown));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::Begin("ToolBar", nullptr, window_flags);
	{
		ImGui::Text("变换工具：");
		ImGui::SameLine();

		const GizmoMode currentGizmoMode = m_transformGizmo.GetMode();
		const ImVec2 gizmoButtonSize(30.0f * m_DpiScale, 0.0f);
		auto drawGizmoModeButton = [&](const char* id, const char* icon, GizmoMode mode, const char* tooltip)
			{
				const bool selected = currentGizmoMode == mode;
				if (selected)
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

				ImGui::PushID(id);
				if (ImGui::Button(icon, gizmoButtonSize))
					m_transformGizmo.SetMode(mode);
				ImGui::PopID();

				if (selected)
					ImGui::PopStyleColor();

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tooltip != nullptr)
					ImGui::SetTooltip("%s", tooltip);
			};

		drawGizmoModeButton("GizmoNone", ICON_FA_MOUSE_POINTER, GizmoMode::None, "无 / 隐藏工具 (Ctrl+Q)");
		ImGui::SameLine();
		drawGizmoModeButton("GizmoTranslate", ICON_FA_ARROWS_ALT, GizmoMode::Translate, "移动 (Ctrl+W)");
		ImGui::SameLine();
		drawGizmoModeButton("GizmoRotate", ICON_FA_SYNC_ALT, GizmoMode::Rotate, "旋转 (Ctrl+E)");
		ImGui::SameLine();
		drawGizmoModeButton("GizmoScale", ICON_FA_EXPAND_ARROWS_ALT, GizmoMode::Scale, "缩放 (Ctrl+R)");

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		const bool skeletonToolSelected = m_showSkeletonToolsWindow;
		if (skeletonToolSelected)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

		if (ImGui::Button(ICON_FA_VECTOR_SQUARE, gizmoButtonSize))
		{
			m_showSkeletonToolsWindow = !m_showSkeletonToolsWindow;
			m_showSkinWeightVisualization = m_showSkeletonToolsWindow;
			m_skeletonEditorTool.SetEnabled(m_showSkeletonToolsWindow);
			MarkWindowVisibilitySettingsDirty();
		}

		if (skeletonToolSelected)
			ImGui::PopStyleColor();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", "骨骼/蒙皮工具");

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		ImGui::Text("相机移动速度：");
		ImGui::SameLine();
		float cameraSpeed = m_dx->GetCameraSpeed();
		ImGui::SetNextItemWidth(180.0f * m_DpiScale);
		ImGui::SliderFloat("##CameraSpeed", &cameraSpeed, 1, 60);
		m_dx->SetCameraSpeed(cameraSpeed);
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

void Editor::RayVector(float mouseX, float mouseY, DirectX::XMVECTOR& pickRayInWorldSpacePos, DirectX::XMVECTOR& pickRayInWorldSpaceDir)
{
	// 默认给一个安全值，异常情况下可直接返回而不产生未初始化向量。
	pickRayInWorldSpacePos = XMVectorZero();
	pickRayInWorldSpaceDir = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

	if (m_dx == nullptr)
		return;

	const float contextWidth = static_cast<float>(EngineHelpers::GetContextWidth(m_hWnd));
	const float contextHeight = static_cast<float>(EngineHelpers::GetContextHeight(m_hWnd));
	if (contextWidth <= 0.0f || contextHeight <= 0.0f)
		return;

	const DirectX::XMMATRIX projection = m_dx->GetProj();
	const DirectX::XMMATRIX view = m_dx->GetView();
	const float projX = XMVectorGetX(projection.r[0]);
	const float projY = XMVectorGetY(projection.r[1]);
	if (fabsf(projX) < 0.000001f || fabsf(projY) < 0.000001f)
		return;

	// 屏幕坐标 -> 观察空间方向。
	float rayViewX = (((2.0f * mouseX) / contextWidth) - 1.0f) / projX;
	float rayViewY = (-((2.0f * mouseY) / contextHeight) + 1.0f) / projY;

	DirectX::XMVECTOR pickRayInViewSpaceDir = XMVectorSet(rayViewX, rayViewY, 1.0f, 0.0f);
	DirectX::XMVECTOR pickRayInViewSpacePos = XMVectorZero();

	const DirectX::XMMATRIX inverseView = XMMatrixInverse(nullptr, view);
	// 观察空间射线 -> 世界空间射线。
	pickRayInWorldSpacePos = XMVector3TransformCoord(pickRayInViewSpacePos, inverseView);
	pickRayInWorldSpaceDir = XMVector3Normalize(XMVector3TransformNormal(pickRayInViewSpaceDir, inverseView));
}

bool Editor::PointInTriangle(DirectX::XMVECTOR& triV1, DirectX::XMVECTOR& triV2, DirectX::XMVECTOR& triV3, DirectX::XMVECTOR& point)
{
	XMVECTOR cp1 = XMVector3Cross((triV3 - triV2), (point - triV2));
	XMVECTOR cp2 = XMVector3Cross((triV3 - triV2), (triV1 - triV2));
	if (XMVectorGetX(XMVector3Dot(cp1, cp2)) >= 0)
	{
		cp1 = XMVector3Cross((triV3 - triV1), (point - triV1));
		cp2 = XMVector3Cross((triV3 - triV1), (triV2 - triV1));
		if (XMVectorGetX(XMVector3Dot(cp1, cp2)) >= 0)
		{
			cp1 = XMVector3Cross((triV2 - triV1), (point - triV1));
			cp2 = XMVector3Cross((triV2 - triV1), (triV3 - triV1));
			if (XMVectorGetX(XMVector3Dot(cp1, cp2)) >= 0)
			{
				return true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	return false;
}

float Editor::PickMesh(DirectX::XMVECTOR pickRayInWorldSpacePos, DirectX::XMVECTOR pickRayInWorldSpaceDir, const std::vector<Vertex>& vertPosArray, const std::vector<std::uint32_t>& indexPosArray, DirectX::XMMATRIX worldSpace)
{
	float nearestDistance = FLT_MAX;

	for (size_t i = 0; i < indexPosArray.size() / 3; ++i)
	{
		const XMFLOAT3 v0 = vertPosArray[indexPosArray[(i * 3) + 0]].Pos;
		const XMFLOAT3 v1 = vertPosArray[indexPosArray[(i * 3) + 1]].Pos;
		const XMFLOAT3 v2 = vertPosArray[indexPosArray[(i * 3) + 2]].Pos;

		const XMVECTOR triV0 = XMVector3TransformCoord(XMVectorSet(v0.x, v0.y, v0.z, 0.0f), worldSpace);
		const XMVECTOR triV1 = XMVector3TransformCoord(XMVectorSet(v1.x, v1.y, v1.z, 0.0f), worldSpace);
		const XMVECTOR triV2 = XMVector3TransformCoord(XMVectorSet(v2.x, v2.y, v2.z, 0.0f), worldSpace);

		// 使用 DirectX 内置的三角形求交，稳定性比手写平面求交更高。
		float distance = 0.0f;
		if (!TriangleTests::Intersects(pickRayInWorldSpacePos, pickRayInWorldSpaceDir, triV0, triV1, triV2, distance))
			continue;
		if (distance <= 0.0f || distance >= nearestDistance)
			continue;

		nearestDistance = distance;
	}

	return nearestDistance;
}

void Editor::RunRay(POINT mousePoint, bool additiveSelection)
{
	if (m_engine == nullptr || m_dx == nullptr)
		return;

	WitchcraECS* ecs = m_engine->GetECS();
	if (ecs == nullptr)
		return;

	DirectX::XMVECTOR pickRayWorldPos = DirectX::XMVectorZero();
	DirectX::XMVECTOR pickRayWorldDir = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	RayVector(static_cast<float>(mousePoint.x), static_cast<float>(mousePoint.y), pickRayWorldPos, pickRayWorldDir);

	// 在场景树里找“最近命中”的实体。
	float nearestHitDistance = FLT_MAX;
	SceneEntityBase* nearestHitEntity = nullptr;

	std::vector<SceneEntityBase*> pendingEntities = ecs->GetSceneRootEntities();
	while (!pendingEntities.empty())
	{
		SceneEntityBase* entity = pendingEntities.back();
		pendingEntities.pop_back();
		if (entity == nullptr)
			continue;

		const std::vector<SceneEntityBase*>& children = ecs->GetSceneChildren(entity);
		for (SceneEntityBase* child : children)
			pendingEntities.push_back(child);

		EntityRenderView renderView;
		if (!ecs->BuildEntityRenderView(entity, &renderView))
			continue;
		if (renderView.meshComponent == nullptr || !renderView.visible || renderView.isSkyEntity)
			continue;

		MeshComponent* meshComponent = renderView.meshComponent;
		const std::vector<Vertex>& vertices = meshComponent->GetVertices();
		const std::vector<std::uint32_t>& indices = meshComponent->GetIndices();
		if (vertices.empty() || indices.empty() || (indices.size() % 3) != 0)
			continue;

		DirectX::XMFLOAT4X4 entityWorldMatrix{};
		if (!ecs->GetEntityRenderMatrix(entity, &entityWorldMatrix))
			continue;

		const float hitDistance = PickMesh(
			pickRayWorldPos,
			pickRayWorldDir,
			vertices,
			indices,
			DirectX::XMLoadFloat4x4(&entityWorldMatrix));

		// 极近表面时给一个微小容差，减少浮点抖动导致的“穿透式选后面”。
		constexpr float kHitDistanceEpsilon = 0.0005f;
		if (hitDistance + kHitDistanceEpsilon >= nearestHitDistance)
			continue;

		nearestHitDistance = hitDistance;
		nearestHitEntity = entity;
	}

	if (nearestHitEntity != nullptr)
	{
		ecs->SelectEntityForHierarchy(nearestHitEntity, additiveSelection);
	}
	else if (!additiveSelection)
	{
		ecs->ClearHierarchySelection();
	}

	m_transformGizmo.UpdateSelection(ecs->GetSelectedEntity());
}

void Editor::SetStyle()
{
	ImGuiStyle* style = &ImGui::GetStyle();
	ImVec4* colors = style->Colors;

	BYTE offset = 0x10;

	ImVec4 text = ImVec4(1.000f, 1.000f, 1.000f, 1.000f); /* OK */
	ImVec4 textDisabled = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x80 - offset, 0x80 - offset, 0x80 - offset, 0xFF));
	ImVec4 windowBg = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x2E - offset, 0x2E - offset, 0x2E - offset, 0x00));
	ImVec4 childBg = ImVec4(0.280f, 0.280f, 0.280f, 0.000f); /* OK */
	ImVec4 popupBg = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x50 - offset, 0x50 - offset, 0x50 - offset, 0xFF));
	ImVec4 border = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x44 - offset, 0x44 - offset, 0x44 - offset, 0xFF));
	ImVec4 borderShadow = ImVec4(0.000f, 0.000f, 0.000f, 0.000f); /* OK */
	ImVec4 frameBg = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x46 - offset, 0x46 - offset, 0x46 - offset, 0xFF));
	ImVec4 frameBgHovered = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x42 - offset, 0x42 - offset, 0x42 - offset, 0xFF));
	ImVec4 titleBg = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x26 - offset, 0x26 - offset, 0x26 - offset, 0xFF));
	ImVec4 menuBarBg = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x32 - offset, 0x32 - offset, 0x32 - offset, 0xFF));
	ImVec4 scrollbarGrab = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x47 - offset, 0x47 - offset, 0x47 - offset, 0xFF));
	ImVec4 scrollbarGrabHovered = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x4D - offset, 0x4D - offset, 0x4D - offset, 0xFF));
	ImVec4 sliderGrab = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x64 - offset, 0x64 - offset, 0x64 - offset, 0xFF));
	ImVec4 headerHovered = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x78 - offset, 0x78 - offset, 0x78 - offset, 0xFF));
	ImVec4 tab = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x19 - offset, 0x19 - offset, 0x19 - offset, 0xFF));
	ImVec4 tabHovered = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x5A - offset, 0x5A - offset, 0x5A - offset, 0xFF));
	ImVec4 plotHistogram = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x95 - offset, 0x95 - offset, 0x95 - offset, 0xFF));

	colors[ImGuiCol_Text] = text;
	colors[ImGuiCol_TextDisabled] = textDisabled;
	colors[ImGuiCol_WindowBg] = windowBg;
	colors[ImGuiCol_ChildBg] = childBg;
	colors[ImGuiCol_PopupBg] = popupBg;
	colors[ImGuiCol_Border] = border;
	colors[ImGuiCol_BorderShadow] = borderShadow;
	colors[ImGuiCol_FrameBg] = frameBg;
	colors[ImGuiCol_FrameBgHovered] = frameBgHovered;
	colors[ImGuiCol_FrameBgActive] = ImVec4(colors[ImGuiCol_ChildBg].x, colors[ImGuiCol_ChildBg].y, colors[ImGuiCol_ChildBg].z, 1.000f);
	colors[ImGuiCol_TitleBg] = titleBg;
	colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_MenuBarBg] = menuBarBg;
	colors[ImGuiCol_ScrollbarBg] = colors[ImGuiCol_FrameBg];
	colors[ImGuiCol_ScrollbarGrab] = scrollbarGrab;
	colors[ImGuiCol_ScrollbarGrabHovered] = scrollbarGrabHovered;
	colors[ImGuiCol_ScrollbarGrabActive] = myColor;
	colors[ImGuiCol_CheckMark] = colors[ImGuiCol_Text];
	colors[ImGuiCol_SliderGrab] = sliderGrab;
	colors[ImGuiCol_SliderGrabActive] = myColor;
	colors[ImGuiCol_Button] = ImVec4(colors[ImGuiCol_Text].x, colors[ImGuiCol_Text].y, colors[ImGuiCol_Text].z, 0.000f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(colors[ImGuiCol_Text].x, colors[ImGuiCol_Text].y, colors[ImGuiCol_Text].z, 0.156f);
	colors[ImGuiCol_ButtonActive] = ImVec4(colors[ImGuiCol_Text].x, colors[ImGuiCol_Text].y, colors[ImGuiCol_Text].z, 0.391f);
	colors[ImGuiCol_Header] = colors[ImGuiCol_PopupBg];
	colors[ImGuiCol_HeaderHovered] = headerHovered;
	colors[ImGuiCol_HeaderActive] = colors[ImGuiCol_HeaderHovered];
	colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
	colors[ImGuiCol_SeparatorHovered] = colors[ImGuiCol_SliderGrab];
	colors[ImGuiCol_SeparatorActive] = myColor;
	colors[ImGuiCol_ResizeGrip] = ImVec4(colors[ImGuiCol_Text].x, colors[ImGuiCol_Text].y, colors[ImGuiCol_Text].z, 0.250f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(colors[ImGuiCol_Text].x, colors[ImGuiCol_Text].y, colors[ImGuiCol_Text].z, 0.670f);
	colors[ImGuiCol_ResizeGripActive] = myColor;
	colors[ImGuiCol_Tab] = tab;
	colors[ImGuiCol_TabHovered] = tabHovered;
	colors[ImGuiCol_TabActive] = colors[ImGuiCol_MenuBarBg];
	colors[ImGuiCol_TabUnfocused] = colors[ImGuiCol_Tab];
	colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_MenuBarBg];
	colors[ImGuiCol_DockingPreview] = ImVec4(myColor.x, myColor.y, myColor.z, 0.781f);
	colors[ImGuiCol_DockingEmptyBg] = colors[ImGuiCol_WindowBg];
	colors[ImGuiCol_PlotLines] = colors[ImGuiCol_HeaderHovered];
	colors[ImGuiCol_PlotLinesHovered] = myColor;
	colors[ImGuiCol_PlotHistogram] = plotHistogram;
	colors[ImGuiCol_PlotHistogramHovered] = myColor;
	colors[ImGuiCol_TextSelectedBg] = colors[ImGuiCol_ButtonHovered];
	colors[ImGuiCol_DragDropTarget] = myColor;
	colors[ImGuiCol_NavHighlight] = myColor;
	colors[ImGuiCol_NavWindowingHighlight] = myColor;
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(colors[ImGuiCol_BorderShadow].x, colors[ImGuiCol_BorderShadow].y, colors[ImGuiCol_BorderShadow].z, 0.586f);
	colors[ImGuiCol_ModalWindowDimBg] = colors[ImGuiCol_NavWindowingDimBg];

	style->ChildRounding = 4.0f;
	style->FrameBorderSize = 1.0f;
	style->FrameRounding = 2.0f;
	style->GrabMinSize = 7.0f;
	style->PopupRounding = 2.0f;
	style->ScrollbarRounding = 12.0f;
	style->ScrollbarSize = 13.0f;
	style->TabBorderSize = 1.0f;
	style->TabRounding = 0.0f;
	style->WindowRounding = 4.0f;
}

void Editor::RenderFileMenuBar()
{
	const bool playModeActive = m_engine != nullptr && m_engine->IsPlayModeActive();
	if (ImGui::BeginMenu("文件"))
	{
		if (playModeActive)
			ImGui::TextDisabled("Play Mode 中场景新建/打开/保存已锁定。");
		if (ImGui::BeginMenu("新建", !playModeActive))
		{
			if (ImGui::MenuItem("场景", "Ctrl+N"))
			{
				m_pendingSceneAction = PendingSceneAction_New;
				m_pendingSceneName = L"未命名场景";
			}
			ImGui::MenuItem("项目", "", false, false);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("打开", !playModeActive))
		{
			if (ImGui::MenuItem("场景", "Ctrl+O"))
				m_pendingSceneAction = PendingSceneAction_Open;
			if (ImGui::MenuItem("项目"))
			{
				m_projectSceneSystem->OpenProject();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("保存", !playModeActive))
		{
			if (ImGui::MenuItem("场景", "Ctrl+S"))
				m_pendingSceneAction = PendingSceneAction_Save;
			ImGui::MenuItem("项目", "", false, false);
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("退出", "Alt+F4"))
			PostMessage(m_hWnd, WM_CLOSE, 0, 0);
		ImGui::EndMenu();
	}
}

void Editor::RenderEditMenuBar()
{
	if (ImGui::BeginMenu("编辑"))
	{
		ImGui::MenuItem("撤消", "", false, false);
		ImGui::MenuItem("重做", "", false, false);
		ImGui::Separator();
		ImGui::MenuItem("项目", "", false, false);
		ImGui::MenuItem("场景", "", false, false);
		ImGui::Separator();
		ImGui::TextDisabled("暂未开放：编辑菜单能力仍在后续补齐。");
		ImGui::EndMenu();
	}
}

void Editor::RenderProjectMenuBar()
{
	if (ImGui::BeginMenu("项目"))
	{
		if (ImGui::MenuItem("项目设置"))
			m_openProjectSettings = true;

		ImGui::EndMenu();
	}
}

void Editor::RenderAssetsMenuBar()
{
	if (ImGui::BeginMenu("资源"))
	{
		if (ImGui::BeginMenu("创建"))
		{
			if (ImGui::MenuItem("文件夹"))
			{
				m_showAssetsWindow = true;
				m_assetsWindow.NeedRender(true);
				MarkWindowVisibilitySettingsDirty();
				m_assetsWindow.CreateFolderInCurrentDirectory();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Lua 脚本"))
			{
				m_showAssetsWindow = true;
				m_assetsWindow.NeedRender(true);
				MarkWindowVisibilitySettingsDirty();
				m_assetsWindow.CreateLuaScriptInCurrentDirectory();
			}

			if (ImGui::MenuItem("材质"))
			{
				m_showAssetsWindow = true;
				m_assetsWindow.NeedRender(true);
				MarkWindowVisibilitySettingsDirty();
				m_assetsWindow.RequestCreateMaterialDialog();
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("返回上一级", "Backspace"))
		{
			m_showAssetsWindow = true;
			m_assetsWindow.NeedRender(true);
			MarkWindowVisibilitySettingsDirty();
			m_assetsWindow.GoBackDir();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("刷新当前目录", "Ctrl+R"))
		{
			m_showAssetsWindow = true;
			m_assetsWindow.NeedRender(true);
			MarkWindowVisibilitySettingsDirty();
			m_assetsWindow.RefreshDir();
		}

		ImGui::Separator();

		const FILEs* selectedFile = m_assetsWindow.GetSelFile();
		const bool canRename = selectedFile != nullptr;
		const bool canRemove = selectedFile != nullptr;
		if (ImGui::MenuItem("重命名", "F2", false, canRename) && selectedFile != nullptr)
		{
			m_showAssetsWindow = true;
			m_assetsWindow.NeedRender(true);
			MarkWindowVisibilitySettingsDirty();
			m_assetsWindow.RequestRenameSelectedAsset();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("移除", "", false, canRemove) && selectedFile != nullptr)
		{
			m_showAssetsWindow = true;
			m_assetsWindow.NeedRender(true);
			MarkWindowVisibilitySettingsDirty();
			m_assetsWindow.RequestRemoveSelectedAsset();
		}
		ImGui::EndMenu();
	}
}

void Editor::RenderEntityMenuBar()
{
	if (ImGui::BeginMenu("实体"))
	{
		const bool playModeActive = m_engine != nullptr && m_engine->IsPlayModeActive();
		WitchcraECS* ecs = m_engine != nullptr ? m_engine->GetECS() : nullptr;
		SceneEntityBase* selectedEntity = ecs != nullptr ? ecs->GetSelectedEntity() : nullptr;
		const bool hasSelectedEntity = selectedEntity != nullptr;
		const bool canRenameEntity = hasSelectedEntity && !playModeActive && !ecs->IsEnvironmentEntity(selectedEntity);
		const bool canDuplicateEntity = hasSelectedEntity && !playModeActive && !ecs->IsEnvironmentEntity(selectedEntity);
		const bool canDeleteEntity = hasSelectedEntity
			&& !playModeActive
			&& !ecs->IsEnvironmentEntity(selectedEntity)
			&& !ecs->IsAmbientLightEntity(selectedEntity);

		if (playModeActive)
			ImGui::TextDisabled("Play Mode 中实体结构只读。");
		if (ImGui::BeginMenu("创建", !playModeActive))
		{
			if (ImGui::MenuItem("空的"))
				OpenCreateEntityWindow(CreateItem::EmptyItem, L"空的");
			if (ImGui::MenuItem("骨骼"))
				OpenCreateEntityWindow(CreateItem::SkeletonItem, L"骨骼");

			ImGui::Separator();

			if (ImGui::MenuItem("天空"))
				OpenCreateEntityWindow(CreateItem::SkyItem, L"天空", true);
			if (ImGui::MenuItem("盒子"))
				OpenCreateEntityWindow(CreateItem::BoxItem, L"盒子");
			if (ImGui::MenuItem("球体"))
				OpenCreateEntityWindow(CreateItem::SphereItem, L"球体");
			if (ImGui::MenuItem("胶囊"))
				OpenCreateEntityWindow(CreateItem::CapsuleItem, L"胶囊");
			if (ImGui::MenuItem("平面"))
				OpenCreateEntityWindow(CreateItem::PlaneItem, L"平面");
			if (ImGui::MenuItem("告示牌"))
				OpenCreateEntityWindow(CreateItem::BillboardItem, L"告示牌");

			ImGui::Separator();

			if (ImGui::MenuItem("相机"))
				OpenCreateEntityWindow(CreateItem::CameraItem, L"相机");
			if (ImGui::MenuItem("灯光"))
				OpenCreateEntityWindow(CreateItem::LightItem, L"灯光");

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("重命名", "F2", false, canRenameEntity) && canRenameEntity)
			m_hierarchyWindow.RequestRenameSelectedEntity();

		if (ImGui::MenuItem("重复", "Ctrl+D", false, canDuplicateEntity) && canDuplicateEntity)
		{
			if (!playModeActive && ecs->DuplicateSelectedEntity(m_dx, m_engine) != nullptr)
				m_hierarchyWindow.RequestFocusSelectedEntity();
		}

		if (ImGui::MenuItem("删除", "Delete", false, canDeleteEntity) && canDeleteEntity)
			m_hierarchyWindow.RequestDeleteSelectedEntity();

		ImGui::EndMenu();
	}
}

void Editor::RenderWindowMenuBar()
{
	if (ImGui::BeginMenu("窗口"))
	{
		if (ImGui::MenuItem("层次", nullptr, &m_showHierarchyWindow))
		{
			m_hierarchyWindow.NeedRender(m_showHierarchyWindow);
			MarkWindowVisibilitySettingsDirty();
		}
		if (ImGui::MenuItem("实体信息", nullptr, &m_showInspectorWindow))
		{
			m_inspectorWindow.NeedRender(m_showInspectorWindow);
			MarkWindowVisibilitySettingsDirty();
		}
		if (ImGui::MenuItem("资源", nullptr, &m_showAssetsWindow))
		{
			m_assetsWindow.NeedRender(m_showAssetsWindow);
			MarkWindowVisibilitySettingsDirty();
		}
		if (ImGui::MenuItem("文件信息", nullptr, &m_showFileWindow))
		{
			m_fileWindow.NeedRender(m_showFileWindow);
			MarkWindowVisibilitySettingsDirty();
		}
		if (ImGui::MenuItem("脚本编辑器", nullptr, &m_showScriptEditorWindow))
		{
			m_scriptEditorWindow.NeedRender(m_showScriptEditorWindow);
			MarkWindowVisibilitySettingsDirty();
		}
		if (ImGui::MenuItem("控制台", nullptr, &m_showConsoleWindow))
		{
			m_consoleWindow.NeedRender(m_showConsoleWindow);
			MarkWindowVisibilitySettingsDirty();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("画面设置", nullptr, &m_showScreenSettingsWindow))
		{
			m_screenSettingsWindow.NeedRender(m_showScreenSettingsWindow);
			MarkWindowVisibilitySettingsDirty();
		}
	if (ImGui::MenuItem("骨骼编辑器", nullptr, &m_showSkeletonToolsWindow))
	{
		MarkWindowVisibilitySettingsDirty();
	}
		ImGui::EndMenu();
	}
}

void Editor::RenderHelpMenuBar()
{
	if (ImGui::BeginMenu("帮助"))
	{
		if (ImGui::MenuItem("关于"))
		{
			m_aboutWindow.NeedRender(true);
		}
		ImGui::EndMenu();
	}
}

void Editor::RenderScriptMenuBar()
{
	if (ImGui::BeginMenu("脚本"))
	{
		ImGui::MenuItem("重新编译", "", false, false);
		ImGui::TextDisabled("暂时禁用：脚本桥（功能）尚未完成。");
		ImGui::EndMenu();
	}
}

bool Editor::ApplySkeletonJointsToData(const std::vector<SkeletonJoint>& joints, Witchcraft::Animation::SkeletonData* skeletonData)
{
	if (skeletonData == nullptr)
		return false;

	Witchcraft::Animation::SkeletonTopology topology;
	topology.Bones.resize(joints.size());
	std::vector<std::wstring> boneNames;
	std::vector<Witchcraft::Animation::BoneLocalPose> localPose;
	std::vector<DirectX::XMFLOAT4X4> localMatrixPose;
	std::vector<DirectX::XMFLOAT4X4> globalPose;
	boneNames.reserve(joints.size());
	localPose.resize(joints.size());
	localMatrixPose.resize(joints.size());
	globalPose.resize(joints.size());

	topology.RootBoneIndex = -1;
	for (std::uint32_t jointIndex = 0; jointIndex < static_cast<std::uint32_t>(joints.size()); ++jointIndex)
	{
		const SkeletonJoint& joint = joints[jointIndex];
		Witchcraft::Animation::SkeletonBone& bone = topology.Bones[jointIndex];

		bone.Name = joint.Name.empty()
			? (L"骨骼" + std::to_wstring(jointIndex))
			: joint.Name;
		bone.ParentIndex = joint.ParentIndex;
		if (bone.ParentIndex < 0 && topology.RootBoneIndex < 0)
			topology.RootBoneIndex = static_cast<std::int32_t>(jointIndex);

		Witchcraft::Animation::BoneLocalPose pose;
		pose.Rotation = joint.Rotation;
		pose.Scale = joint.Scale;
		pose.Translation = joint.Position;
		if (joint.ParentIndex >= 0 && joint.ParentIndex < static_cast<int>(joints.size()))
		{
			const DirectX::XMFLOAT3& parentPosition = joints[static_cast<size_t>(joint.ParentIndex)].Position;
			pose.Translation = DirectX::XMFLOAT3(
				joint.Position.x - parentPosition.x,
				joint.Position.y - parentPosition.y,
				joint.Position.z - parentPosition.z);
		}
		pose.Matrix = Witchcraft::Animation::ComposeBoneLocalPoseMatrix(pose);
		pose.HasMatrix = true;

		bone.BindLocalPose = pose;
		bone.BindGlobalMatrix = Witchcraft::Animation::MakeIdentityFloat4x4();
		bone.InverseBindPose = Witchcraft::Animation::MakeIdentityFloat4x4();

		boneNames.push_back(bone.Name);
		localPose[jointIndex] = pose;
		localMatrixPose[jointIndex] = pose.Matrix;
	}

	for (std::uint32_t jointIndex = 0; jointIndex < static_cast<std::uint32_t>(joints.size()); ++jointIndex)
	{
		const int parentIndex = topology.Bones[jointIndex].ParentIndex;
		const DirectX::XMMATRIX localMatrix = DirectX::XMLoadFloat4x4(&localMatrixPose[jointIndex]);
		DirectX::XMMATRIX globalMatrix = localMatrix;
		if (parentIndex >= 0 && parentIndex < static_cast<int>(joints.size()))
		{
			const DirectX::XMMATRIX parentGlobalMatrix = DirectX::XMLoadFloat4x4(&globalPose[static_cast<size_t>(parentIndex)]);
			globalMatrix = DirectX::XMMatrixMultiply(localMatrix, parentGlobalMatrix);
		}

		DirectX::XMStoreFloat4x4(&globalPose[jointIndex], globalMatrix);
		topology.Bones[jointIndex].BindGlobalMatrix = globalPose[jointIndex];

		DirectX::XMFLOAT4X4 inverseBindPose{};
		DirectX::XMStoreFloat4x4(&inverseBindPose, DirectX::XMMatrixInverse(nullptr, globalMatrix));
		topology.Bones[jointIndex].InverseBindPose = inverseBindPose;
	}

	topology.RebuildNameToIndexMap();
	skeletonData->Topology = topology;
	skeletonData->BoneNames = boneNames;
	skeletonData->LocalPose = localPose;
	skeletonData->LocalMatrixPose = localMatrixPose;
	skeletonData->GlobalPose = globalPose;
	skeletonData->Dirty = false;
	return true;
}

void Editor::ImgSrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
{
	UINT idx = 0; // 消耗到剩余几个
	if (!this_Editor->m_imguiDescPool.Allocate(idx)) {
		IM_ASSERT(false && "ImGui SRV描述符池已耗尽！");
		return;
	}
	// 计算 CPU 和 GPU 句柄
	*out_cpu_desc_handle = this_Editor->mGUISrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	out_cpu_desc_handle->ptr += idx * this_Editor->m_dx->GetCbvSrvUavDescriptorSize();
	*out_gpu_desc_handle = this_Editor->mGUISrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	out_gpu_desc_handle->ptr += idx * this_Editor->m_dx->GetCbvSrvUavDescriptorSize();
}

void Editor::ImgSrvDescriptorFree(ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle)
{
	// 通过句柄计算索引
	uintptr_t cpuHeapOffset = cpu_desc_handle.ptr - this_Editor->mGUISrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
	UINT cpuIdx = static_cast<UINT>(cpuHeapOffset / this_Editor->m_dx->GetCbvSrvUavDescriptorSize());
	uintptr_t gpuHeapOffset = gpu_desc_handle.ptr - this_Editor->mGUISrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr;
	UINT gpuIdx = static_cast<UINT>(gpuHeapOffset / this_Editor->m_dx->GetCbvSrvUavDescriptorSize());
	// 比对是否出现偏差
	IM_ASSERT(cpuIdx == gpuIdx);
	this_Editor->m_imguiDescPool.Free(cpuIdx);
}
