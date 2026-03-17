#pragma once

#include "Engine/EngineUtils.h"
#include "../D3DWindow/D3DWindow.h"

class ProjectSceneSystem
{
public:
	void Init(D3DWindow* dx);
	void NewScene(std::wstring _name);
	void OpenScene();
	void SaveScene();
	void OpenProject();
	void SaveProject();
	void ClearScene(std::wstring _name);

	std::wstring GetSceneNmae();

private:
	std::wstring sceneName;

private:
	D3DWindow* m_dx = nullptr;
};
