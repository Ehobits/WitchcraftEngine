
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <memory>
#include <crtdbg.h>
#include <Windows.h>
#include <windowsx.h>
#include <atlbase.h>
#include <iostream>
#include <fstream>
#include "Engine/Engine.h"
#include "Engine/EngineUtils.h"
#include "UserInput/Keyboard/KeyboardClass.h"
#include "UserInput/Mouse/MouseClass.h"
#include "Editor/Editor.h"

enum class InputMessageType
{
	KeyDown,
	KeyUp,
	Char,
	MouseMove,
	MouseLeftDown,
	MouseLeftUp,
	MouseRightDown,
	MouseRightUp,
	MouseMiddleDown,
	MouseMiddleUp,
	MouseWheel,
	ResetState
};

struct InputMessage
{
	InputMessageType type = InputMessageType::MouseMove;
	BYTE key = 0;
	bool wasPressed = false;
	int x = 0;
	int y = 0;
	int wheelDelta = 0;
};

struct AppMember
{
	// 运行时核心对象与消息线程/渲染线程/输入线程共享状态。
	Engine engine;
	D3DWindow* dx = nullptr;
	Editor editor;
	std::atomic_bool run = false;
	std::atomic_bool resize = false;
	MSG msg = { 0 };
	std::thread renderThread;
	std::thread inputThread;
	std::mutex inputMutex;
	std::condition_variable inputCv;
	std::queue<InputMessage> inputMessages;
};
AppMember g_am;

int StartEngine(std::wstring, HINSTANCE&, HINSTANCE&, LPWSTR&, int&);
void StartWorkerThreads();
HWND MyCreateWindow(std::wstring, HINSTANCE&, int, int);
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void EnqueueInputMessage(const InputMessage& inputMessage);
void ProcessInputMessage(const InputMessage& inputMessage);
void StopWorkerThreads();

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
#if defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 本线程持有 Win32 窗口及所有原生对话框。Shell/TSF UI 需要 STA 套间；
	// 渲染工作保持在其工作线程上运行。
	if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)))
		return false;

	std::filesystem::path path = EngineUtils::GetAppDirPath();

	g_am.dx = new D3DWindow();
	int ret = StartEngine(path.c_str(), hInstance, hPrevInstance, lpCmdLine, nCmdShow);

	if (ret >= 0)
		 g_am.engine.EngineShutdown();
	delete g_am.dx;
	g_am.dx = nullptr;

	CoUninitialize();

	return ret;
}

int StartEngine(std::wstring MainPath, HINSTANCE& hInstance, HINSTANCE& hPrevInstance, LPWSTR& lpCmdLine, int& nCmdShow)
{
	HWND hWnd = MyCreateWindow(L"WitchcraftEngine", hInstance, 2280, 1440);

	if (!g_am.dx->Create(hWnd, &g_am.engine.timer, &g_am.editor))
	{
		MessageBox(nullptr, L"初始化 DirectX12 失败！", L"错误", MB_OK);
		return -1;
	}

	g_am.engine.EngineStart(g_am.dx, &g_am.editor, MainPath);

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	g_am.editor.Init(hWnd, &g_am.engine, MainPath);

	g_am.dx->BeginWorkerThreads();
	g_am.run = true;
	StartWorkerThreads();

	while (WM_QUIT != g_am.msg.message)
	{
		if (PeekMessage(&g_am.msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&g_am.msg);
			DispatchMessage(&g_am.msg);
		}
	}

	StopWorkerThreads();
	return (int)g_am.msg.wParam;
}

void RenderThreadMain(UINT threadId)
{
	(void)threadId;

	WINDOWPLACEMENT wp;
	wp.length = sizeof(wp);
	
	// 渲染线程：在应用运行期间持续执行引擎更新与绘制。
	while (g_am.run.load())
	{
		GetWindowPlacement(g_am.dx->GetHwnd(), &wp);
		if (wp.showCmd != SW_SHOWMINIMIZED)
		{
			if (g_am.resize.exchange(false))
			{
				Sleep(24);
				g_am.dx->OnResize(g_am.dx->GetWindowInfo().fullscreenState);
				Sleep(16);
			}
			g_am.engine.EngineProcess();
			g_am.dx->RenderB();
			g_am.dx->RenderE();
		}
	}
}

