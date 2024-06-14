#pragma once

#include "MathHelpers.h"

#define MOVE_NOT_SPECIFIDE 0
#define MOVE_UP 1
#define MOVE_DOWN 2
#define MOVE_LEFT 4
#define MOVE_RIGHT 7
#define MOVE_DEEPEN 10
#define MOVE_FROMAW 15

namespace EngineHelpers
{
	RECT GetClientRect(HWND hWnd);
	UINT GetDisplayWidth();
	UINT GetDisplayHeight();
	UINT GetContextWidth(HWND hWnd);
	UINT GetContextHeight(HWND hWnd);
	void AddLog(const wchar_t* text, ...);
	/* --------------------- */
	const wchar_t* OpenFileDialog(HWND hWnd, LPCWSTR dir, LPCWSTR filter, LPCWSTR title);
	/* --------------------- */

}