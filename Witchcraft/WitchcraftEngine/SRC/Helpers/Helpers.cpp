#include "Helpers.h"
#include <xstring>
#include <stdio.h>
#include <filesystem>
#include "D3DWindow/D3DWindow.h"
#include <shobjidl.h>
#include <comdef.h>

UINT EngineHelpers::GetDisplayWidth()
{
	return GetSystemMetrics(SM_CXSCREEN);
}

UINT EngineHelpers::GetDisplayHeight()
{
	return GetSystemMetrics(SM_CYSCREEN);
}

UINT EngineHelpers::GetContextWidth(HWND hWnd)
{
	RECT rc;
	GetClientRect(hWnd, &rc);
	UINT width = rc.right - rc.left;
	return width;
}

UINT EngineHelpers::GetContextHeight(HWND hWnd)
{
	RECT rc;
	GetClientRect(hWnd, &rc);
	UINT height = rc.bottom - rc.top;
	return height;
}

void EngineHelpers::AddLog(const wchar_t* text, ...)
{
	wchar_t buff[MAX_PATH];
	va_list args;
	va_start(args, text);
	_vsnwprintf_s(buff, MAX_PATH, text, args);
	va_end(args);
	OutputDebugStringW(buff);
	OutputDebugStringW(L"\n");
}

RECT EngineHelpers::GetClientRect(HWND hWnd)
{
	RECT mainWindow;
	GetClientRect(hWnd, &mainWindow);

	POINT left_top = { mainWindow.left, mainWindow.top };
	POINT right_bottom = { mainWindow.right, mainWindow.bottom };
	ClientToScreen(hWnd, &left_top);
	ClientToScreen(hWnd, &right_bottom);

	RECT clip;
	SetRect(&clip, left_top.x, left_top.y, right_bottom.x, right_bottom.y);
	return clip;
}

const wchar_t* EngineHelpers::OpenFileDialog(HWND hWnd, LPCWSTR dir, LPCWSTR filter, LPCWSTR title)
{
	wchar_t wtext[MAX_PATH];
	ZeroMemory(&wtext, sizeof(wtext));

	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFile = wtext;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = dir;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	ofn.lpstrTitle = title;
	GetOpenFileName(&ofn);

	return wtext;
}
