#pragma once

#include "Engine/EngineUtils.h"
#include "ServicesContainer/ServicesContainer.h"

class CameraComponent;

class ProjectSceneSystem
{
public:
	void Init(D3DWindow* dx, ServicesContainer* ComponentServices);
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
	ServicesContainer* m_ComponentServices = nullptr;

	CameraComponent* camera = nullptr;
};
