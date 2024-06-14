#pragma once

//#define SOL_ALL_SAFETIES_ON 1
#define SOL_PRINT_ERRORS 1
//#define SOL_EXCEPTIONS 1
#include <sol/sol.hpp>

#include "Engine/EngineUtils.h"
#include "ServicesContainer/ServicesContainer.h"

struct EntityX
{
	void CreateEntity();
	void AddComponent(const char* component_name);
	sol::object GetComponent(const char* component_name);
	bool HasComponent(const char* component_name);
	void RemoveComponent(const char* component_name);
};

class ScriptingSystem
{
public:
	bool Init(ServicesContainer* ComponentServices);
	sol::state& GetState();
	void CreateScript(const wchar_t* filename, const wchar_t* name);

private:
	sol::state lua;

private:
	/* system */
	void lua_add_console();
	void lua_add_time();
	void lua_add_input();
	void lua_add_bounding_box();

	/* entity */
	void lua_add_entity();
	void lua_add_general_component();
	void lua_add_transform_component();
	void lua_add_camera_component();
	void lua_add_rigidbody_component();
	void lua_add_mesh_component();
};

struct ScriptBuffer
{
public:
	std::wstring filePath;
	std::wstring fileName;
	std::wstring fileNameToUpper; /* for imgui */

public:
	bool activeComponent = true;
	bool error = false;
};
