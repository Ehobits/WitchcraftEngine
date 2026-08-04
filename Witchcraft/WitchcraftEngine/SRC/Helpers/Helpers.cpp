#include "Helpers.h"
#include <xstring>
#include <stdio.h>
#include <filesystem>
#include "D3DWindow/D3DWindow.h"
#include <commdlg.h>
#include <shobjidl.h>
#include <comdef.h>

static UINT NativeDialogMessage = WM_APP + 0x3A7;

UINT EngineHelpers::GetDisplayWidth(HWND hWnd)
{
	MONITORINFO monInfo;
	monInfo.cbSize = sizeof(MONITORINFO);

	HMONITOR hMonitor;
	if (hWnd)
		hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	else
		hMonitor = MonitorFromPoint(POINT(0, 0), MONITOR_DEFAULTTONEAREST);
	GetMonitorInfo(hMonitor, &monInfo);

	return monInfo.rcMonitor.right - monInfo.rcMonitor.left;
}

UINT EngineHelpers::GetDisplayHeight(HWND hWnd)
{
	MONITORINFO monInfo;
	monInfo.cbSize = sizeof(MONITORINFO);

	HMONITOR hMonitor;
	if (hWnd)
		hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	else
		hMonitor = MonitorFromPoint(POINT(0, 0), MONITOR_DEFAULTTONEAREST);
	GetMonitorInfo(hMonitor, &monInfo);

	return monInfo.rcMonitor.bottom - monInfo.rcMonitor.top;
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

UINT EngineHelpers::GetDisplayDPI(HWND hWnd)
{
	return GetDpiForWindow(hWnd);
}

bool EngineHelpers::TryOpenFileDialog(HWND hWnd, LPCWSTR dir, LPCWSTR filter, LPCWSTR title, std::wstring* outPath)
{
	NativeDialogRequest request;
	request.Type = NativeDialogType::OpenFile;
	request.OwnerWindow = hWnd;
	request.Directory = dir;
	request.Filter = filter;
	request.Title = title;
	request.OutputPath = outPath;

	if (hWnd == nullptr || !DispatchNativeDialogRequest(hWnd, &request))
		return RunFileDialogOnCurrentThread(hWnd, dir, filter, title, nullptr, false, outPath);

	if (GetWindowThreadProcessId(hWnd, nullptr) == GetCurrentThreadId())
		return RunFileDialogOnCurrentThread(hWnd, dir, filter, title, nullptr, false, outPath);

	return request.FileDialogResult;
}

bool EngineHelpers::TrySaveFileDialog(HWND hWnd, LPCWSTR dir, LPCWSTR filter, LPCWSTR title, LPCWSTR defaultExt, std::wstring* outPath)
{
	NativeDialogRequest request;
	request.Type = NativeDialogType::SaveFile;
	request.OwnerWindow = hWnd;
	request.Directory = dir;
	request.Filter = filter;
	request.Title = title;
	request.DefaultExtension = defaultExt;
	request.OutputPath = outPath;

	if (hWnd == nullptr || !DispatchNativeDialogRequest(hWnd, &request))
		return RunFileDialogOnCurrentThread(hWnd, dir, filter, title, defaultExt, true, outPath);

	if (GetWindowThreadProcessId(hWnd, nullptr) == GetCurrentThreadId())
		return RunFileDialogOnCurrentThread(hWnd, dir, filter, title, defaultExt, true, outPath);

	return request.FileDialogResult;
}

int EngineHelpers::ShowMessageBox(HWND hWnd, LPCWSTR text, LPCWSTR title, UINT type)
{
	NativeDialogRequest request;
	request.Type = NativeDialogType::MessageWin;
	request.OwnerWindow = hWnd;
	request.Text = text;
	request.Title = title;
	request.MessageBoxType = type;

	if (hWnd == nullptr || !DispatchNativeDialogRequest(hWnd, &request))
		return MessageBoxW(hWnd, text, title, type);

	if (GetWindowThreadProcessId(hWnd, nullptr) == GetCurrentThreadId())
		return MessageBoxW(hWnd, text, title, type);

	return request.MessageBoxResult;
}

bool EngineHelpers::TryHandleNativeDialogWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* outResult)
{
	if (message != NativeDialogMessage || lParam != 0 || wParam == 0)
		return false;

	NativeDialogRequest* request = reinterpret_cast<NativeDialogRequest*>(wParam);
	if (request == nullptr || request->OwnerWindow != hWnd)
		return false;

	switch (request->Type)
	{
	case NativeDialogType::OpenFile:
		request->FileDialogResult = RunFileDialogOnCurrentThread(
			hWnd, request->Directory, request->Filter, request->Title, nullptr, false, request->OutputPath);
		break;
	case NativeDialogType::SaveFile:
		request->FileDialogResult = RunFileDialogOnCurrentThread(
			hWnd, request->Directory, request->Filter, request->Title, request->DefaultExtension, true, request->OutputPath);
		break;
	case NativeDialogType::MessageWin:
		request->MessageBoxResult = MessageBoxW(hWnd, request->Text, request->Title, request->MessageBoxType);
		break;
	default:
		return false;
	}

	if (outResult != nullptr)
		*outResult = 0;
	return true;
}

bool EngineHelpers::RunFileDialogOnCurrentThread(HWND hWnd,
	LPCWSTR dir, LPCWSTR filter, LPCWSTR title, LPCWSTR defaultExt,
	bool isSaveDialog, std::wstring* outPath)
{
	if (outPath == nullptr)
		return false;

	outPath->clear();

	const LPCWSTR dialogFilter =
		(filter != nullptr && filter[0] != L'\0')
		? filter
		: L"所有文件 (*.*)\0*.*\0\0";
	const LPCWSTR dialogInitialDir =
		(dir != nullptr && dir[0] != L'\0')
		? dir
		: nullptr;
	const LPCWSTR dialogTitle =
		(title != nullptr && title[0] != L'\0')
		? title
		: (isSaveDialog ? L"保存文件" : L"打开文件");
	const LPCWSTR dialogDefaultExt =
		(defaultExt != nullptr && defaultExt[0] != L'\0')
		? defaultExt
		: nullptr;

	wchar_t wtext[MAX_PATH];
	ZeroMemory(&wtext, sizeof(wtext));

	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFile = wtext;
	ofn.lpstrFile[0] = L'\0';
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = dialogFilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = dialogInitialDir;
	ofn.lpstrDefExt = dialogDefaultExt;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!isSaveDialog)
		ofn.Flags |= OFN_FILEMUSTEXIST;
	ofn.lpstrTitle = dialogTitle;

	const std::filesystem::path originalCurrentPath = std::filesystem::current_path();
	const BOOL dialogResult = isSaveDialog ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn);
	std::error_code restoreError;
	std::filesystem::current_path(originalCurrentPath, restoreError);

	if (!dialogResult)
		return false;

	*outPath = wtext;
	return true;
}

bool EngineHelpers::DispatchNativeDialogRequest(HWND hWnd, NativeDialogRequest* request)
{
	if (request == nullptr || hWnd == nullptr || !IsWindow(hWnd))
		return false;

	const DWORD ownerThreadId = GetWindowThreadProcessId(hWnd, nullptr);
	if (ownerThreadId == 0)
		return false;

	if (ownerThreadId == GetCurrentThreadId())
		return true;

	// SendMessage 将保持请求及其输出缓冲区有效，直到窗口线程完成显示模式对话框。
	SendMessageW(
		hWnd,
		NativeDialogMessage,
		reinterpret_cast<WPARAM>(request),
		0);
	return true;
}
