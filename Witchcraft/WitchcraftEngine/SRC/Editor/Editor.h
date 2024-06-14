#pragma once

#include <chrono>

#include "D3DWindow/D3DWindow.h"
#include "Window/ScreenSettingsWindow.h"
#include "Window/FileWindow.h"
#include "Window/AssetsWindow.h"
#include "Window/InspectorWindow.h"
#include "Window/HierarchyWindow.h"
#include "Window/ConsoleWindow.h"
#include "Window/AboutWindow.h"
#include "SYSTEM/ProjectSceneSystem.h"
#include "SYSTEM/ScriptingSystem.h"
#include "HELPERS/Helpers.h"
#include "ServicesContainer/Component/MeshComponent.h"

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
	UnknownItem =0,
	EmptyItem,
	SkyItem,
	BoxItem,
	SphereItem,
	CapsuleItem,
	PlaneItem,
	CameraItem
};

class Engine;
class WitchcraECS;

class Editor
{
public:
	bool Init(HWND hWnd, Engine* engine, D3DWindow* dx, std::wstring path);
	void Update();
	void Render();
	void Shutdown();

public:
	void SetProcHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	//// - Ray System //////////////////////////////////////
	void RayVector(float mouseX, float mouseY, DirectX::XMVECTOR& pickRayInWorldSpacePos, DirectX::XMVECTOR& pickRayInWorldSpaceDir);
	bool PointInTriangle(DirectX::XMVECTOR& triV1, DirectX::XMVECTOR& triV2, DirectX::XMVECTOR& triV3, DirectX::XMVECTOR& point);
	float PickMesh(DirectX::XMVECTOR pickRayInWorldSpacePos, DirectX::XMVECTOR pickRayInWorldSpaceDir, const std::vector<Vertex>& vertPosArray, const std::vector<UINT>& indexPosArray, DirectX::XMMATRIX worldSpace);
	void RunRay(POINT mousePoint);

private:
	void SetStyle();
	void SetFont();
	void RenderBar();
	void RenderDownBar();
	void RenderUpBar();
	void RenderToolBar();

private:
	void SetDocking();
	ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
	bool opt_fullscreen = true;
	bool opt_padding = false;
	BYTE renderState = PrimitiveState::PrimitiveTriangle;

public:
	ImFont* fonts[2] = { nullptr };
	ImFont* icons = nullptr;

public:
	ImVec4 myColor = ImVec4(ImGui::ColorConvertU32ToFloat4(IM_COL32(0xE2, 0x52, 0x52, 0xFF)));

private:
	void RenderFileMenuBar();
	void RenderEditMenuBar();
	void RenderAssetsMenuBar();
	void RenderEntityMenuBar();
	void RenderWindowMenuBar();
	void RenderHelpMenuBar();
	void RenderScriptMenuBar();

private:

	HWND m_hWnd = nullptr;

	POINT point = { 0, 0 };

	ComPtr<ID3D12RootSignature> mGUIRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> RtvHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> DsvHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> mGUISrvDescriptorHeap = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE editerCPUTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE editerGPUTexDescriptor;

	Engine* m_engine = nullptr;
	D3DWindow* m_dx = nullptr;
	ServicesContainer* m_ComponentServices = nullptr;
	WitchcraECS* ecs = nullptr;
	ProjectSceneSystem* m_projectSceneSystem = nullptr;
	ScriptingSystem* m_scriptingSystem = nullptr;
	PhysicsSystem* m_physicsSystem = nullptr;
	ScreenSettingsWindow m_screenSettingsWindow;
	AssetsWindow m_assetsWindow;
	HierarchyWindow m_hierarchyWindow;
	InspectorWindow m_inspectorWindow;
	FileWindow m_fileWindow;
	ConsoleWindow m_consoleWindow;
	AboutWindow m_aboutWindow;

	bool openCreateWindow = false;
	std::wstring name = L"";
	Transform transform;
	CreateItem CreaItem = CreateItem::UnknownItem;
};
