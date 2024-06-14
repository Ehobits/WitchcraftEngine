#pragma once

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "AssetsWindow.h"
#include "ServicesContainer/ServicesContainer.h"
#include "SYSTEM/ScriptingSystem.h"
#include "SYSTEM/PhysicsSystem.h"

class D3DWindow;

class InspectorWindow
{
public:
	void Init(ServicesContainer* ComponentServices, D3DWindow* dx, AssetsWindow* assetsWindow, PhysicsSystem* physicsSystem);
	void Render();

	void NeedRender(bool render);

private:
	bool renderInspector = true;

	bool _Enabled = true;
	bool _Static = false;

	ServicesContainer* m_ComponentServices = nullptr;
	D3DWindow* m_dx = nullptr;
	AssetsWindow* m_assetsWindow = nullptr;
	PhysicsSystem* m_physicsSystem = nullptr;
private:
	void RenderAdd();

	void UpdateComponent();
	void RenderComponent();
};
