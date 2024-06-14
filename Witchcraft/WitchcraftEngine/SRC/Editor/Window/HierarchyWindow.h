#pragma once

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "AssetsWindow.h"
#include "ConsoleWindow.h"
#include "ServicesContainer/ServicesContainer.h"
#include "ModelAnalysis/AssimpLoader.h"

class HierarchyWindow
{
public:
	void Init(ConsoleWindow* consoleWindow, AssimpLoader* assimpLoader, ServicesContainer* ComponentServices);
	void Render();

	bool CreateComponentWindow(bool* pOpen, std::wstring* name, Transform* transform = nullptr);

	void NeedRender(bool render);
private:
	void RenderTree();

private:
	bool renderHierarchy = true;
	bool openme = false;
	bool openCreateWindow = false;
	std::wstring name = L"";

	ConsoleWindow* m_consoleWindow = nullptr;
	AssimpLoader* m_assimpLoader = nullptr;
	ServicesContainer* m_ComponentServices = nullptr;

};
