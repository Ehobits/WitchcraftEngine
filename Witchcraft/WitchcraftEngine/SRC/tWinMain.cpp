
#include <crtdbg.h>
#include <Windows.h>
#include <atlbase.h>
#include <iostream>
#include <fstream>
#include "Engine/Engine.h"
#include "Engine/EngineUtils.h"
#include "UserInput/Keyboard/KeyboardClass.h"
#include "UserInput/Mouse/MouseClass.h"
#include "Editor/Editor.h"

Engine engine;
D3DWindow* dx;
Editor editor;

// 前向声明
int StartEngine(std::wstring, HINSTANCE&, HINSTANCE&, LPWSTR&, int&);
HWND MyCreateWindow(std::wstring, HINSTANCE&, int, int);
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

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

	dx = new D3DWindow();
	int ret = StartEngine(path.c_str(), hInstance, hPrevInstance, lpCmdLine, nCmdShow);

	engine.EngineShutdown();
	
	CoUninitialize();

	return ret;
}

int StartEngine(std::wstring MainPath, HINSTANCE& hInstance, HINSTANCE& hPrevInstance, LPWSTR& lpCmdLine, int& nCmdShow)
{
	HWND hWnd = MyCreateWindow(L"WitchcraftEngine", hInstance, 1280, 720);

	if (!dx->Create(hWnd, &engine.timer, &editor))
		MessageBox(nullptr, L"创建DirectX12 失败！", L"错误", MB_OK);

	engine.EngineStart(dx, &editor, MainPath);

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	editor.Init(hWnd, &engine, dx, MainPath);

	dx->BegineThread();

	MSG msg = { 0 };

	while (WM_QUIT != msg.message)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		engine.EngineProcess();
		dx->RenderB();
		dx->RenderE();
	}

	return (int)msg.wParam;
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
		MessageBox(nullptr, L"注册窗类口失败！", L"错误", MB_OK);
		return nullptr;
	}

	/*---*/

	int x = (EngineHelpers::GetDisplayWidth() - width) / 2;
	int y = (EngineHelpers::GetDisplayHeight() - height) / 2;

	HWND hwnd = CreateWindow(
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
		MessageBox(nullptr, L"创建窗口失败！", L"错误", MB_OK);

	/*---*/

	return hwnd;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	editor.SetProcHandler(hwnd, uMsg, wParam, lParam);

	switch (uMsg)
	{
	case WM_ACTIVATE:
	{
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			engine.timer.Stop();
		}
		else
		{
			engine.timer.Start();
		}
	}
	return 0;

	case WM_EXITSIZEMOVE:
	{
		DirectX::XMFLOAT2 size = DirectX::XMFLOAT2(LOWORD(lParam), HIWORD(lParam));
		dx->OnResize();
	}
	return 0;

	case WM_SIZE:
	{
		if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)
		{
			DirectX::XMFLOAT2 size = DirectX::XMFLOAT2(LOWORD(lParam), HIWORD(lParam));
			dx->OnResize();
		}
	}
	return 0;

	case WM_GETMINMAXINFO:
	{
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 256;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 256;
	}
	return 0;

	//Keyboard Messages
	case WM_KEYDOWN:
	{
		BYTE keycode = static_cast<BYTE>(wParam);
		if (engine.GetKeyboard()->IsKeysAutoRepeat())
		{
			engine.GetKeyboard()->OnKeyPressed(keycode);
		}
		else
		{
			const bool wasPressed = lParam & 0x40000000;
			if (!wasPressed)
			{
				engine.GetKeyboard()->OnKeyPressed(keycode);
			}
		}
	}
	return 0;
	
	case WM_KEYUP:
	{
		BYTE keycode = static_cast<BYTE>(wParam);
		engine.GetKeyboard()->OnKeyReleased(keycode);
	}
	return 0;
	
	case WM_CHAR:
	{
		BYTE ch = static_cast<BYTE>(wParam);
		if (engine.GetKeyboard()->IsCharsAutoRepeat())
		{
			engine.GetKeyboard()->OnChar(ch);
		}
		else
		{
			const bool wasPressed = lParam & 0x40000000;
			if (!wasPressed)
			{
				engine.GetKeyboard()->OnChar(ch);
			}
		}
	}
	return 0;
	
	//Mouse Messages
	case WM_MOUSEMOVE:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		engine.GetMouse()->OnMouseMove(x, y);
	}
	return 0;

	case WM_LBUTTONDOWN:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		engine.GetMouse()->OnLeftPressed(x, y);
	}
	return 0;

	case WM_RBUTTONDOWN:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		engine.GetMouse()->OnRightPressed(x, y);
	}
	return 0;

	case WM_MBUTTONDOWN:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		engine.GetMouse()->OnMiddlePressed(x, y);
	}
	return 0;

	case WM_LBUTTONUP:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		engine.GetMouse()->OnLeftReleased(x, y);
	}
	return 0;

	case WM_RBUTTONUP:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		engine.GetMouse()->OnRightReleased(x, y);
	}
	return 0;

	case WM_MBUTTONUP:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		engine.GetMouse()->OnMiddleReleased(x, y);
	}
	return 0;

	case WM_MOUSEWHEEL:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (GET_WHEEL_DELTA_WPARAM(wParam) > 0)
		{
			engine.GetMouse()->OnWheelUp(x, y);
		}
		else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0)
		{
			engine.GetMouse()->OnWheelDown(x, y);
		}
	}
	return 0;

	case WM_INPUT:
	{
		UINT dataSize = 0u;
		GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, NULL, &dataSize, sizeof(RAWINPUTHEADER)); //Need to populate data size first

		if (dataSize > 0u)
		{
			std::unique_ptr<BYTE[]> rawdata = std::make_unique<BYTE[]>(dataSize);
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawdata.get(), &dataSize, sizeof(RAWINPUTHEADER)) == dataSize)
			{
				RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(rawdata.get());
				if (raw->header.dwType == RIM_TYPEMOUSE)
				{
					engine.GetMouse()->OnMouseMoveRaw(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
				}
			}
		}

		return DefWindowProc(hwnd, uMsg, wParam, lParam); //Need to call DefWindowProc for WM_INPUT messages
	}
	return 0;
	
	case WM_DESTROY:
	{
		dx->DestroyRender();
		PostQuitMessage(0);
	}
	return 0;

	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	return NULL;
}
