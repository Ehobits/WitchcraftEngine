#include "ProjectSceneSystem.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"

#include <fstream>

void ProjectSceneSystem::Init(D3DWindow* dx)
{
	m_dx = dx;
}

void ProjectSceneSystem::ClearScene(std::wstring _name)
{
	sceneName = _name;
}

std::wstring ProjectSceneSystem::GetSceneNmae()
{
	return sceneName;
}

void ProjectSceneSystem::NewScene(std::wstring _name)
{
	ClearScene(_name);
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
