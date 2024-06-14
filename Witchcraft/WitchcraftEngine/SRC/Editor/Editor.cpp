#include "Editor.h"

#include "ServicesContainer/COMPONENT/GeneralComponent.h"
#include "ServicesContainer/COMPONENT/ScriptingComponent.h"
#include "String/SStringUtils.h"
#include "Engine/Engine.h"
#include "Engine/EngineUtils.h"
#include "ModelAnalysis/AssimpLoader.h"

#include <wincodec.h>

///////////////////////////////////////////////////////////////

#define MAX_NUM_IMGUI_IMAGES_PER_FRAME 128

static ImVec2 mainMenuBarSize = ImVec2(NULL, NULL);

bool Editor::Init(HWND hWnd, Engine* engine, D3DWindow* dx, std::wstring path)
{
	m_hWnd = hWnd;
	m_engine = engine;
	m_dx = dx;
	m_ComponentServices = engine->GetEntity();
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

		auto staticSamplers = dx->GetStaticSamplers();

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

		ThrowIfFailed(dx->GetDevice()->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mGUIRootSignature.GetAddressOf()))); 
	}
	
	//
	//创建UI的SRV堆。存储每个UI窗口（不包含资源窗口）都要用到的图像资源
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = m_dx->GetSwapChainBufferCount() * MAX_NUM_IMGUI_IMAGES_PER_FRAME + 2; //贴图资源数量（大于实际数量没关系小了不行）
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_dx->GetDevice()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mGUISrvDescriptorHeap)));

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	if (!ImGui_ImplWin32_Init(m_hWnd)) return false;
	if (!ImGui_ImplDX12_Init(m_dx->GetDevice(), m_dx->GetSwapChainBufferCount(),
		m_dx->GetBackBufferFormat(), mGUISrvDescriptorHeap.Get(),
		mGUISrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mGUISrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()))
		return false;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigViewportsNoAutoMerge = true;
	SetStyle();
	SetFont();

	{
		io.Fonts->AddFontDefault();
		static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
		ImFontConfig icons_config;
		icons_config.MergeMode = true;
		icons_config.PixelSnapH = true;
		icons_config.GlyphOffset = ImVec2(0.f, 2.5f);
		icons = io.Fonts->AddFontFromFileTTF((SString::WstringToUTF8(path) + "\\" + FONT_ICON_FILE_NAME_FAS).c_str(), 16.0f, &icons_config, icons_ranges);
	}

	editerCPUTexDescriptor = mGUISrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	editerGPUTexDescriptor = mGUISrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	
	//下一个描述符
	editerCPUTexDescriptor.Offset(1, dx->GetCbvSrvUavDescriptorSize());
	editerGPUTexDescriptor.Offset(1, dx->GetCbvSrvUavDescriptorSize());

	AssimpLoader assimpLoader;
	assimpLoader.Create(m_dx);
	m_consoleWindow.Init();
	m_screenSettingsWindow.Init(m_dx, mGUISrvDescriptorHeap.Get());
	m_assetsWindow.Init(m_dx, this, mGUISrvDescriptorHeap.Get());
	m_fileWindow.Init(m_dx, &m_assetsWindow, mGUISrvDescriptorHeap.Get());
	m_aboutWindow.Init(m_dx, mGUISrvDescriptorHeap.Get());
	m_hierarchyWindow.Init(&m_consoleWindow, &assimpLoader, m_ComponentServices);
	m_inspectorWindow.Init(m_ComponentServices, m_dx, &m_assetsWindow, m_physicsSystem);

	return true;
}