void InputThreadMain(UINT threadId)
{
	(void)threadId;

	// 输入线程：消费 WindowProc 解码后的输入消息队列。
	while (true)
	{
		InputMessage inputMessage;
		{
			std::unique_lock<std::mutex> lock(g_am.inputMutex);
			g_am.inputCv.wait(lock, []()
				{
					return !g_am.run.load() || !g_am.inputMessages.empty();
				});

			if (!g_am.run.load() && g_am.inputMessages.empty())
				break;

			inputMessage = g_am.inputMessages.front();
			g_am.inputMessages.pop();
		}

		ProcessInputMessage(inputMessage);
	}
}

void EnqueueInputMessage(const InputMessage& inputMessage)
{
	{
		std::lock_guard<std::mutex> lock(g_am.inputMutex);
		g_am.inputMessages.push(inputMessage);
	}
	g_am.inputCv.notify_one();
}

void ProcessInputMessage(const InputMessage& inputMessage)
{
	KeyboardClass* keyboard = g_am.engine.GetKeyboard();
	MouseClass* mouse = g_am.engine.GetMouse();

	switch (inputMessage.type)
	{
	case InputMessageType::KeyDown:
		if (keyboard->IsKeysAutoRepeat() || !inputMessage.wasPressed)
			keyboard->OnKeyPressed(inputMessage.key);
		break;

	case InputMessageType::KeyUp:
		keyboard->OnKeyReleased(inputMessage.key);
		break;

	case InputMessageType::Char:
		if (keyboard->IsCharsAutoRepeat() || !inputMessage.wasPressed)
			keyboard->OnChar(inputMessage.key);
		break;

	case InputMessageType::MouseMove:
		mouse->OnMouseMove(inputMessage.x, inputMessage.y);
		break;

	case InputMessageType::MouseLeftDown:
		mouse->OnLeftPressed(inputMessage.x, inputMessage.y);
		break;

	case InputMessageType::MouseLeftUp:
		mouse->OnLeftReleased(inputMessage.x, inputMessage.y);
		break;

	case InputMessageType::MouseRightDown:
		mouse->OnRightPressed(inputMessage.x, inputMessage.y);
		break;

	case InputMessageType::MouseRightUp:
		mouse->OnRightReleased(inputMessage.x, inputMessage.y);
		break;

	case InputMessageType::MouseMiddleDown:
		mouse->OnMiddlePressed(inputMessage.x, inputMessage.y);
		break;

	case InputMessageType::MouseMiddleUp:
		mouse->OnMiddleReleased(inputMessage.x, inputMessage.y);
		break;

	case InputMessageType::MouseWheel:
		if (inputMessage.wheelDelta > 0)
			mouse->OnWheelUp(inputMessage.x, inputMessage.y);
		else if (inputMessage.wheelDelta < 0)
			mouse->OnWheelDown(inputMessage.x, inputMessage.y);
		break;

	case InputMessageType::ResetState:
		keyboard->ClearState();
		mouse->Reset();
		break;
	}
}

void StopWorkerThreads()
{
	g_am.run = false;
	g_am.inputCv.notify_all();

	if (g_am.inputThread.joinable())
		g_am.inputThread.join();

	if (g_am.renderThread.joinable())
		g_am.renderThread.join();
}

void StartWorkerThreads()
{
	// 线程 ID 仅用于调试/追踪。
	g_am.renderThread = std::thread(RenderThreadMain, 1);
	g_am.inputThread = std::thread(InputThreadMain, 2);
}

