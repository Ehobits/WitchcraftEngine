#include "ScriptingComponent.h"
#include "String/SStringUtils.h"

void ScriptingComponent::AddScript(const wchar_t* path)
{
	//std::filesystem::path file(path);
	//if (file.extension().wstring() != LUA)
	//	return;

	//for (size_t i = 0; i < scripts.size(); i++)
	//	if (scripts[i].fileName == file.stem().wstring())
	//		return;

	//ScriptBuffer scriptBuffer;

	//sol::load_result result = scriptingSystem.GetState().load_file(SString::WstringToUTF8(path).c_str());
	//if (!result.valid())
	//{
	//	sol::error error = result;
	//	//consoleWindow->AddErrorMessage(L"%s", error.what());
	//	scriptBuffer.error = true;
	//}
	//else scriptBuffer.error = false;

	//{
	//	scriptBuffer.filePath = path;
	//	scriptBuffer.fileName = file.stem().wstring();
	//	scriptBuffer.fileNameToUpper = scriptBuffer.fileName;
	//	//std::transform(
	//	//	scriptBuffer.fileNameToUpper.begin(),
	//	//	scriptBuffer.fileNameToUpper.end(),
	//	//	scriptBuffer.fileNameToUpper.begin(),
	//	//	[](BYTE c) { return std::toupper(c); });
	//}
	//scripts.push_back(scriptBuffer);
}

void ScriptingComponent::lua_call_start()
{
	//lua_add_entity_from_component();

	//for (size_t i = 0; i < scripts.size(); i++)
	//{
	//	if (!scripts[i].activeComponent) continue;
	//	if (scripts[i].error) continue;

	//	std::wstring buffer = scripts[i].fileName;
	//	sol::function function = scriptingSystem.GetState()[buffer.c_str()]["Start"];
	//	if (function)
	//	{
	//		sol::protected_function_result result = function();
	//		if (!result.valid())
	//		{
	//			sol::error error = result;
	//			//consoleWindow->AddErrorMessage(L"%s", error.what());
	//			//game->StopGame();
	//		}
	//	}
	//}
}

void ScriptingComponent::lua_call_update()
{
	//lua_add_entity_from_component();

	//for (size_t i = 0; i < scripts.size(); i++)
	//{
	//	if (!scripts[i].activeComponent) continue;
	//	if (scripts[i].error) continue;

	//	std::wstring buffer = scripts[i].fileName;
	//	sol::function function = scriptingSystem.GetState()[buffer.c_str()]["Update"];
	//	if (function)
	//	{
	//		sol::protected_function_result result = function();
	//		if (!result.valid())
	//		{
	//			sol::error error = result;
	//			//consoleWindow->AddErrorMessage(L"%s", error.what());
	//			//game->StopGame();
	//		}
	//	}
	//}
}

void ScriptingComponent::RecompileScripts()
{
	//for (size_t i = 0; i < scripts.size(); i++)
	//{
	//	sol::load_result result = scriptingSystem.GetState().load_file(SString::WstringToUTF8(scripts[i].filePath).c_str());
	//	if (!result.valid())
	//	{
	//		sol::error error = result;
	//		//consoleWindow->AddErrorMessage(L"%s", error.what());
	//		scripts[i].error = true;
	//	}
	//	else scripts[i].error = false;
	//}
}

void ScriptingComponent::lua_add_entity_from_component()
{
	//entt::entity entity = entt::to_entity(ComponentServices->registry, *this);
	//EntityX entityX; entityX.entity = entity;
	//scriptingSystem.GetState().set("entity", entityX);
}