void Editor::Update()
{
	// 更新用户输入
	{
		KeyboardClass* keyboard = m_engine->GetKeyboard();
		while (!keyboard->CharBufferIsEmpty())
		{
			BYTE ch = keyboard->ReadChar();
		}

		while (!keyboard->KeyBufferIsEmpty())
		{
			KeyboardEvent kbe = keyboard->ReadKey();
			BYTE keycode = kbe.GetKeyCode();
			if (kbe.IsPress())
			{
			}

		}

		{
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
				//this->gfx.Camera3D.AdjustPosition(0.0f, Camera3DSpeed * dt, 0.0f);
			}

			if (MoveCamera && (MovementDirection != MOVE_NOT_SPECIFIDE))
			{
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
				m_dx->MoveCamera(ImGui::GetIO().DeltaTime, distance);
				MoveCamera = false;
			}
		}

		MouseClass* mouse = m_engine->GetMouse();
		while (!mouse->EventBufferIsEmpty())
		{
			MouseEvent me = mouse->ReadEvent();

			if (mouse->IsLeftDown())
			{
				point = { me.GetPosX(), me.GetPosY() };

				if (me.GetType() == MouseEvent::EventType::Move)
				{
					RunRay(point);
				}
			}

			if (mouse->IsRightDown())
			{
				POINT pt = point;
				point = { me.GetPosX(), me.GetPosY() };

				if (me.GetType() == MouseEvent::EventType::Move)
				{
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
						m_dx->MoveCamera(ImGui::GetIO().DeltaTime, DirectX::XMFLOAT3((pt.x - point.x), -(pt.y - point.y), 0.0f));
				}
			}

			if (mouse->IsMiddleDown())
			{
				POINT pt = point;
				point = { me.GetPosX(), me.GetPosY() };
				DirectX::XMFLOAT2 angle((pt.x - point.x), (pt.y - point.y));
				if (me.GetType() == MouseEvent::EventType::Move)
					m_dx->RotateCamera(ImGui::GetIO().DeltaTime, angle);
			}

			if (me.GetType() == MouseEvent::EventType::WheelUp)
			{
				m_dx->MoveCamera(ImGui::GetIO().DeltaTime, DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));
			}
			else if (me.GetType() == MouseEvent::EventType::WheelDown)
			{
				m_dx->MoveCamera(ImGui::GetIO().DeltaTime, DirectX::XMFLOAT3(0.0f, 0.0f, -1.0f));
			}

		}
	}
}

void Editor::Render()
{
	//bool mastbClose = false;
	//if (m_dx->IsCommandListClose())
	//{
	//	m_dx->ResetCommandList();
	//	mastbClose = true;
	//}

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_dx->GetRtv();
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dx->GetDsv();

	// 指定我们要渲染到的缓冲区。
	m_dx->GetCurrFrameResourceCommandList()->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	// 切换CBV堆
	m_dx->GetCurrFrameResourceCommandList()->SetDescriptorHeaps(1, mGUISrvDescriptorHeap.GetAddressOf());
	m_dx->GetCurrFrameResourceCommandList()->SetGraphicsRootSignature(mGUIRootSignature.Get());
	editerGPUTexDescriptor.Offset(0, m_dx->GetCbvSrvUavDescriptorSize());
	//m_dx->GetThreadCommandList(threadIndex)->SetGraphicsRootDescriptorTable(0, editerTexDescriptor);

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)EngineHelpers::GetContextWidth(m_hWnd), (float)EngineHelpers::GetContextHeight(m_hWnd));

	ImGuiStyle* style = &ImGui::GetStyle();
	ImVec4* colors = style->Colors;
	BYTE offset = 0x10;
	ImVec4 windowBg = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x2E - offset, 0x2E - offset, 0x2E - offset, 0x00));
	colors[ImGuiCol_WindowBg] = windowBg;

	{
		SetDocking();

		windowBg = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x2E - offset, 0x2E - offset, 0x2E - offset, 0xFF));
		colors[ImGuiCol_WindowBg] = windowBg;

		//openCreateWindow = false;
		//name = L"";

		RenderBar();
		RenderDownBar();
		RenderUpBar();
		m_assetsWindow.Render();
		m_screenSettingsWindow.Render();
		m_hierarchyWindow.Render();
		m_inspectorWindow.Render();
		m_fileWindow.Render();
		m_consoleWindow.Render();
		m_aboutWindow.Render();
		RenderToolBar();

		if (openCreateWindow)
		{
			switch (CreaItem)
			{
			case CreateItem::EmptyItem:
			{
				if (m_hierarchyWindow.CreateComponentWindow(&openCreateWindow, &name))
				{
					//m_ComponentServices->CreateEmptyEntity(entity, name);
					//m_ComponentServices->selected = entity;
				}
			}
			break;
			case CreateItem::SkyItem:
			{
				if (m_hierarchyWindow.CreateComponentWindow(&openCreateWindow, &name))
				{
					//m_ComponentServices->CreateSkyEntity(entity, name);
					//m_ComponentServices->selected = entity;
				}
			}
			break;
			case CreateItem::BoxItem:
			{
				if (m_hierarchyWindow.CreateComponentWindow(&openCreateWindow, &name, &transform))
				{
					//m_ComponentServices->CreateCubeEntity(entity, name, &transform);
					//m_ComponentServices->selected = entity;
				}
			}
			break;
			case CreateItem::SphereItem:
			{
				if (m_hierarchyWindow.CreateComponentWindow(&openCreateWindow, &name, &transform))
				{
					//m_ComponentServices->CreateSphereEntity(entity, name, &transform);
					//m_ComponentServices->selected = entity;
				}
			}
			break;
			case CreateItem::CapsuleItem:
			{
				if (m_hierarchyWindow.CreateComponentWindow(&openCreateWindow, &name, &transform))
				{
					//m_ComponentServices->CreateCapsuleEntity(entity, name, &transform);
					//m_ComponentServices->selected = entity;
				}
			}
			break;
			case CreateItem::PlaneItem:
			{
				if (m_hierarchyWindow.CreateComponentWindow(&openCreateWindow, &name, &transform))
				{
					//m_ComponentServices->CreatePlaneEntity(entity, name, &transform);
					//m_ComponentServices->selected = entity;
				}
			}
			break;
			case CreateItem::CameraItem:
			{
				if (m_hierarchyWindow.CreateComponentWindow(&openCreateWindow, &name, &transform))
				{
					//m_ComponentServices->CreateCameraEntity(entity, name, &transform);
					//m_ComponentServices->selected = entity;
				}
			}
			break;
			default:
				break;
			}
		}
	}

	ImGui::Render();

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_dx->GetCurrFrameResourceCommandList());

	// 更新和渲染附加平台窗口
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

