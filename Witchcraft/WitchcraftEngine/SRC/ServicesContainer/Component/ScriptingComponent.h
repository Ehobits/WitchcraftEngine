#pragma once

#include <Windows.h>

#include "System/ScriptingSystem.h"
#include "BaseComponent.h"

// 脚本模块
struct ScriptingComponent : public BaseComponent
{
public:
	void AddScript(const wchar_t* path);
	void RecompileScripts();

public:
	void lua_call_start();
	void lua_call_update();

public:
	ComponentType GetComponentType() { return mComponentType; }

private:
	void lua_add_entity_from_component();

public:
	ComponentType mComponentType = ComponentType::Co_Scripting;

	std::vector<ScriptBuffer> scripts;
};