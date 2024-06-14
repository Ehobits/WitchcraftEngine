#pragma once

#include <xstring>
#include <Windows.h>
#include <imgui.h>

#include "D3DWindow/D3DWindow.h"
#include "ENTITY/Entity.h"

enum GameState : BYTE
{
	GameNone = 0x00,
	GamePlay = 0x01,
	GameStop = 0x02,
};

class Game
{
private:
	std::wstring name = L"StarGame";
	int width = 1920; /* 1920 */
	int height = 1060; /* 800 */
	//UINT antialiasing = 8;

private:
	HWND hwnd = nullptr;
//public:
//	IDXGISwapChain* gameSwapChain = NULL;
//private:
//	ID3D11RenderTargetView* gameRenderTargetView = NULL;
//	ID3D11DepthStencilView* gameDepthStencilView = NULL;

public:
	bool StartGame(D3DWindow* dx, Entity* ecs, HWND parent);
	void StopGame();
	BYTE GetGameState();
	bool GameResizeBuffer();

public:
	//D3D11_VIEWPORT GetViewport();
	//ID3D11RenderTargetView* GetRenderTargetView();
	//ID3D11DepthStencilView* GetDepthStencilView();
	//IDXGISwapChain* GetSwapChain();

private:
	//bool GameCreateWindow();
	//bool GameCreateContext(HWND parent);
	//bool GameCreateRenderTargetView();
	//bool GameCreateDepthStencilView();

public:
	UINT GameGetContextWidth();
	UINT GameGetContextHeight();

private:
	BYTE gameState = GameState::GameNone;
	D3DWindow* m_dx = nullptr;
	Entity* m_ecs = nullptr;

public:
	void HideCursor(bool value);
	bool isCursorLocked = false;
	void LockCursor(bool value);
	bool IsCursorHidden();
	bool IsCursorLocked();

private:
	LARGE_INTEGER frequency;
	LARGE_INTEGER t1, t2;
	float frameTime, deltaTime, elapsedTime;
	unsigned int frameCount;

	bool timeState = false;

public:
	void StartTime();
	void StopTime();
	void ResetTime();

public:
	void InitTime();
	void BeginTime();
	void EndTime();

public:
	float GetFrameTime();
	float GetDeltaTime();
	float GetElapsedTime();
	unsigned int GetFrameCount();

public:
	bool hide_window = false;
	void SetWindowState(unsigned int state);
};
