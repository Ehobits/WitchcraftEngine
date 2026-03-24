
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
AppMember am;

int StartEngine(std::wstring, HINSTANCE&, HINSTANCE&, LPWSTR&, int&);
void WakeupThreads();
HWND MyCreateWindow(std::wstring, HINSTANCE&, int, int);
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void EnqueueInputMessage(const InputMessage& inputMessage);
void ProcessInputMessage(const InputMessage& inputMessage);
void StopThreads();

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
#if defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
		return false;

	FILE* fp = nullptr;
	freopen_s(&fp, "log.txt", "w", stdout);
	std::cout << "..." << std::endl;

	std::filesystem::path path = EngineUtils::GetAppDirPath();

	am.dx = new D3DWindow();
	int ret = StartEngine(path.c_str(), hInstance, hPrevInstance, lpCmdLine, nCmdShow);

	if (ret >= 0)
		 am.engine.EngineShutdown();
	delete am.dx;
	am.dx = nullptr;

	CoUninitialize();

	return ret;
}

int StartEngine(std::wstring MainPath, HINSTANCE& hInstance, HINSTANCE& hPrevInstance, LPWSTR& lpCmdLine, int& nCmdShow)
{
	HWND hWnd = MyCreateWindow(L"WitchcraftEngine", hInstance, 1280, 720);

	if (!am.dx->Create(hWnd, &am.engine.timer, &am.editor))
	{
		MessageBox(nullptr, L"初始化 DirectX12 失败！", L"错误", MB_OK);
		return -1;
	}

	am.engine.EngineStart(am.dx, &am.editor, MainPath);

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	am.editor.Init(hWnd, &am.engine, MainPath);

	am.dx->BegineThread();
	am.run = true;
	WakeupThreads();

	while (WM_QUIT != am.msg.message)
	{
		if (PeekMessage(&am.msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&am.msg);
			DispatchMessage(&am.msg);
		}
	}

	StopThreads();
	return (int)am.msg.wParam;
}

void RenderThreadWork(UINT ID)
{
	WINDOWPLACEMENT wp;
	wp.length = sizeof(wp);
	
	while (am.run.load())
	{
		GetWindowPlacement(am.dx->GethWnd(), &wp);
		if (wp.showCmd != SW_SHOWMINIMIZED)
		{
			if (am.resize.exchange(false))
			{
				am.dx->OnResize();
			}
			am.engine.EngineProcess();
			am.dx->RenderB();
			am.dx->RenderE();
		}
	}
}

void UserInputThreadWork(UINT ID)
{
	while (true)
	{
		InputMessage inputMessage;
		{
			std::unique_lock<std::mutex> lock(am.inputMutex);
			am.inputCv.wait(lock, []()
				{
					return !am.run.load() || !am.inputMessages.empty();
				});

			if (!am.run.load() && am.inputMessages.empty())
				break;

			inputMessage = am.inputMessages.front();
			am.inputMessages.pop();
		}

		ProcessInputMessage(inputMessage);
	}
}

void EnqueueInputMessage(const InputMessage& inputMessage)
{
	{
		std::lock_guard<std::mutex> lock(am.inputMutex);
		am.inputMessages.push(inputMessage);
	}
	am.inputCv.notify_one();
}

void ProcessInputMessage(const InputMessage& inputMessage)
{
	KeyboardClass* keyboard = am.engine.GetKeyboard();
	MouseClass* mouse = am.engine.GetMouse();

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

void StopThreads()
{
	am.run = false;
	am.inputCv.notify_all();

	if (am.inputThread.joinable())
		am.inputThread.join();

	if (am.renderThread.joinable())
		am.renderThread.join();
}

void WakeupThreads()
{
	am.renderThread = std::thread(RenderThreadWork, 1);
	am.inputThread = std::thread(UserInputThreadWork, 2);
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
	am.editor.SetProcHandler(hwnd, uMsg, wParam, lParam);
	if ((uMsg > WM_MDISETMENU && uMsg < WM_MDIREFRESHMENU) || uMsg == WM_SIZE)
		am.resize = true;

	InputMessage inputMessage = {};
	bool queueInputMessage = false;
	switch (uMsg)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		inputMessage.type = InputMessageType::KeyDown;
		inputMessage.key = static_cast<BYTE>(wParam);
		inputMessage.wasPressed = (lParam & 0x40000000) != 0;
		queueInputMessage = true;
		break;

	case WM_KEYUP:
	case WM_SYSKEYUP:
		inputMessage.type = InputMessageType::KeyUp;
		inputMessage.key = static_cast<BYTE>(wParam);
		queueInputMessage = true;
		break;

	case WM_CHAR:
	case WM_SYSCHAR:
		inputMessage.type = InputMessageType::Char;
		inputMessage.key = static_cast<BYTE>(wParam);
		inputMessage.wasPressed = (lParam & 0x40000000) != 0;
		queueInputMessage = true;
		break;

	case WM_MOUSEMOVE:
		inputMessage.type = InputMessageType::MouseMove;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		queueInputMessage = true;
		break;

	case WM_LBUTTONDOWN:
		inputMessage.type = InputMessageType::MouseLeftDown;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		queueInputMessage = true;
		break;

	case WM_LBUTTONUP:
		inputMessage.type = InputMessageType::MouseLeftUp;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		queueInputMessage = true;
		break;

	case WM_RBUTTONDOWN:
		inputMessage.type = InputMessageType::MouseRightDown;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		queueInputMessage = true;
		break;

	case WM_RBUTTONUP:
		inputMessage.type = InputMessageType::MouseRightUp;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		queueInputMessage = true;
		break;

	case WM_MBUTTONDOWN:
		inputMessage.type = InputMessageType::MouseMiddleDown;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		queueInputMessage = true;
		break;

	case WM_MBUTTONUP:
		inputMessage.type = InputMessageType::MouseMiddleUp;
		inputMessage.x = GET_X_LPARAM(lParam);
		inputMessage.y = GET_Y_LPARAM(lParam);
		queueInputMessage = true;
		break;

	case WM_MOUSEWHEEL:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ScreenToClient(hwnd, &pt);
		inputMessage.type = InputMessageType::MouseWheel;
		inputMessage.x = pt.x;
		inputMessage.y = pt.y;
		inputMessage.wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		queueInputMessage = true;
	}
		break;

	case WM_KILLFOCUS:
		inputMessage.type = InputMessageType::ResetState;
		queueInputMessage = true;
		break;
	}

	if (queueInputMessage)
		EnqueueInputMessage(inputMessage);

	switch (uMsg)
	{
	case WM_ACTIVATE:
	{
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			am.engine.timer.Stop();
		}
		else
		{
			am.engine.timer.Start();
		}
	}
	return 0;

	case WM_GETMINMAXINFO:
	{
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 256;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 256;
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
