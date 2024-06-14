#include "ProjectSceneSystem.h"
#include "ServicesContainer/COMPONENT/GeneralComponent.h"
#include "ServicesContainer/COMPONENT/TransformComponent.h"
#include "ServicesContainer/COMPONENT/MeshComponent.h"
#include "ServicesContainer/COMPONENT/CameraComponent.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"

#include <fstream>

void ProjectSceneSystem::Init(D3DWindow* dx, ServicesContainer* ComponentServices)
{
	m_dx = dx;
	m_ComponentServices = ComponentServices;
}

void ProjectSceneSystem::ClearScene(std::wstring _name)
{
	m_ComponentServices->RemoveAll();

	sceneName = _name;
	camera = nullptr;
}

std::wstring ProjectSceneSystem::GetSceneNmae()
{
	return sceneName;
}

void ProjectSceneSystem::NewScene(std::wstring _name)
{
	ClearScene(_name);

	camera = new CameraComponent();
	camera->SetDXWindow(m_dx);
	//entt::entity cube = m_ComponentServices->CreateEntity();
	//m_ComponentServices->CreateCubeEntity(cube);
	//m_ComponentServices->selected = cube;
	m_ComponentServices->AddService(L"主要相机", camera);
}

void ProjectSceneSystem::SaveScene()
{

}

void ProjectSceneSystem::OpenProject()
{
	EngineHelpers::OpenFileDialog(m_dx->GethWnd(), L"", L"", L"打开项目");
}

void ProjectSceneSystem::SaveProject()
{
}

void ProjectSceneSystem::OpenScene()
{
	EngineHelpers::OpenFileDialog(m_dx->GethWnd(), L"", L"", L"打开场景");
}