HWND MyCreateWindow(std::wstring name, HINSTANCE& hInstance, int width, int height)
{
	WNDCLASSEX wcex;
	ZeroMemory(&wcex, sizeof(WNDCLASSEX));

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WindowProc;
	wcex.cbClsExtra = NULL;
	wcex.cbWndExtra = NULL;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wcex.hbrBackground = (HBRUSH)CreateSolidBrush(RGB(46, 46, 46));
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = name.c_str();
	wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wcex))
	{
		MessageBox(nullptr, L"注册窗口类失败！", L"错误", MB_OK);
		return nullptr;
	}

	/*---*/

	int x = (EngineHelpers::GetDisplayWidth() - width) / 2;
	int y = (EngineHelpers::GetDisplayHeight() - height) / 2;

	HWND hwnd = CreateWindowEx(WS_EX_NOREDIRECTIONBITMAP,
		name.c_str(),
		name.c_str(),
		WS_OVERLAPPEDWINDOW,
		x, y,
		width, height,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

	if (!hwnd)
	{
		MessageBox(nullptr, L"窗口初始化失败！", L"错误", MB_OK);
	}

	/*---*/

	return hwnd;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT nativeDialogResult = 0;
	if (EngineHelpers::TryHandleNativeDialogWindowMessage(hwnd, uMsg, wParam, lParam, &nativeDialogResult))
		return nativeDialogResult;

	g_am.editor.SetProcHandler(hwnd, uMsg, wParam, lParam);
	if ((uMsg > WM_MDISETMENU && uMsg < WM_MDIREFRESHMENU) || uMsg == WM_SIZE)
		g_am.resize = true;

	// 将窗口消息直接映射为输入消息，避免额外的函数跳转。
	InputMessage inputMessage = {};
	bool hasInputMessage = true;
	switch (uMsg)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		inputMessage.type = InputMessageType::KeyDown;
		inputMessage.key = static_cast<BYTE>(wParam);
		inputMessage.wasPressed = (lParam & 0x40000000) != 0;
		break;

	case WM_KEYUP:
	case WM_SYSKEYUP:
		inputMessage.type = InputMessageType::KeyUp;
		inputMessage.key = static_cast<BYTE>(wParam);
		break;

	case WM_CHAR:
	case WM_SYSCHAR:
		inputMessage.type = InputMessageType::Char;
		inputMessage.key = static_cast<BYTE>(wParam);
		inputMessage.wasPressed = (lParam & 0x40000000) != 0;
		break;

	case WM_MOUSEMOVE:
		inputMessage.type = InputMessageType::MouseMove;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		break;

	case WM_LBUTTONDOWN:
		inputMessage.type = InputMessageType::MouseLeftDown;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		break;

	case WM_LBUTTONUP:
		inputMessage.type = InputMessageType::MouseLeftUp;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		break;

	case WM_RBUTTONDOWN:
		inputMessage.type = InputMessageType::MouseRightDown;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		break;

	case WM_RBUTTONUP:
		inputMessage.type = InputMessageType::MouseRightUp;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		break;

	case WM_MBUTTONDOWN:
		inputMessage.type = InputMessageType::MouseMiddleDown;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		break;

	case WM_MBUTTONUP:
		inputMessage.type = InputMessageType::MouseMiddleUp;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		break;

	case WM_MOUSEWHEEL:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ScreenToClient(hwnd, &pt);
		inputMessage.type = InputMessageType::MouseWheel;
		inputMessage.x = pt.x;
		inputMessage.y = pt.y;
		inputMessage.wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
	}
	break;

	case WM_KILLFOCUS:
		inputMessage.type = InputMessageType::ResetState;
		break;

	default:
		hasInputMessage = false;
		break;
	}
	if (hasInputMessage)
		EnqueueInputMessage(inputMessage);

	switch (uMsg)
	{
	case WM_ACTIVATE:
	{
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			g_am.engine.timer.Stop();
		}
		else
		{
			g_am.engine.timer.Start();
		}
	}
	return 0;

	case WM_GETMINMAXINFO:
	{
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 256;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 256;
	}
	return 0;

	case WM_SETCURSOR:
	{
		if (LOWORD(lParam) == HTCLIENT)
		{
			if (g_am.editor.ApplyImGuiCursorForClientArea())
				return TRUE;
		}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);

	case WM_CLOSE:
	{
		ProjectSceneSystem* projectSceneSystem = g_am.engine.GetprojectSceneSystem();
		if (projectSceneSystem != nullptr && !projectSceneSystem->ConfirmLeaveCurrentSceneIfNeeded())
			return 0;

		DestroyWindow(hwnd);
	}
	return 0;

	case WM_DESTROY:
	{
		PostQuitMessage(0);
	}
	return 0;

	case WM_INPUT:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);

	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	return NULL;
}