void Editor::Shutdown()
{
	m_aboutWindow.Shutdown();
	m_assetsWindow.Shutdown();

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void Editor::SetProcHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam);
}

void Editor::SetFont()
{
	fonts[0] = ImGui::GetIO().Fonts->AddFontFromFileTTF(u8"DATA\\Fonts\\STXIHEI.ttf", 18.0f, NULL, ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
	fonts[1] = ImGui::GetIO().Fonts->AddFontFromFileTTF(u8"DATA\\Fonts\\Roboto.ttf", 16.0f);
}

void Editor::RenderBar()
{
	if (ImGui::BeginMainMenuBar())
	{
		mainMenuBarSize = ImGui::GetWindowSize();

		RenderFileMenuBar();
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
	if (opt_fullscreen)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + WINDOW_DOWN));
		ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - (WINDOW_DOWN + WINDOW_DOWN)));
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

	ImGui::Begin(u8"DockSpace", NULL, window_flags);

	if (!opt_padding)
		ImGui::PopStyleVar();

	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID(u8"MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}

	ImGui::End();
}

void Editor::RenderDownBar()
{
	static ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::SetNextWindowPos(ImVec2(0.0f, (float)EngineHelpers::GetContextHeight(m_hWnd) - WINDOW_DOWN));
	ImGui::SetNextWindowSize(ImVec2((float)EngineHelpers::GetContextWidth(m_hWnd), WINDOW_DOWN));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

	ImGui::Begin(u8"DownBar", NULL, window_flags);
	{
		ImGui::Text(u8"当前场景：");
		ImGui::SameLine();
		ImGui::Text(SString::WstringToUTF8(m_projectSceneSystem->GetSceneNmae()).c_str());
		ImGui::SameLine();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

void Editor::RenderUpBar()
{
	static ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::SetNextWindowPos(ImVec2(0.f, mainMenuBarSize.y));
	ImGui::SetNextWindowSize(ImVec2((float)EngineHelpers::GetContextWidth(m_hWnd), WINDOW_DOWN));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

	static ImVec2 size = ImVec2(WINDOW_DOWN, WINDOW_DOWN);

	ImGui::Begin(u8"UpBar", NULL, window_flags);
	{
		ImGui::PushFont(icons);
		ImGui::Button(ICON_FA_SAVE, size);
		ImGui::SameLine();
		ImGui::Button(ICON_FA_ARROW_CIRCLE_LEFT, size);
		ImGui::SameLine();
		ImGui::Button(ICON_FA_ARROW_CIRCLE_RIGHT, size);
		ImGui::SameLine();

		///////////////////////////////////////////////////////

		//if (m_viewportWindow->GetMode() == ImGuizmo::LOCAL)
		//{
		//	if (ImGui::Button(ICON_FA_CUBE, size))
		//		m_viewportWindow->SetMode(ImGuizmo::WORLD);
		//}
		//else
		//{
		//	if (ImGui::Button(ICON_FA_GLOBE, size))
		//		m_viewportWindow->SetMode(ImGuizmo::LOCAL);
		//}

		ImGui::SameLine();

		///////////////////////////////////////////////////////

		//if (m_viewportWindow->GetOperation() == ImGuizmo::TRANSLATE)
		//{
		//	ImGui::PushStyleColor(ImGuiCol_Button, myColor);
		//	ImGui::Button(ICON_FA_ARROWS_ALT, size);
		//	ImGui::PopStyleColor();
		//}
		//else
		//{
		//	if (ImGui::Button(ICON_FA_ARROWS_ALT, size))
		//		m_viewportWindow->SetOperation(ImGuizmo::TRANSLATE);
		//}

		ImGui::SameLine();

		///////////////////////////////////////////////////////

		//if (m_viewportWindow->GetOperation() == ImGuizmo::ROTATE)
		//{
		//	ImGui::PushStyleColor(ImGuiCol_Button, myColor);
		//	ImGui::Button(ICON_FA_SYNC_ALT, size);
		//	ImGui::PopStyleColor();
		//}
		//else
		//{
		//	if (ImGui::Button(ICON_FA_SYNC_ALT, size))
		//		m_viewportWindow->SetOperation(ImGuizmo::ROTATE);
		//}

		ImGui::SameLine();

		///////////////////////////////////////////////////////

		//if (m_viewportWindow->GetOperation() == ImGuizmo::SCALE)
		//{
		//	ImGui::PushStyleColor(ImGuiCol_Button, myColor);
		//	ImGui::Button(ICON_FA_EXPAND_ARROWS_ALT, size);
		//	ImGui::PopStyleColor();
		//}
		//else
		//{
		//	if (ImGui::Button(ICON_FA_EXPAND_ARROWS_ALT, size))
		//		m_viewportWindow->SetOperation(ImGuizmo::SCALE);
		//}

		ImGui::SameLine();

		///////////////////////////////////////////////////////

		if (ImGui::BeginPopupContextItem())
		{
			for (UINT i = 0; i < sizeof(fonts); i++)
				ImGui::PushFont(fonts[i]);
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
		//if (game->hide_window)
		//{
		//	ImGui::PushStyleColor(ImGuiCol_Button, myColor);
		//	if (ImGui::Button(ICON_FA_LEAF, size))
		//	{
		//		game->hide_window = false;
		//		game->SetWindowState(SW_NORMAL);
		//	}
		//	ImGui::PopStyleColor();
		//}
		//else
		//{
		//	if (ImGui::Button(ICON_FA_LEAF, size))
		//	{
		//		game->hide_window = true;
		//		game->SetWindowState(SW_HIDE);
		//	}
		//}

		///////////////////////////////////////////////////////

		ImGui::SameLine();

		//if (game->GetGameState() == GameState::GamePlay)
		//{
		//	ImGui::PushStyleColor(ImGuiCol_Button, myColor);
		//	if (ImGui::Button(ICON_FA_PLAY, size))
		//		game->StopGame();
		//	ImGui::PopStyleColor();
		//}
		//else
		//{
		//	if (ImGui::Button(ICON_FA_PLAY, size))
		//		game->StartGame(dx->hwnd);
		//}

		///////////////////////////////////////////////////////

		ImGui::SameLine();
		ImGui::Button(ICON_FA_PAUSE, size);

		///////////////////////////////////////////////////////

		ImGui::PopFont();
	}
	ImGui::End();

	ImGui::PopStyleVar(5);
}

void Editor::RenderToolBar()
{	
	static ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::SetNextWindowSize(ImVec2((float)EngineHelpers::GetContextWidth(m_hWnd), WINDOW_DOWN));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::Begin(u8"ToolBar", nullptr, window_flags);
	{
		ImGui::Text(u8"相机移动速度：");
		ImGui::SameLine();
		float cameraSpeed = m_dx->GetCameraSpeed();
		ImGui::SliderFloat(u8" ", &cameraSpeed, 1, 20);
		m_dx->SetCameraSpeed(cameraSpeed);
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

void Editor::RayVector(float mouseX, float mouseY, DirectX::XMVECTOR& pickRayInWorldSpacePos, DirectX::XMVECTOR& pickRayInWorldSpaceDir)
{
	using namespace DirectX;

	XMVECTOR pickRayInViewSpaceDir = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	XMVECTOR pickRayInViewSpacePos = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);

	float PRVecX = 0.0f, PRVecY = 0.0f, PRVecZ = 0.0f;

	//DirectX::XMMATRIX m_projection = m_dx->GetPerspectiveProjectionMatrix();
	//PRVecX = (((2.0f * mouseX) / windowSize.x) - 1) / m_projection(0, 0);
	//PRVecY = -(((2.0f * mouseY) / (windowSize.y - windowCursorPos.y)) - 1) / m_projection(1, 1);
	PRVecZ = 1.0f;

	pickRayInViewSpaceDir = XMVectorSet(PRVecX, PRVecY, PRVecZ, 0.0f);

	//XMMATRIX pickRayToWorldSpaceMatrix;
	//XMVECTOR matInvDeter;

	//pickRayToWorldSpaceMatrix = XMMatrixInverse(&matInvDeter, m_dx->GetPerspectiveViewMatrix());

	//pickRayInWorldSpacePos = XMVector3TransformCoord(pickRayInViewSpacePos, pickRayToWorldSpaceMatrix);
	//pickRayInWorldSpaceDir = XMVector3TransformNormal(pickRayInViewSpaceDir, pickRayToWorldSpaceMatrix);
}

bool Editor::PointInTriangle(DirectX::XMVECTOR& triV1, DirectX::XMVECTOR& triV2, DirectX::XMVECTOR& triV3, DirectX::XMVECTOR& point)
{
	using namespace DirectX;

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

float Editor::PickMesh(DirectX::XMVECTOR pickRayInWorldSpacePos, DirectX::XMVECTOR pickRayInWorldSpaceDir, const std::vector<Vertex>& vertPosArray, const std::vector<UINT>& indexPosArray, DirectX::XMMATRIX worldSpace)
{
	using namespace DirectX;

	for (int i = 0; i < indexPosArray.size() / 3; i++)
	{
		XMVECTOR tri1V1 = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		XMVECTOR tri1V2 = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		XMVECTOR tri1V3 = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);

		XMFLOAT3 tV1, tV2, tV3;

		tV1 = vertPosArray[indexPosArray[(i * 3) + 0]].Pos;
		tV2 = vertPosArray[indexPosArray[(i * 3) + 1]].Pos;
		tV3 = vertPosArray[indexPosArray[(i * 3) + 2]].Pos;

		tri1V1 = XMVectorSet(tV1.x, tV1.y, tV1.z, 0.0f);
		tri1V2 = XMVectorSet(tV2.x, tV2.y, tV2.z, 0.0f);
		tri1V3 = XMVectorSet(tV3.x, tV3.y, tV3.z, 0.0f);

		tri1V1 = XMVector3TransformCoord(tri1V1, worldSpace);
		tri1V2 = XMVector3TransformCoord(tri1V2, worldSpace);
		tri1V3 = XMVector3TransformCoord(tri1V3, worldSpace);

		XMVECTOR U = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		XMVECTOR V = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		XMVECTOR faceNormal = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);

		U = tri1V2 - tri1V1;
		V = tri1V3 - tri1V1;

		faceNormal = XMVector3Cross(U, V);
		faceNormal = XMVector3Normalize(faceNormal);

		XMVECTOR triPoint = tri1V1;

		float tri1A = XMVectorGetX(faceNormal);
		float tri1B = XMVectorGetY(faceNormal);
		float tri1C = XMVectorGetZ(faceNormal);
		float tri1D = (-tri1A * XMVectorGetX(triPoint) - tri1B * XMVectorGetY(triPoint) - tri1C * XMVectorGetZ(triPoint));

		float ep1, ep2, t = 0.0f;
		float planeIntersectX, planeIntersectY, planeIntersectZ = 0.0f;
		XMVECTOR pointInPlane = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);

		ep1 = (XMVectorGetX(pickRayInWorldSpacePos) * tri1A) + (XMVectorGetY(pickRayInWorldSpacePos) * tri1B) + (XMVectorGetZ(pickRayInWorldSpacePos) * tri1C);
		ep2 = (XMVectorGetX(pickRayInWorldSpaceDir) * tri1A) + (XMVectorGetY(pickRayInWorldSpaceDir) * tri1B) + (XMVectorGetZ(pickRayInWorldSpaceDir) * tri1C);

		if (ep2 != 0.0f)
			t = -(ep1 + tri1D) / (ep2);

		if (t > 0.0f)
		{
			planeIntersectX = XMVectorGetX(pickRayInWorldSpacePos) + XMVectorGetX(pickRayInWorldSpaceDir) * t;
			planeIntersectY = XMVectorGetY(pickRayInWorldSpacePos) + XMVectorGetY(pickRayInWorldSpaceDir) * t;
			planeIntersectZ = XMVectorGetZ(pickRayInWorldSpacePos) + XMVectorGetZ(pickRayInWorldSpaceDir) * t;

			pointInPlane = XMVectorSet(planeIntersectX, planeIntersectY, planeIntersectZ, 0.0f);

			if (PointInTriangle(tri1V1, tri1V2, tri1V3, pointInPlane))
			{
				return t / 2.0f;
			}
		}
	}

	return FLT_MAX;
}

void Editor::RunRay(POINT mousePoint)
{
	//float tempDist = 0.0f;
	//float closestDist = FLT_MAX;

	//XMVECTOR prwsPos, prwsDir;
	//RayVector(mousePoint.x, mousePoint.y, prwsPos, prwsDir);

	//auto view = m_ComponentServices->registry.view<MeshComponent>();
	//for (auto entity : view)
	//{
	//	auto& meshComp = m_ComponentServices->registry.get<MeshComponent>(entity);
	//	//tempDist = PickMesh(prwsPos, prwsDir, meshComp.GetVertices(), meshComp.GetIndices(), transComp.GetTransform());
	//	if (tempDist < closestDist && m_ComponentServices->registry.get<GeneralComponent>(entity).IsEnabled())
	//	{
	//		closestDist = tempDist;
	//		break;
	//	}
	//	else
	//	{
	//	}
	//}
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
	if (ImGui::BeginMenu(u8"文件"))
	{
		if (ImGui::BeginMenu(u8"新建"))
		{
			if (ImGui::MenuItem(u8"场景"))
			{
				m_dx->SetPosition3f(DirectX::XMFLOAT3(0.0f, 0.0f, -5.0f));
				m_projectSceneSystem->NewScene(L"未命名场景");
			}
			ImGui::MenuItem(u8"项目");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(u8"打开"))
		{
			if (ImGui::MenuItem(u8"场景"))
			{	
				m_projectSceneSystem->OpenScene();
			}
			if (ImGui::MenuItem(u8"项目"))
			{
				m_projectSceneSystem->OpenProject();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(u8"保存"))
		{
			if (ImGui::MenuItem(u8"场景"))
				m_projectSceneSystem->SaveScene();
			ImGui::MenuItem(u8"项目");
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem(u8"退出"))
			PostQuitMessage(0);
		ImGui::EndMenu();
	}
}

void Editor::RenderEditMenuBar()
{
	if (ImGui::BeginMenu(u8"编辑"))
	{
		ImGui::MenuItem(u8"撤消");
		ImGui::MenuItem(u8"重做");
		ImGui::Separator();
		ImGui::MenuItem(u8"项目");
		ImGui::MenuItem(u8"场景");
		ImGui::EndMenu();
	}
}

void Editor::RenderAssetsMenuBar()
{
	if (ImGui::BeginMenu(u8"资源"))
	{
		//if (m_assetsWindow.GetOutCore())
		{
			if (ImGui::BeginMenu(u8"创建"))
			{
				if (ImGui::MenuItem(u8"文件夹"))
				{
					std::wstring buffer = EngineUtils::GetProjectDirPath() + L"\\" + L"Folder"; /* path + name */
					unsigned int safe = m_assetsWindow.GetSafeName(buffer);
					std::wstring str = buffer + std::to_wstring(safe);
					m_assetsWindow.CreateDir(str);
					m_assetsWindow.RefreshDir();
				}
				ImGui::Separator();
				if (ImGui::MenuItem(u8"Lua 脚本"))
				{
					std::wstring buffer = EngineUtils::GetProjectDirPath() + L"\\" + L"LuaScript"; /* path + name */
					unsigned int safe = m_assetsWindow.GetSafeName(buffer, FILEs::File_Type::LUAFILE);
					std::wstring str = buffer + std::to_wstring(safe) + L".lua";
					std::wstring table = L"LuaScript" + std::to_wstring(safe);
					m_scriptingSystem->CreateScript(str.c_str(), table.c_str());
					m_assetsWindow.RefreshDir();
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (m_assetsWindow.GetSelFile() != nullptr)
			{
				if (ImGui::MenuItem(u8"移除"))
				{
					std::wstring buffer = EngineUtils::GetProjectDirPath() + L"\\" + m_assetsWindow.GetSelFile()->file_name;
					m_assetsWindow.RemoveAsset(buffer);
					m_assetsWindow.RefreshDir();
				}
			}
			else
			{
				ImGui::MenuItem(u8"移除", "", false, false);
			}
		}
		//else
		//{
		//	if (ImGui::BeginMenu(u8"创建"))
		//	{
		//		ImGui::MenuItem(u8"文件夹", "", false, false);
		//		ImGui::Separator();
		//		ImGui::MenuItem(u8"Lua 脚本", "", false, false);
		//		ImGui::EndMenu();
		//	}
		//	ImGui::Separator();
		//	ImGui::MenuItem(u8"移除", "", false, false);
		//}
		ImGui::EndMenu();
	}
}

void Editor::RenderEntityMenuBar()
{
	if (ImGui::BeginMenu(u8"实体"))
	{
		if (ImGui::BeginMenu(u8"创建"))
		{
			if (ImGui::MenuItem(u8"空的"))
			{
				name = L"空的";
				openCreateWindow = true;
				CreaItem = CreateItem::EmptyItem;

				//auto entity = ecs->CreateEntity();
				//m_ComponentServices->CreateEmptyEntity(entity);
				//m_ComponentServices->selected = entity;
			}
			ImGui::Separator();
			if (ImGui::MenuItem(u8"天空"))
			{
				name = L"天空";
				openCreateWindow = true;
				CreaItem = CreateItem::SkyItem;

			//	if (m_hierarchyWindow.CreateComponentWindow(&name))
			//	{
			//		auto entity = m_ComponentServices->CreateEntity();
			//		m_ComponentServices->CreateSkyEntity(entity, name);
			//		m_ComponentServices->selected = entity;
			//	}
			}
			if (ImGui::MenuItem(u8"盒子"))
			{
				name = L"盒子";
				openCreateWindow = true;
				CreaItem = CreateItem::BoxItem;
				
				//if (m_hierarchyWindow.CreateComponentWindow(&name, &transform))
				//{
				//	auto entity = m_ComponentServices->CreateEntity();
				//	m_ComponentServices->CreateCubeEntity(entity, name, &transform);
				//	m_ComponentServices->selected = entity;
				//}
			}
			if (ImGui::MenuItem(u8"球体"))
			{
				name = L"球体";
				openCreateWindow = true;
				CreaItem = CreateItem::SphereItem;
				
				//if (m_hierarchyWindow.CreateComponentWindow(&name, &transform))
				//{
				//	auto entity = m_ComponentServices->CreateEntity();
				//	m_ComponentServices->CreateSphereEntity(entity, name, &transform);
				//	m_ComponentServices->selected = entity;
				//}
			}
			if (ImGui::MenuItem(u8"胶囊"))
			{
				name = L"胶囊";
				openCreateWindow = true;
				CreaItem = CreateItem::CapsuleItem;
				
				//if (m_hierarchyWindow.CreateComponentWindow(&name, &transform))
				//{
				//	auto entity = m_ComponentServices->CreateEntity();
				//	m_ComponentServices->CreateCapsuleEntity(entity, name, &transform);
				//	m_ComponentServices->selected = entity;
				//}
			}
			if (ImGui::MenuItem(u8"平面"))
			{
				name = L"平面";
				openCreateWindow = true;
				CreaItem = CreateItem::PlaneItem;
				
				//if (m_hierarchyWindow.CreateComponentWindow(&name, &transform))
				//{
				//	auto entity = m_ComponentServices->CreateEntity();
				//	m_ComponentServices->CreatePlaneEntity(entity, name, &transform);
				//	m_ComponentServices->selected = entity;
				//}
			}
			ImGui::Separator();
			if (ImGui::MenuItem(u8"相机"))
			{
				name = L"相机";
				openCreateWindow = true;
				CreaItem = CreateItem::CameraItem;
				
				//if (m_hierarchyWindow.CreateComponentWindow(&name, &transform))
				//{
				//	auto entity = m_ComponentServices->CreateEntity();
				//	m_ComponentServices->CreateCameraEntity(entity, name, &transform);
				//	m_ComponentServices->selected = entity;
				//}
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		//if (m_ComponentServices->selected != entt::null)
		//{
		//	if (ImGui::MenuItem(u8"复制")) {}
		//	if (ImGui::MenuItem(u8"粘贴")) {}
		//	ImGui::Separator();
		//	if (ImGui::MenuItem(u8"移除"))
		//	{
		//		m_ComponentServices->GetComponent<GeneralComponent>(m_ComponentServices->selected).Destroy(m_ComponentServices);
		//	}
		//	ImGui::Separator();
		//	if (ImGui::MenuItem(u8"上移"))
		//	{
		//		m_ComponentServices->GetComponent<GeneralComponent>(m_ComponentServices->selected).MoveUp(m_ComponentServices);
		//	}
		//	if (ImGui::MenuItem(u8"下移"))
		//	{
		//		m_ComponentServices->GetComponent<GeneralComponent>(m_ComponentServices->selected).MoveDown(m_ComponentServices);
		//	}
		//}
		//else
		//{
		//	ImGui::MenuItem(u8"复制", "", false, false);
		//	ImGui::MenuItem(u8"粘贴", "", false, false);
		//	ImGui::Separator();
		//	ImGui::MenuItem(u8"移除", "", false, false);
		//	ImGui::Separator();
		//	ImGui::MenuItem(u8"上移", "", false, false);
		//	ImGui::MenuItem(u8"下移", "", false, false);
		//}
		ImGui::EndMenu();
	}
}

void Editor::RenderWindowMenuBar()
{
	if (ImGui::BeginMenu(u8"窗口"))
	{
		if (ImGui::MenuItem(u8"层次", NULL))
			m_hierarchyWindow.NeedRender(true);
		if (ImGui::MenuItem(u8"画面设置", NULL))
			m_screenSettingsWindow.NeedRender(true);
		if (ImGui::MenuItem(u8"控制台", NULL))
			m_consoleWindow.NeedRender(true);
		if (ImGui::MenuItem(u8"资源", NULL))
			m_assetsWindow.NeedRender(true);
		if (ImGui::MenuItem(u8"文件信息", NULL))
			m_fileWindow.NeedRender(true);
		if (ImGui::MenuItem(u8"实体信息", NULL))
			m_inspectorWindow.NeedRender(true);
		ImGui::EndMenu();
	}
}

void Editor::RenderHelpMenuBar()
{
	if (ImGui::BeginMenu(u8"帮助"))
	{
		if (ImGui::MenuItem(u8"关于"))
		{
			m_aboutWindow.NeedRender(true);
		}
		ImGui::EndMenu();
	}
}

void Editor::RenderScriptMenuBar()
{
	if (ImGui::BeginMenu(u8"脚本"))
	{
		if (ImGui::MenuItem(u8"重新编译"))
		{
			//auto view = m_ComponentServices->registry.view<ScriptingComponent>();
			//for (auto entity : view)
			//	m_ComponentServices->GetComponent<ScriptingComponent>(entity).RecompileScripts();
		}
		ImGui::EndMenu();
	}
}
