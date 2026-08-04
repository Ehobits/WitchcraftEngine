#pragma once

#include "MathHelpers.h"

#include <xstring>

#define MOVE_NOT_SPECIFIDE 0
#define MOVE_UP 1
#define MOVE_DOWN 2
#define MOVE_LEFT 4
#define MOVE_RIGHT 7
#define MOVE_DEEPEN 10
#define MOVE_FROMAW 15

namespace EngineHelpers
{
	enum class NativeDialogType
	{
		OpenFile,
		SaveFile,
		MessageWin
	};

	struct NativeDialogRequest
	{
		NativeDialogType Type = NativeDialogType::OpenFile;
		HWND OwnerWindow = nullptr;
		LPCWSTR Directory = nullptr;
		LPCWSTR Filter = nullptr;
		LPCWSTR Title = nullptr;
		LPCWSTR DefaultExtension = nullptr;
		LPCWSTR Text = nullptr;
		UINT MessageBoxType = 0;
		std::wstring* OutputPath = nullptr;
		bool FileDialogResult = false;
		int MessageBoxResult = 0;
	};

	RECT GetClientRect(HWND hWnd);
	UINT GetDisplayDPI(HWND hWnd);
	UINT GetDisplayWidth(HWND hWnd = nullptr);
	UINT GetDisplayHeight(HWND hWnd = nullptr);
	UINT GetContextWidth(HWND hWnd);
	UINT GetContextHeight(HWND hWnd);
	void AddLog(const wchar_t* text, ...);
	/* --------------------- */
	bool TryOpenFileDialog(HWND hWnd, LPCWSTR dir, LPCWSTR filter, LPCWSTR title, std::wstring* outPath);
	bool TrySaveFileDialog(HWND hWnd, LPCWSTR dir, LPCWSTR filter, LPCWSTR title, LPCWSTR defaultExt, std::wstring* outPath);
	// 原生对话框必须在持有其 HWND 的线程上创建。编辑器的更新运行在渲染线程，
	// 而 Win32 主窗口属于消息线程，因此这些辅助函数会同步编排到该线程。
	int ShowMessageBox(HWND hWnd, LPCWSTR text, LPCWSTR title, UINT type);
	bool TryHandleNativeDialogWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* outResult);
	/* --------------------- */

	bool RunFileDialogOnCurrentThread(HWND hWnd,
		LPCWSTR dir, LPCWSTR filter, LPCWSTR title, LPCWSTR defaultExt,
		bool isSaveDialog, std::wstring* outPath);
	bool DispatchNativeDialogRequest(HWND hWnd, NativeDialogRequest* request);
}
