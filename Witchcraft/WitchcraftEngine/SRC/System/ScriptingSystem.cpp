#include "ScriptingSystem.h"
#include "HELPERS/Helpers.h"
#include "Common/BillboardSharedTypes.h"
#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/BillboardComponent.h"
#include "ECS/COMPONENT/BoneAttachmentComponent.h"
#include "ECS/COMPONENT/LightComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"
#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
#include "ECS/COMPONENT/ScriptingComponent.h"
#include "ECS/COMPONENT/SkeletonComponent.h"
#include "ECS/COMPONENT/SkinnedMeshComponent.h"
#include "ECS/Component/AnimatorComponent.h"
#include "ECS/WitchcraECS.h"
#include "System/Animation/AnimationPlaybackController.h"
#include "String/SStringUtils.h"
#include "Assets.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <vector>
#include <cwctype>

namespace ScriptingSystemDetail
{
	std::string ToUtf8(const std::wstring& value)
	{
		return SString::WstringToUTF8(value);
	}

	std::wstring ToWstring(const std::string& value)
	{
		return SString::UTF8ToWstring(value);
	}

	std::wstring ToLowerPathText(std::wstring text)
	{
		std::transform(text.begin(), text.end(), text.begin(),
			[](wchar_t ch)
			{
				return static_cast<wchar_t>(std::towlower(ch));
			});
		return text;
	}

	bool PathStartsWithAssetsRoot(const std::filesystem::path& path)
	{
		auto it = path.begin();
		if (it == path.end())
			return false;

		const std::wstring firstPart = ToLowerPathText(it->wstring());
		return firstPart == L"assets" || firstPart == L"asset";
	}

	std::filesystem::path StripAssetsRootPrefix(const std::filesystem::path& path)
	{
		if (!PathStartsWithAssetsRoot(path))
			return path;

		std::filesystem::path strippedPath;
		auto it = path.begin();
		++it;
		for (; it != path.end(); ++it)
			strippedPath /= *it;
		return strippedPath;
	}

	bool TryExistingPath(const std::filesystem::path& candidatePath, std::filesystem::path* outPath)
	{
		if (candidatePath.empty())
			return false;

		std::error_code existsError;
		if (!std::filesystem::exists(candidatePath, existsError))
			return false;

		if (outPath != nullptr)
			*outPath = candidatePath.lexically_normal();
		return true;
	}


	std::filesystem::path ResolveRuntimeScriptPath(const std::wstring& scriptPath)
	{
		if (scriptPath.empty())
			return {};

		const std::filesystem::path originalPath(scriptPath);
		std::filesystem::path resolvedPath;
		if (TryExistingPath(originalPath, &resolvedPath))
			return resolvedPath;

		if (originalPath.is_absolute())
			return originalPath.lexically_normal();

		const std::filesystem::path projectRoot = std::filesystem::path(EngineUtils::GetProjectDirPath());
		if (!projectRoot.empty())
		{
			const std::vector<std::filesystem::path> projectRelativeCandidates =
			{
				originalPath,
				StripAssetsRootPrefix(originalPath)
			};

			for (const std::filesystem::path& relativeCandidate : projectRelativeCandidates)
			{
				if (TryExistingPath(projectRoot / relativeCandidate, &resolvedPath))
					return resolvedPath;
			}
		}

		const std::filesystem::path currentRelativeCandidate = std::filesystem::current_path() / originalPath;
		if (TryExistingPath(currentRelativeCandidate, &resolvedPath))
			return resolvedPath;

		return originalPath.lexically_normal();
	}
}

bool ScriptingSystem::Init()
{
	lua.open_libraries(sol::lib::base);
	lua.open_libraries(sol::lib::package);
	lua.open_libraries(sol::lib::coroutine);
	lua.open_libraries(sol::lib::string);
	lua.open_libraries(sol::lib::os);
	lua.open_libraries(sol::lib::math);
	lua.open_libraries(sol::lib::table);
	lua.open_libraries(sol::lib::debug);
	lua.open_libraries(sol::lib::bit32);
	lua.open_libraries(sol::lib::io);

	//luaopen_socket_core(lua.lua_state());

	/* system */
	lua_add_console();
	lua_add_time();
	lua_add_input();
	lua_add_bounding_box();
	lua_add_entity();

	/* entity */
	lua_add_general_component();
	lua_add_transform_component();
	lua_add_camera_component();
	lua_add_light_component();
	lua_add_billboard_component();
	lua_add_bone_attachment_component();
	lua_add_mesh_component();
	lua_add_physics_component();
	lua_add_rigidbody_component();
	lua_add_scripting_component();
	lua_add_skeleton_component();
	lua_add_skinned_mesh_component();

	return true;
}

bool ScriptingSystem::StartRuntime(WitchcraECS* ecs)
{
	mRuntimeScripts.clear();
	mRuntimeCommandBuffer.clear();
	mRuntimeDeltaTime = 0.0f;
	mRuntimeElapsedTime = 0.0f;
	mRuntimeFrameCount = 0;
	if (ecs == nullptr)
	{
		mRuntimeActive = false;
		mRuntimeEcs = nullptr;
		mRuntimeStats = ScriptingRuntimeStats{};
		mRuntimeCommandBuffer.clear();
		return false;
	}

	mRuntimeActive = true;
	mRuntimeEcs = ecs;
	ScanRuntimeScripts(ecs);
	BuildRuntimeScriptInstances(ecs);
	CallRuntimeStartCallbacks();
	FlushRuntimeCommands(ecs);
	RefreshRuntimeInstanceStats();
	return true;
}

void ScriptingSystem::UpdateRuntime(WitchcraECS* ecs, float deltaTime)
{
	if (!mRuntimeActive)
		return;
	mRuntimeDeltaTime = deltaTime;
	mRuntimeElapsedTime += deltaTime;
	++mRuntimeFrameCount;
	CallRuntimeUpdateCallbacks(deltaTime);
	FlushRuntimeCommands(ecs);
	RefreshRuntimeInstanceStats();
}

void ScriptingSystem::StopRuntime(WitchcraECS* ecs)
{
	(void)ecs;
	mRuntimeActive = false;
	mRuntimeEcs = nullptr;
	mRuntimeStats = ScriptingRuntimeStats{};
	mRuntimeDeltaTime = 0.0f;
	mRuntimeElapsedTime = 0.0f;
	mRuntimeFrameCount = 0;
	mRuntimeScripts.clear();
	mRuntimeCommandBuffer.clear();
}

bool ScriptingSystem::IsRuntimeActive() const
{
	return mRuntimeActive;
}

const ScriptingRuntimeStats& ScriptingSystem::GetRuntimeStats() const
{
	return mRuntimeStats;
}

void ScriptingSystem::ScanRuntimeScripts(WitchcraECS* ecs)
{
	mRuntimeStats = ScriptingRuntimeStats{};
	if (ecs == nullptr)
		return;

	for (SceneEntityBase* rootEntity : ecs->GetSceneRootEntities())
		ScanRuntimeScriptsRecursive(ecs, rootEntity);
}

void ScriptingSystem::ScanRuntimeScriptsRecursive(WitchcraECS* ecs, SceneEntityBase* entity)
{
	if (ecs == nullptr || entity == nullptr)
		return;
	if (ecs->IsEnvironmentEntity(entity))
	{
		for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
			ScanRuntimeScriptsRecursive(ecs, childEntity);
		return;
	}

	EntityScriptingComponentData snapshot;
	if (ecs->GetEntityScriptingSnapshot(entity, &snapshot))
	{
		++mRuntimeStats.HostEntityCount;
		mRuntimeStats.ScriptEntryCount += static_cast<std::uint32_t>(snapshot.scripts.size());
		for (const EntityScriptingComponentData::ScriptSnapshot& script : snapshot.scripts)
		{
			if (script.activeComponent && !script.error)
				++mRuntimeStats.ActiveScriptEntryCount;
		}
	}

	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
		ScanRuntimeScriptsRecursive(ecs, childEntity);
}

void ScriptingSystem::BuildRuntimeScriptInstances(WitchcraECS* ecs)
{
	mRuntimeScripts.clear();
	if (ecs == nullptr)
		return;

	for (SceneEntityBase* rootEntity : ecs->GetSceneRootEntities())
		BuildRuntimeScriptInstancesRecursive(ecs, rootEntity);
}

void ScriptingSystem::BuildRuntimeScriptInstancesRecursive(WitchcraECS* ecs, SceneEntityBase* entity)
{
	if (ecs == nullptr || entity == nullptr)
		return;
	if (ecs->IsEnvironmentEntity(entity))
	{
		for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
			BuildRuntimeScriptInstancesRecursive(ecs, childEntity);
		return;
	}

	EntityScriptingComponentData snapshot;
	if (ecs->GetEntityScriptingSnapshot(entity, &snapshot))
	{
		for (std::size_t scriptIndex = 0; scriptIndex < snapshot.scripts.size(); ++scriptIndex)
		{
			const EntityScriptingComponentData::ScriptSnapshot& script = snapshot.scripts[scriptIndex];
			if (script.activeComponent && !script.error && !script.filePath.empty())
				AddRuntimeScriptInstance(entity, scriptIndex, script.filePath);
		}
	}

	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
		BuildRuntimeScriptInstancesRecursive(ecs, childEntity);
}

void ScriptingSystem::RefreshRuntimeInstanceStats()
{
	mRuntimeStats.RuntimeInstanceCount = static_cast<std::uint32_t>(mRuntimeScripts.size());
	mRuntimeStats.ErrorInstanceCount = 0;
	for (const RuntimeScriptInstance& runtimeScript : mRuntimeScripts)
	{
		if (runtimeScript.Error)
			++mRuntimeStats.ErrorInstanceCount;
	}
}

void ScriptingSystem::RecordRuntimeScriptError(RuntimeScriptInstance& runtimeScript, const std::wstring& message)
{
	runtimeScript.Error = true;
	++mRuntimeStats.ErrorRevision;
	mRuntimeStats.LastErrorScriptPath = runtimeScript.ScriptPath;
	mRuntimeStats.LastErrorMessage = message;
}

bool ScriptingSystem::AddRuntimeScriptInstance(SceneEntityBase* entity, std::size_t scriptIndex, const std::wstring& scriptPath)
{
	RuntimeScriptInstance runtimeScript;
	runtimeScript.OwnerEntity = entity;
	runtimeScript.ScriptIndex = scriptIndex;
	runtimeScript.ScriptPath = scriptPath;

	const std::filesystem::path filePath = ScriptingSystemDetail::ResolveRuntimeScriptPath(scriptPath);
	if (!std::filesystem::exists(filePath))
	{
		RecordRuntimeScriptError(runtimeScript, L"脚本文件不存在：" + filePath.wstring());
		mRuntimeScripts.push_back(runtimeScript);
		return false;
	}

	std::string scriptSource;
	std::ifstream inputFile(filePath, std::ios::binary);
	if (!inputFile)
	{
		RecordRuntimeScriptError(runtimeScript, L"脚本文件无法读取：" + filePath.wstring());
		mRuntimeScripts.push_back(runtimeScript);
		return false;
	}
	std::ostringstream sourceStream;
	sourceStream << inputFile.rdbuf();
	scriptSource = sourceStream.str();
	const std::string chunkName = "@" + SString::WstringToUTF8(filePath.wstring());
	sol::load_result loadResult = lua.load(scriptSource, chunkName);
	if (!loadResult.valid())
	{
		sol::error error = loadResult;
		RecordRuntimeScriptError(runtimeScript, std::wstring(L"脚本加载失败：") + SString::UTF8ToWstring(error.what()));
		mRuntimeScripts.push_back(runtimeScript);
		return false;
	}

	sol::protected_function chunk = loadResult;
	sol::protected_function_result chunkResult = chunk();
	if (!chunkResult.valid())
	{
		sol::error error = chunkResult;
		RecordRuntimeScriptError(runtimeScript, std::wstring(L"脚本执行失败：") + SString::UTF8ToWstring(error.what()));
		mRuntimeScripts.push_back(runtimeScript);
		return false;
	}

	sol::object returnedObject = chunkResult.get<sol::object>();
	if (returnedObject.get_type() != sol::type::table)
	{
		RecordRuntimeScriptError(runtimeScript, L"脚本必须返回 table。");
		mRuntimeScripts.push_back(runtimeScript);
		return false;
	}

	runtimeScript.InstanceTable = returnedObject.as<sol::table>();
	runtimeScript.Error = false;
	mRuntimeScripts.push_back(runtimeScript);
	return true;
}

void ScriptingSystem::CallRuntimeStartCallbacks()
{
	for (RuntimeScriptInstance& runtimeScript : mRuntimeScripts)
	{
		if (runtimeScript.Error || runtimeScript.Started || !runtimeScript.InstanceTable.has_value())
			continue;

		sol::object callbackObject = runtimeScript.InstanceTable->raw_get<sol::object>("OnStart");
		if (callbackObject.get_type() != sol::type::function)
		{
			runtimeScript.Started = true;
			continue;
		}

		sol::protected_function callback = callbackObject.as<sol::protected_function>();
		sol::protected_function_result result = callback(ScriptEntityRef{ runtimeScript.OwnerEntity, mRuntimeEcs, this });
		if (!result.valid())
		{
			sol::error error = result;
			RecordRuntimeScriptError(runtimeScript, std::wstring(L"OnStart 失败：") + SString::UTF8ToWstring(error.what()));
		}
		runtimeScript.Started = true;
	}
}

void ScriptingSystem::CallRuntimeUpdateCallbacks(float deltaTime)
{
	for (RuntimeScriptInstance& runtimeScript : mRuntimeScripts)
	{
		if (runtimeScript.Error || !runtimeScript.Started || !runtimeScript.InstanceTable.has_value())
			continue;

		sol::object callbackObject = runtimeScript.InstanceTable->raw_get<sol::object>("OnUpdate");
		if (callbackObject.get_type() != sol::type::function)
			continue;

		sol::protected_function callback = callbackObject.as<sol::protected_function>();
		sol::protected_function_result result = callback(ScriptEntityRef{ runtimeScript.OwnerEntity, mRuntimeEcs, this }, deltaTime);
		if (!result.valid())
		{
			sol::error error = result;
			RecordRuntimeScriptError(runtimeScript, std::wstring(L"OnUpdate 失败：") + SString::UTF8ToWstring(error.what()));
		}
	}
}

void ScriptingSystem::FlushRuntimeCommands(WitchcraECS* ecs)
{
	WitchcraECS* targetEcs = ecs != nullptr ? ecs : mRuntimeEcs;
	if (!mRuntimeActive || targetEcs == nullptr || mRuntimeCommandBuffer.empty())
		return;

	std::vector<RuntimeCommand> commands = std::move(mRuntimeCommandBuffer);
	mRuntimeCommandBuffer.clear();
	for (const RuntimeCommand& command : commands)
		(void)ExecuteRuntimeCommand(targetEcs, command);

	mRuntimeScripts.erase(
		std::remove_if(
			mRuntimeScripts.begin(),
			mRuntimeScripts.end(),
			[targetEcs](const RuntimeScriptInstance& runtimeScript)
			{
				if (runtimeScript.OwnerEntity == nullptr || !targetEcs->HasEntity(runtimeScript.OwnerEntity))
					return true;

				EntityScriptingComponentData snapshot;
				return !targetEcs->GetEntityScriptingSnapshot(runtimeScript.OwnerEntity, &snapshot);
			}),
		mRuntimeScripts.end());
}

bool ScriptingSystem::QueueCreateChild(SceneEntityBase* parentEntity, const std::wstring& name)
{
	if (!mRuntimeActive || mRuntimeEcs == nullptr || parentEntity == nullptr || name.empty())
		return false;
	if (!mRuntimeEcs->HasEntity(parentEntity))
		return false;

	RuntimeCommand command;
	command.CommandType = RuntimeCommand::Type::CreateChild;
	command.TargetEntity = parentEntity;
	command.Name = name;
	mRuntimeCommandBuffer.push_back(std::move(command));
	return true;
}

bool ScriptingSystem::QueueDestroyEntity(SceneEntityBase* entity, bool destroyChildren)
{
	if (!mRuntimeActive || mRuntimeEcs == nullptr || entity == nullptr)
		return false;
	if (!mRuntimeEcs->HasEntity(entity))
		return false;

	RuntimeCommand command;
	command.CommandType = RuntimeCommand::Type::DestroyEntity;
	command.TargetEntity = entity;
	command.DestroyChildren = destroyChildren;
	mRuntimeCommandBuffer.push_back(std::move(command));
	return true;
}

bool ScriptingSystem::QueueAddComponent(SceneEntityBase* entity, const std::wstring& componentName)
{
	if (!mRuntimeActive || mRuntimeEcs == nullptr || entity == nullptr || componentName.empty())
		return false;
	if (!mRuntimeEcs->HasEntity(entity))
		return false;

	RuntimeCommand command;
	command.CommandType = RuntimeCommand::Type::AddComponent;
	command.TargetEntity = entity;
	command.ComponentName = componentName;
	mRuntimeCommandBuffer.push_back(std::move(command));
	return true;
}

bool ScriptingSystem::QueueRemoveComponent(SceneEntityBase* entity, const std::wstring& componentName)
{
	if (!mRuntimeActive || mRuntimeEcs == nullptr || entity == nullptr || componentName.empty())
		return false;
	if (!mRuntimeEcs->HasEntity(entity))
		return false;

	RuntimeCommand command;
	command.CommandType = RuntimeCommand::Type::RemoveComponent;
	command.TargetEntity = entity;
	command.ComponentName = componentName;
	mRuntimeCommandBuffer.push_back(std::move(command));
	return true;
}

bool ScriptingSystem::ExecuteRuntimeCommand(WitchcraECS* ecs, const RuntimeCommand& command)
{
	if (ecs == nullptr)
		return false;

	auto isComponentName = [](const std::wstring& name, const wchar_t* canonicalName, const wchar_t* shortName)
	{
		return name == canonicalName || name == shortName;
	};

	switch (command.CommandType)
	{
	case RuntimeCommand::Type::CreateChild:
	{
		if (command.TargetEntity == nullptr || command.Name.empty() || !ecs->HasEntity(command.TargetEntity))
			return false;
		const std::wstring uniqueName = ecs->GetUniqueEntityName(command.Name, command.TargetEntity);
		return ecs->CreateBasicEntity(uniqueName, command.TargetEntity, ComponentType::Co_Unk) != nullptr;
	}
	case RuntimeCommand::Type::DestroyEntity:
		if (command.TargetEntity == nullptr || !ecs->HasEntity(command.TargetEntity))
			return false;
		return ecs->DestroyEntity(command.TargetEntity, command.DestroyChildren);
	case RuntimeCommand::Type::AddComponent:
		if (command.TargetEntity == nullptr || !ecs->HasEntity(command.TargetEntity))
			return false;
		if (isComponentName(command.ComponentName, L"CameraComponent", L"Camera"))
			return ecs->AddCameraComponent(command.TargetEntity) != nullptr;
		if (isComponentName(command.ComponentName, L"PhysicsComponent", L"Physics"))
			return ecs->AddPhysicsComponent(command.TargetEntity) != nullptr;
		if (isComponentName(command.ComponentName, L"RigidbodyComponent", L"Rigidbody"))
			return ecs->AddRigidbodyComponent(command.TargetEntity) != nullptr;
		if (isComponentName(command.ComponentName, L"ScriptingComponent", L"Scripting"))
			return ecs->AddScriptingComponent(command.TargetEntity) != nullptr;
		if (isComponentName(command.ComponentName, L"AnimatorComponent", L"Animator"))
			return ecs->AddAnimatorComponent(command.TargetEntity) != nullptr;
		return false;
	case RuntimeCommand::Type::RemoveComponent:
		if (command.TargetEntity == nullptr || !ecs->HasEntity(command.TargetEntity))
			return false;
		if (isComponentName(command.ComponentName, L"CameraComponent", L"Camera"))
			return ecs->RemoveCameraComponent(command.TargetEntity);
		if (isComponentName(command.ComponentName, L"PhysicsComponent", L"Physics"))
			return ecs->RemovePhysicsComponent(command.TargetEntity);
		if (isComponentName(command.ComponentName, L"RigidbodyComponent", L"Rigidbody"))
			return ecs->RemoveRigidbodyComponent(command.TargetEntity);
		if (isComponentName(command.ComponentName, L"ScriptingComponent", L"Scripting"))
			return ecs->RemoveScriptingComponent(command.TargetEntity);
		if (isComponentName(command.ComponentName, L"AnimatorComponent", L"Animator"))
			return ecs->RemoveAnimatorComponent(command.TargetEntity);
		return false;
	default:
		return false;
	}
}

void ScriptingSystem::NotifyAnimationEvent(const AnimationScriptEventNotification& notification)
{
	if (!mRuntimeActive || mRuntimeEcs == nullptr || notification.OwnerEntity == nullptr)
		return;

	const std::string fallbackCallback = "OnAnimationEvent";
	const std::string callbackName = notification.ScriptCallbackName.empty()
		? fallbackCallback
		: SString::WstringToUTF8(notification.ScriptCallbackName);
	if (callbackName.empty())
		return;

	for (RuntimeScriptInstance& runtimeScript : mRuntimeScripts)
	{
		if (runtimeScript.Error || !runtimeScript.Started || !runtimeScript.InstanceTable.has_value())
			continue;
		if (runtimeScript.OwnerEntity != notification.OwnerEntity)
			continue;

		sol::object callbackObject = runtimeScript.InstanceTable->raw_get<sol::object>(callbackName);
		if (callbackObject.get_type() != sol::type::function)
			continue;

		sol::table eventTable = lua.create_table();
		eventTable["name"] = SString::WstringToUTF8(notification.EventName);
		eventTable["parameter"] = SString::WstringToUTF8(notification.Parameter);
		eventTable["scriptCallback"] = SString::WstringToUTF8(notification.ScriptCallbackName);
		eventTable["clip"] = SString::WstringToUTF8(notification.ClipAssetPath);
		eventTable["layer"] = SString::WstringToUTF8(notification.LayerName);
		eventTable["eventTime"] = notification.EventTime;
		eventTable["playbackTime"] = notification.PlaybackTime;

		sol::protected_function callback = callbackObject.as<sol::protected_function>();
		sol::protected_function_result result = callback(ScriptEntityRef{ runtimeScript.OwnerEntity, mRuntimeEcs, this }, eventTable);
		if (!result.valid())
		{
			sol::error error = result;
			RecordRuntimeScriptError(runtimeScript, std::wstring(L"动画事件回调失败：") + SString::UTF8ToWstring(error.what()));
		}
	}
	RefreshRuntimeInstanceStats();
}

/* system */
void ScriptingSystem::lua_add_console()
{
	lua["Console"] = sol::new_table();
	//lua["Console"]["Info"]    = [](const wchar_t* message) { consoleWindow->AddInfoMessage(message);    };
	//lua["Console"]["Warning"] = [](const wchar_t* message) { consoleWindow->AddWarningMessage(message); };
	//lua["Console"]["Error"]   = [](const wchar_t* message) { consoleWindow->AddErrorMessage(message);   };
}

void ScriptingSystem::lua_add_time()
{
	lua["Time"] = sol::new_table();
	lua["Time"]["FrameTime"] = [this]() { return mRuntimeDeltaTime; };
	lua["Time"]["DeltaTime"] = [this]() { return mRuntimeDeltaTime; };
	lua["Time"]["ElapsedTime"] = [this]() { return mRuntimeElapsedTime; };
	lua["Time"]["FrameCount"] = [this]() { return mRuntimeFrameCount; };
}

void ScriptingSystem::lua_add_input()
{
	lua["Input"] = sol::new_table();
	lua["Input"]["MouseButton"] = sol::new_table();
	lua["Input"]["MouseButton"]["Left"] = VK_LBUTTON;
	lua["Input"]["MouseButton"]["Right"] = VK_RBUTTON;
	lua["Input"]["MouseButton"]["Middle"] = VK_MBUTTON;

	lua["Input"]["KeyCode"] = sol::new_table();
	lua["Input"]["KeyCode"]["A"] = 'A';
	lua["Input"]["KeyCode"]["B"] = 'B';
	lua["Input"]["KeyCode"]["C"] = 'C';
	lua["Input"]["KeyCode"]["D"] = 'D';
	lua["Input"]["KeyCode"]["E"] = 'E';
	lua["Input"]["KeyCode"]["F"] = 'F';
	lua["Input"]["KeyCode"]["G"] = 'G';
	lua["Input"]["KeyCode"]["H"] = 'H';
	lua["Input"]["KeyCode"]["I"] = 'I';
	lua["Input"]["KeyCode"]["J"] = 'J';
	lua["Input"]["KeyCode"]["K"] = 'K';
	lua["Input"]["KeyCode"]["L"] = 'L';
	lua["Input"]["KeyCode"]["M"] = 'M';
	lua["Input"]["KeyCode"]["N"] = 'N';
	lua["Input"]["KeyCode"]["O"] = 'O';
	lua["Input"]["KeyCode"]["P"] = 'P';
	lua["Input"]["KeyCode"]["Q"] = 'Q';
	lua["Input"]["KeyCode"]["R"] = 'R';
	lua["Input"]["KeyCode"]["S"] = 'S';
	lua["Input"]["KeyCode"]["T"] = 'T';
	lua["Input"]["KeyCode"]["U"] = 'U';
	lua["Input"]["KeyCode"]["V"] = 'V';
	lua["Input"]["KeyCode"]["W"] = 'W';
	lua["Input"]["KeyCode"]["X"] = 'X';
	lua["Input"]["KeyCode"]["Y"] = 'Y';
	lua["Input"]["KeyCode"]["Z"] = 'Z';
	lua["Input"]["KeyCode"]["0"] = '0';
	lua["Input"]["KeyCode"]["1"] = '1';
	lua["Input"]["KeyCode"]["2"] = '2';
	lua["Input"]["KeyCode"]["3"] = '3';
	lua["Input"]["KeyCode"]["4"] = '4';
	lua["Input"]["KeyCode"]["5"] = '5';
	lua["Input"]["KeyCode"]["6"] = '6';
	lua["Input"]["KeyCode"]["7"] = '7';
	lua["Input"]["KeyCode"]["8"] = '8';
	lua["Input"]["KeyCode"]["9"] = '9';
	lua["Input"]["KeyCode"]["F1"] = VK_F1;
	lua["Input"]["KeyCode"]["F2"] = VK_F2;
	lua["Input"]["KeyCode"]["F3"] = VK_F3;
	lua["Input"]["KeyCode"]["F4"] = VK_F4;
	lua["Input"]["KeyCode"]["F5"] = VK_F5;
	lua["Input"]["KeyCode"]["F6"] = VK_F6;
	lua["Input"]["KeyCode"]["F7"] = VK_F7;
	lua["Input"]["KeyCode"]["F8"] = VK_F8;
	lua["Input"]["KeyCode"]["F9"] = VK_F9;
	lua["Input"]["KeyCode"]["F10"] = VK_F10;
	lua["Input"]["KeyCode"]["F11"] = VK_F11;
	lua["Input"]["KeyCode"]["F12"] = VK_F12;
	lua["Input"]["KeyCode"]["Up"] = VK_UP;
	lua["Input"]["KeyCode"]["Down"] = VK_DOWN;
	lua["Input"]["KeyCode"]["Left"] = VK_LEFT;
	lua["Input"]["KeyCode"]["Right"] = VK_RIGHT;
	lua["Input"]["KeyCode"]["LeftControl"] = VK_LCONTROL;
	lua["Input"]["KeyCode"]["RightControl"] = VK_RCONTROL;
	lua["Input"]["KeyCode"]["LeftShift"] = VK_LSHIFT;
	lua["Input"]["KeyCode"]["RightShift"] = VK_RSHIFT;
	lua["Input"]["KeyCode"]["LeftAlt"] = VK_LMENU;
	lua["Input"]["KeyCode"]["RightAlt"] = VK_RMENU;
	lua["Input"]["KeyCode"]["Space"] = VK_SPACE;
	lua["Input"]["KeyCode"]["Enter"] = VK_RETURN;
	lua["Input"]["KeyCode"]["Escape"] = VK_ESCAPE;
	lua["Input"]["KeyCode"]["Tab"] = VK_TAB;
}

void ScriptingSystem::lua_add_bounding_box()
{
	sol::usertype<BoundingBox> boundingBox = lua.new_usertype<BoundingBox>(
		"BoundingBox",
		sol::constructors<BoundingBox(), BoundingBox(DirectX::XMFLOAT3, DirectX::XMFLOAT3)>());
}

/* entity */
void ScriptingSystem::lua_add_entity()
{
	sol::usertype<ScriptVector3> scriptVector3 = lua.new_usertype<ScriptVector3>(
		"ScriptVector3");
	scriptVector3["x"] = &ScriptVector3::X;
	scriptVector3["y"] = &ScriptVector3::Y;
	scriptVector3["z"] = &ScriptVector3::Z;

	sol::usertype<ScriptVector4> scriptVector4 = lua.new_usertype<ScriptVector4>(
		"ScriptVector4");
	scriptVector4["x"] = &ScriptVector4::X;
	scriptVector4["y"] = &ScriptVector4::Y;
	scriptVector4["z"] = &ScriptVector4::Z;
	scriptVector4["w"] = &ScriptVector4::W;

	sol::table sceneEntityType = lua.create_table();
	sceneEntityType["Sky"] = SceneEntityType::Sky;
	sceneEntityType["Ground"] = SceneEntityType::Ground;
	sceneEntityType["StaticScenery"] = SceneEntityType::StaticScenery;
	sceneEntityType["DynamicScenery"] = SceneEntityType::DynamicScenery;
	sceneEntityType["Interactive"] = SceneEntityType::Interactive;
	lua["SceneEntityType"] = sceneEntityType;

	sol::usertype<ScriptTransformRef> scriptTransformRef = lua.new_usertype<ScriptTransformRef>(
		"ScriptTransformRef");
	scriptTransformRef["IsValid"] = &ScriptTransformRef::IsValid;
	scriptTransformRef["GetPosition"] = &ScriptTransformRef::GetPosition;
	scriptTransformRef["SetPosition"] = &ScriptTransformRef::SetPosition;
	scriptTransformRef["GetRotation"] = &ScriptTransformRef::GetRotation;
	scriptTransformRef["SetRotation"] = &ScriptTransformRef::SetRotation;
	scriptTransformRef["GetScale"] = &ScriptTransformRef::GetScale;
	scriptTransformRef["SetScale"] = &ScriptTransformRef::SetScale;

	sol::usertype<ScriptAnimatorRef> scriptAnimatorRef = lua.new_usertype<ScriptAnimatorRef>(
		"ScriptAnimatorRef");
	scriptAnimatorRef["IsValid"] = &ScriptAnimatorRef::IsValid;
	scriptAnimatorRef["Play"] = sol::overload(
		[](const ScriptAnimatorRef& animator, const std::string& clip) { return animator.Play(clip, std::string{}, 0.0f, true); },
		[](const ScriptAnimatorRef& animator, const std::string& clip, const std::string& layer) { return animator.Play(clip, layer, 0.0f, true); },
		[](const ScriptAnimatorRef& animator, const std::string& clip, const std::string& layer, float startTime, bool loop) { return animator.Play(clip, layer, startTime, loop); });
	scriptAnimatorRef["Stop"] = sol::overload(
		[](const ScriptAnimatorRef& animator) { return animator.Stop(std::string{}); },
		[](const ScriptAnimatorRef& animator, const std::string& layer) { return animator.Stop(layer); });
	scriptAnimatorRef["CrossFade"] = sol::overload(
		[](const ScriptAnimatorRef& animator, const std::string& clip, float duration) { return animator.CrossFade(clip, duration, std::string{}, 0.0f, true); },
		[](const ScriptAnimatorRef& animator, const std::string& clip, float duration, const std::string& layer) { return animator.CrossFade(clip, duration, layer, 0.0f, true); },
		[](const ScriptAnimatorRef& animator, const std::string& clip, float duration, const std::string& layer, float startTime, bool loop) { return animator.CrossFade(clip, duration, layer, startTime, loop); });
	scriptAnimatorRef["SetLayerWeight"] = &ScriptAnimatorRef::SetLayerWeight;
	scriptAnimatorRef["SetLayerSpeed"] = &ScriptAnimatorRef::SetLayerSpeed;
	scriptAnimatorRef["SetLayerEnabled"] = &ScriptAnimatorRef::SetLayerEnabled;

	sol::usertype<ScriptEntityRef> scriptEntityRef = lua.new_usertype<ScriptEntityRef>(
		"ScriptEntityRef");
	scriptEntityRef["IsValid"] = &ScriptEntityRef::IsValid;
	scriptEntityRef["GetParent"] = &ScriptEntityRef::GetParent;
	scriptEntityRef["GetName"] = &ScriptEntityRef::GetName;
	scriptEntityRef["SetName"] = &ScriptEntityRef::SetName;
	scriptEntityRef["GetTag"] = &ScriptEntityRef::GetTag;
	scriptEntityRef["SetTag"] = &ScriptEntityRef::SetTag;
	scriptEntityRef["IsVisible"] = &ScriptEntityRef::IsVisible;
	scriptEntityRef["SetVisible"] = &ScriptEntityRef::SetVisible;
	scriptEntityRef["IsStatic"] = &ScriptEntityRef::IsStatic;
	scriptEntityRef["SetStatic"] = &ScriptEntityRef::SetStatic;
	scriptEntityRef["GetSceneType"] = &ScriptEntityRef::GetSceneType;
	scriptEntityRef["SetSceneType"] = &ScriptEntityRef::SetSceneType;
	scriptEntityRef["GetTransform"] = &ScriptEntityRef::GetTransform;
	scriptEntityRef["GetGeneralComponent"] = &ScriptEntityRef::GetGeneralComponent;
	scriptEntityRef["GetLightComponent"] = &ScriptEntityRef::GetLightComponent;
	scriptEntityRef["GetCameraComponent"] = &ScriptEntityRef::GetCameraComponent;
	scriptEntityRef["GetMeshComponent"] = &ScriptEntityRef::GetMeshComponent;
	scriptEntityRef["GetPhysicsComponent"] = &ScriptEntityRef::GetPhysicsComponent;
	scriptEntityRef["GetRigidBodyComponent"] = &ScriptEntityRef::GetRigidBodyComponent;
	scriptEntityRef["GetScriptingComponent"] = &ScriptEntityRef::GetScriptingComponent;
	scriptEntityRef["GetBillboardComponent"] = &ScriptEntityRef::GetBillboardComponent;
	scriptEntityRef["GetBoneAttachmentComponent"] = &ScriptEntityRef::GetBoneAttachmentComponent;
	scriptEntityRef["GetSkeletonComponent"] = &ScriptEntityRef::GetSkeletonComponent;
	scriptEntityRef["GetSkinnedMeshComponent"] = &ScriptEntityRef::GetSkinnedMeshComponent;
	scriptEntityRef["GetAnimator"] = &ScriptEntityRef::GetAnimator;
	scriptEntityRef["AddBoxCollider"] = &ScriptEntityRef::AddBoxCollider;
	scriptEntityRef["AddPlaneCollider"] = &ScriptEntityRef::AddPlaneCollider;
	scriptEntityRef["QueueCreateChild"] = &ScriptEntityRef::QueueCreateChild;
	scriptEntityRef["QueueDestroy"] = sol::overload(
		[](const ScriptEntityRef& entity) { return entity.QueueDestroy(true); },
		[](const ScriptEntityRef& entity, bool destroyChildren) { return entity.QueueDestroy(destroyChildren); });
	scriptEntityRef["QueueAddComponent"] = &ScriptEntityRef::QueueAddComponent;
	scriptEntityRef["QueueRemoveComponent"] = &ScriptEntityRef::QueueRemoveComponent;
}
void ScriptingSystem::lua_add_general_component()
{
	sol::usertype<GeneralComponent> component = lua.new_usertype<GeneralComponent>(
		"GeneralComponent");
	component["SetVisible"] = &GeneralComponent::SetVisible;
	component["IsVisible"] = &GeneralComponent::IsVisible;
	component["SetComponentType"] = &GeneralComponent::SetComponentType;
	component["GetComponentType"] = &GeneralComponent::GetComponentType;
}
void ScriptingSystem::lua_add_transform_component()
{
	sol::usertype<TransformComponent> component = lua.new_usertype<TransformComponent>(
		"TransformComponent");
	component["SetPosition"] = sol::overload(
		[](TransformComponent& transform, const ScriptVector3& position)
		{
			transform.SetPosition3f(DirectX::XMFLOAT3(position.X, position.Y, position.Z));
		},
		[](TransformComponent& transform, float x, float y, float z)
		{
			transform.SetPosition(x, y, z);
		});
	component["SetRotation"] = sol::overload(
		[](TransformComponent& transform, const ScriptVector3& rotation)
		{
			transform.SetRotation3f(DirectX::XMFLOAT3(rotation.X, rotation.Y, rotation.Z));
		},
		[](TransformComponent& transform, float x, float y, float z)
		{
			transform.SetRotation(x, y, z);
		});
	component["SetScale"] = sol::overload(
		[](TransformComponent& transform, const ScriptVector3& scale)
		{
			transform.SetScale3f(DirectX::XMFLOAT3(scale.X, scale.Y, scale.Z));
		},
		[](TransformComponent& transform, float x, float y, float z)
		{
			transform.SetScale(x, y, z);
		});
	component["GetPosition"] = [](TransformComponent& transform)
	{
		const DirectX::XMFLOAT3 position = transform.GetPosition();
		return ScriptVector3{ position.x, position.y, position.z };
	};
	component["GetRotation"] = [](TransformComponent& transform)
	{
		const DirectX::XMFLOAT3 rotation = transform.GetRotation();
		return ScriptVector3{ rotation.x, rotation.y, rotation.z };
	};
	component["GetScale"] = [](TransformComponent& transform)
	{
		const DirectX::XMFLOAT3 scale = transform.GetScale();
		return ScriptVector3{ scale.x, scale.y, scale.z };
	};
	component["GetLocalPosition"] = [](TransformComponent& transform)
	{
		const DirectX::XMFLOAT3 position = transform.GetLocalPosition();
		return ScriptVector3{ position.x, position.y, position.z };
	};
	component["GetLocalRotation"] = [](TransformComponent& transform)
	{
		const DirectX::XMFLOAT3 rotation = transform.GetLocalRotation();
		return ScriptVector3{ rotation.x, rotation.y, rotation.z };
	};
	component["GetLocalScale"] = [](TransformComponent& transform)
	{
		const DirectX::XMFLOAT3 scale = transform.GetLocalScale();
		return ScriptVector3{ scale.x, scale.y, scale.z };
	};
}
void ScriptingSystem::lua_add_camera_component()
{
	sol::usertype<CameraComponent> component = lua.new_usertype<CameraComponent>(
		"CameraComponent");
	component["SetFov"] = &CameraComponent::SetFov;
	component["SetNear"] = &CameraComponent::SetNear;
	component["SetFar"] = &CameraComponent::SetFar;
	component["GetFov"] = &CameraComponent::GetFov;
	component["GetNear"] = &CameraComponent::GetNear;
	component["GetFar"] = &CameraComponent::GetFar;
	component["SetScale"] = &CameraComponent::SetScale;
	component["GetScale"] = &CameraComponent::GetScale;
	component["RestoreScale"] = &CameraComponent::RestoreScale;
}

void ScriptingSystem::lua_add_light_component()
{
	sol::usertype<LightComponent> component = lua.new_usertype<LightComponent>(
		"LightComponent");
	component["SetKind"] = &LightComponent::SetKind;
	component["GetKind"] = &LightComponent::GetKind;
	component["SetType"] = &LightComponent::SetType;
	component["GetType"] = &LightComponent::GetType;
	component["SetColor"] = [](LightComponent& light, const ScriptVector3& color)
	{
		light.SetColor(DirectX::XMFLOAT3(color.X, color.Y, color.Z));
	};
	component["GetColor"] = [](const LightComponent& light)
	{
		const DirectX::XMFLOAT3 color = light.GetColor();
		return ScriptVector3{ color.x, color.y, color.z };
	};
	component["SetPower"] = &LightComponent::SetPower;
	component["GetPower"] = &LightComponent::GetPower;
	component["SetCastShadow"] = &LightComponent::SetCastShadow;
	component["GetCastShadow"] = &LightComponent::GetCastShadow;
	component["SetEnableVolumetric"] = &LightComponent::SetEnableVolumetric;
	component["GetEnableVolumetric"] = &LightComponent::GetEnableVolumetric;
	component["SetVolumetricIntensity"] = &LightComponent::SetVolumetricIntensity;
	component["GetVolumetricIntensity"] = &LightComponent::GetVolumetricIntensity;
	component["SetVolumetricAttenuationDistance"] = &LightComponent::SetVolumetricAttenuationDistance;
	component["GetVolumetricAttenuationDistance"] = &LightComponent::GetVolumetricAttenuationDistance;

	sol::table lightKind = lua.create_table();
	lightKind["Ambient"] = LightKind::Ambient;
	lightKind["Directional"] = LightKind::Directional;
	lightKind["Spot"] = LightKind::Spot;
	lightKind["Point"] = LightKind::Point;
	lua["LightKind"] = lightKind;
}

void ScriptingSystem::lua_add_billboard_component()
{
	sol::usertype<BillboardComponent> component = lua.new_usertype<BillboardComponent>(
		"BillboardComponent");
	component["SetMode"] = &BillboardComponent::SetMode;
	component["GetMode"] = &BillboardComponent::GetMode;
	component["SetFacingMode"] = &BillboardComponent::SetFacingMode;
	component["GetFacingMode"] = &BillboardComponent::GetFacingMode;
	component["SetSize"] = &BillboardComponent::SetSize;
	component["GetWidth"] = &BillboardComponent::GetWidth;
	component["GetHeight"] = &BillboardComponent::GetHeight;
	component["SetScreenSize"] = &BillboardComponent::SetScreenSize;
	component["GetScreenSize"] = &BillboardComponent::GetScreenSize;
	component["SetOffset"] = [](BillboardComponent& billboard, const ScriptVector3& offset)
	{
		billboard.SetOffset(DirectX::XMFLOAT3(offset.X, offset.Y, offset.Z));
	};
	component["GetOffset"] = [](const BillboardComponent& billboard)
	{
		const DirectX::XMFLOAT3 offset = billboard.GetOffset();
		return ScriptVector3{ offset.x, offset.y, offset.z };
	};
	component["SetColor"] = [](BillboardComponent& billboard, const ScriptVector4& color)
	{
		billboard.SetColor(DirectX::XMFLOAT4(color.X, color.Y, color.Z, color.W));
	};
	component["GetColor"] = [](const BillboardComponent& billboard)
	{
		const DirectX::XMFLOAT4 color = billboard.GetColor();
		return ScriptVector4{ color.x, color.y, color.z, color.w };
	};
	component["SetMaterialName"] = [](BillboardComponent& billboard, const std::string& materialName)
	{
		billboard.SetMaterialName(ScriptingSystemDetail::ToWstring(materialName));
	};
	component["GetMaterialName"] = [](const BillboardComponent& billboard)
	{
		return ScriptingSystemDetail::ToUtf8(billboard.GetMaterialName());
	};

	sol::table billboardMode = lua.create_table();
	billboardMode["WorldSize"] = BillboardMode::WorldSize;
	billboardMode["ScreenSize"] = BillboardMode::ScreenSize;
	lua["BillboardMode"] = billboardMode;

	sol::table billboardFacingMode = lua.create_table();
	billboardFacingMode["FaceCamera"] = BillboardFacingMode::FaceCamera;
	billboardFacingMode["YAxisOnly"] = BillboardFacingMode::YAxisOnly;
	lua["BillboardFacingMode"] = billboardFacingMode;
}

void ScriptingSystem::lua_add_bone_attachment_component()
{
	sol::usertype<BoneAttachmentComponent> component = lua.new_usertype<BoneAttachmentComponent>(
		"BoneAttachmentComponent");
	component["SetBoneName"] = [](BoneAttachmentComponent& boneAttachment, const std::string& boneName)
	{
		boneAttachment.SetBoneName(ScriptingSystemDetail::ToWstring(boneName));
	};
	component["GetBoneName"] = [](const BoneAttachmentComponent& boneAttachment)
	{
		return ScriptingSystemDetail::ToUtf8(boneAttachment.GetBoneName());
	};
	component["SetBoneIndex"] = &BoneAttachmentComponent::SetBoneIndex;
	component["GetBoneIndex"] = &BoneAttachmentComponent::GetBoneIndex;
}

void ScriptingSystem::lua_add_mesh_component()
{
	sol::usertype<MeshComponent> component = lua.new_usertype<MeshComponent>(
		"MeshComponent");
	component["GetNumVertices"] = &MeshComponent::GetNumVertices;
	component["GetNumFaces"] = &MeshComponent::GetNumFaces;
	component["GetIndexCount"] = &MeshComponent::GetIndexCount;
	component["GetVertexCount"] = &MeshComponent::GetVertexCount;
	component["SetFileName"] = [](MeshComponent& mesh, const std::string& fileName)
	{
		mesh.SetFileName(ScriptingSystemDetail::ToWstring(fileName));
	};
	component["GetFileName"] = [](MeshComponent& mesh)
	{
		return ScriptingSystemDetail::ToUtf8(mesh.GetFileName());
	};
	component["SetMeshName"] = [](MeshComponent& mesh, const std::string& meshName)
	{
		mesh.SetMeshName(ScriptingSystemDetail::ToWstring(meshName));
	};
	component["GetMeshName"] = [](MeshComponent& mesh)
	{
		return ScriptingSystemDetail::ToUtf8(mesh.GetMeshName());
	};
	component["SetGeometryName"] = [](MeshComponent& mesh, const std::string& geometryName)
	{
		mesh.SetGeometryName(ScriptingSystemDetail::ToWstring(geometryName));
	};
	component["GetGeometryName"] = [](const MeshComponent& mesh)
	{
		return ScriptingSystemDetail::ToUtf8(mesh.GetGeometryName());
	};
	component["SetMaterial"] = [](MeshComponent& mesh, const std::string& materialName)
	{
		mesh.SetMaterial(ScriptingSystemDetail::ToWstring(materialName));
	};
	component["GetMaterialName"] = [](MeshComponent& mesh)
	{
		return ScriptingSystemDetail::ToUtf8(mesh.GetMaterialName());
	};
	component["SetDefaultMaterialName"] = [](MeshComponent& mesh, const std::string& materialName)
	{
		mesh.SetDefaultMaterialName(ScriptingSystemDetail::ToWstring(materialName));
	};
	component["GetDefaultMaterialName"] = [](const MeshComponent& mesh)
	{
		return ScriptingSystemDetail::ToUtf8(mesh.GetDefaultMaterialName());
	};
	component["OwnsGeometry"] = &MeshComponent::OwnsGeometry;
	component["SetAllVertexColor"] = [](MeshComponent& mesh, const ScriptVector4& color)
	{
		return mesh.SetAllVertexColor(DirectX::XMFLOAT4(color.X, color.Y, color.Z, color.W));
	};
}

void ScriptingSystem::lua_add_physics_component()
{
	sol::usertype<ScriptPhysicsColliderSnapshot> colliderSnapshot = lua.new_usertype<ScriptPhysicsColliderSnapshot>(
		"ScriptPhysicsColliderSnapshot");
	colliderSnapshot["colliderType"] = &ScriptPhysicsColliderSnapshot::colliderType;
	colliderSnapshot["activeComponent"] = &ScriptPhysicsColliderSnapshot::activeComponent;
	colliderSnapshot["staticFriction"] = &ScriptPhysicsColliderSnapshot::staticFriction;
	colliderSnapshot["dynamicFriction"] = &ScriptPhysicsColliderSnapshot::dynamicFriction;
	colliderSnapshot["restitution"] = &ScriptPhysicsColliderSnapshot::restitution;
	colliderSnapshot["center"] = &ScriptPhysicsColliderSnapshot::center;
	colliderSnapshot["size"] = &ScriptPhysicsColliderSnapshot::size;

	sol::usertype<PhysicsComponent> component = lua.new_usertype<PhysicsComponent>(
		"PhysicsComponent");
	component["GetBoxColliderCount"] = &PhysicsComponent::GetBoxColliderCount;
	component["HasPlaneCollider"] = &PhysicsComponent::HasPlaneCollider;
	component["RemoveCollider"] = &PhysicsComponent::RemoveCollider;
	component["GetColliderSnapshot"] = [](const PhysicsComponent& physics, size_t index) -> std::optional<ScriptPhysicsColliderSnapshot>
	{
		const PhysicsBoxColliderSnapshot* snapshot = physics.GetColliderSnapshot(index);
		if (snapshot == nullptr)
			return std::nullopt;

		ScriptPhysicsColliderSnapshot scriptSnapshot;
		scriptSnapshot.colliderType = snapshot->colliderType;
		scriptSnapshot.activeComponent = snapshot->activeComponent;
		scriptSnapshot.staticFriction = snapshot->staticFriction;
		scriptSnapshot.dynamicFriction = snapshot->dynamicFriction;
		scriptSnapshot.restitution = snapshot->restitution;
		scriptSnapshot.center = ScriptVector3{ snapshot->center.x, snapshot->center.y, snapshot->center.z };
		scriptSnapshot.size = ScriptVector3{ snapshot->size.x, snapshot->size.y, snapshot->size.z };
		return scriptSnapshot;
	};
	component["SetColliderSnapshot"] = [](PhysicsComponent& physics, size_t index, const ScriptPhysicsColliderSnapshot& snapshot)
	{
		PhysicsBoxColliderSnapshot physicsSnapshot;
		physicsSnapshot.colliderType = snapshot.colliderType;
		physicsSnapshot.activeComponent = snapshot.activeComponent;
		physicsSnapshot.staticFriction = snapshot.staticFriction;
		physicsSnapshot.dynamicFriction = snapshot.dynamicFriction;
		physicsSnapshot.restitution = snapshot.restitution;
		physicsSnapshot.center = DirectX::XMFLOAT3(snapshot.center.X, snapshot.center.Y, snapshot.center.Z);
		physicsSnapshot.size = DirectX::XMFLOAT3(snapshot.size.X, snapshot.size.Y, snapshot.size.Z);
		return physics.SetColliderSnapshot(index, physicsSnapshot);
	};

	sol::table physicsColliderType = lua.create_table();
	physicsColliderType["Box"] = PhysicsColliderType::Box;
	physicsColliderType["Plane"] = PhysicsColliderType::Plane;
	lua["PhysicsColliderType"] = physicsColliderType;
}
void ScriptingSystem::lua_add_rigidbody_component()
{
	sol::usertype<RigidBodyComponent> component = lua.new_usertype<RigidBodyComponent>(
		"RigidbodyComponent");

	component["SetMass"] = &RigidBodyComponent::SetMass;
	component["GetMass"] = &RigidBodyComponent::GetMass;
	component["SetLinearDamping"] = &RigidBodyComponent::SetLinearDamping;
	component["GetLinearDamping"] = &RigidBodyComponent::GetLinearDamping;
	component["SetAngularDamping"] = &RigidBodyComponent::SetAngularDamping;
	component["GetAngularDamping"] = &RigidBodyComponent::GetAngularDamping;
	component["SetGravity"] = &RigidBodyComponent::UseGravity;
	component["GetGravity"] = &RigidBodyComponent::HasUseGravity;
	component["SetKinematic"] = &RigidBodyComponent::SetKinematic;
	component["IsKinematic"] = &RigidBodyComponent::IsKinematic;
	component["AddForce"] = &RigidBodyComponent::AddForce;
	component["AddTorque"] = &RigidBodyComponent::AddTorque;
	component["ClearForce"] = &RigidBodyComponent::ClearForce;
	component["ClearTorque"] = &RigidBodyComponent::ClearTorque;
	component["SetLinearLockX"] = &RigidBodyComponent::SetLinearLockX;
	component["GetLinearLockX"] = &RigidBodyComponent::GetLinearLockX;
	component["SetLinearLockY"] = &RigidBodyComponent::SetLinearLockY;
	component["GetLinearLockY"] = &RigidBodyComponent::GetLinearLockY;
	component["SetLinearLockZ"] = &RigidBodyComponent::SetLinearLockZ;
	component["GetLinearLockZ"] = &RigidBodyComponent::GetLinearLockZ;
	component["SetAngularLockX"] = &RigidBodyComponent::SetAngularLockX;
	component["GetAngularLockX"] = &RigidBodyComponent::GetAngularLockX;
	component["SetAngularLockY"] = &RigidBodyComponent::SetAngularLockY;
	component["GetAngularLockY"] = &RigidBodyComponent::GetAngularLockY;
	component["SetAngularLockZ"] = &RigidBodyComponent::SetAngularLockZ;
	component["GetAngularLockZ"] = &RigidBodyComponent::GetAngularLockZ;
	component["SetPosition"] = [](RigidBodyComponent& rigidBody, const ScriptVector3& position)
	{
		rigidBody.SetPosition(DirectX::XMFLOAT3(position.X, position.Y, position.Z));
	};
	component["SetRotation"] = [](RigidBodyComponent& rigidBody, const ScriptVector4& rotation)
	{
		rigidBody.SetRotation(DirectX::XMFLOAT4(rotation.X, rotation.Y, rotation.Z, rotation.W));
	};
}

void ScriptingSystem::lua_add_scripting_component()
{
	sol::usertype<ScriptingComponent> component = lua.new_usertype<ScriptingComponent>(
		"ScriptingComponent");
	component["AddScript"] = [](ScriptingComponent& scripting, const std::string& path)
	{
		const std::wstring widePath = ScriptingSystemDetail::ToWstring(path);
		scripting.AddScript(widePath.c_str());
	};
	component["RemoveScript"] = &ScriptingComponent::RemoveScript;
	component["RecompileScripts"] = &ScriptingComponent::RecompileScripts;
	component["GetScriptCount"] = &ScriptingComponent::GetScriptCount;
	component["SetScriptActive"] = &ScriptingComponent::SetScriptActive;
}

void ScriptingSystem::lua_add_skeleton_component()
{
	sol::usertype<SkeletonComponent> component = lua.new_usertype<SkeletonComponent>(
		"SkeletonComponent");
	component["SetSkeletonAssetPath"] = [](SkeletonComponent& skeleton, const std::string& assetPath)
	{
		skeleton.SetSkeletonAssetPath(ScriptingSystemDetail::ToWstring(assetPath));
	};
	component["GetSkeletonAssetPath"] = [](const SkeletonComponent& skeleton)
	{
		return ScriptingSystemDetail::ToUtf8(skeleton.GetSkeletonAssetPath());
	};
	component["SetDirty"] = &SkeletonComponent::SetDirty;
	component["IsDirty"] = &SkeletonComponent::IsDirty;
}

void ScriptingSystem::lua_add_skinned_mesh_component()
{
	sol::usertype<SkinnedMeshComponent> component = lua.new_usertype<SkinnedMeshComponent>(
		"SkinnedMeshComponent");
	component["SetSkinnedMeshAssetPath"] = [](SkinnedMeshComponent& skinnedMesh, const std::string& assetPath)
	{
		skinnedMesh.SetSkinnedMeshAssetPath(ScriptingSystemDetail::ToWstring(assetPath));
	};
	component["GetSkinnedMeshAssetPath"] = [](const SkinnedMeshComponent& skinnedMesh)
	{
		return ScriptingSystemDetail::ToUtf8(skinnedMesh.GetSkinnedMeshAssetPath());
	};
	component["SetSkeletonAssetPath"] = [](SkinnedMeshComponent& skinnedMesh, const std::string& assetPath)
	{
		skinnedMesh.SetSkeletonAssetPath(ScriptingSystemDetail::ToWstring(assetPath));
	};
	component["GetSkeletonAssetPath"] = [](const SkinnedMeshComponent& skinnedMesh)
	{
		return ScriptingSystemDetail::ToUtf8(skinnedMesh.GetSkeletonAssetPath());
	};
}

sol::state& ScriptingSystem::GetState()
{
	return lua;
}

bool ScriptTransformRef::IsValid() const
{
	return Entity != nullptr && Ecs != nullptr;
}

ScriptVector3 ScriptTransformRef::GetPosition() const
{
	Transform transform{};
	if (!IsValid() || !Ecs->GetEntityEditableLocalTransform(Entity, &transform))
		return ScriptVector3{};
	return ScriptVector3{ transform.position.x, transform.position.y, transform.position.z };
}

void ScriptTransformRef::SetPosition(float x, float y, float z) const
{
	Transform transform{};
	if (!IsValid() || !Ecs->GetEntityEditableLocalTransform(Entity, &transform))
		return;
	transform.position = DirectX::XMFLOAT3(x, y, z);
	Ecs->SetEntityEditableLocalTransform(Entity, transform);
}

ScriptVector3 ScriptTransformRef::GetRotation() const
{
	Transform transform{};
	if (!IsValid() || !Ecs->GetEntityEditableLocalTransform(Entity, &transform))
		return ScriptVector3{};
	return ScriptVector3{ transform.rotation.x, transform.rotation.y, transform.rotation.z };
}

void ScriptTransformRef::SetRotation(float x, float y, float z) const
{
	Transform transform{};
	if (!IsValid() || !Ecs->GetEntityEditableLocalTransform(Entity, &transform))
		return;
	transform.rotation = DirectX::XMFLOAT3(x, y, z);
	Ecs->SetEntityEditableLocalTransform(Entity, transform);
}

ScriptVector3 ScriptTransformRef::GetScale() const
{
	Transform transform{};
	if (!IsValid() || !Ecs->GetEntityEditableLocalTransform(Entity, &transform))
		return ScriptVector3{};
	return ScriptVector3{ transform.scale.x, transform.scale.y, transform.scale.z };
}

void ScriptTransformRef::SetScale(float x, float y, float z) const
{
	Transform transform{};
	if (!IsValid() || !Ecs->GetEntityEditableLocalTransform(Entity, &transform))
		return;
	transform.scale = DirectX::XMFLOAT3(x, y, z);
	Ecs->SetEntityEditableLocalTransform(Entity, transform);
}

bool ScriptAnimatorRef::IsValid() const
{
	return Animator != nullptr;
}

bool ScriptAnimatorRef::Play(const std::string& clipAssetPath, const std::string& layerName, float startTime, bool loop) const
{
	return Witchcraft::Animation::AnimationPlaybackController::Play(
		Animator,
		SString::UTF8ToWstring(clipAssetPath),
		SString::UTF8ToWstring(layerName),
		startTime,
		loop);
}

bool ScriptAnimatorRef::CrossFade(const std::string& clipAssetPath, float fadeDuration, const std::string& layerName, float startTime, bool loop) const
{
	return Witchcraft::Animation::AnimationPlaybackController::CrossFade(
		Animator,
		SString::UTF8ToWstring(clipAssetPath),
		fadeDuration,
		SString::UTF8ToWstring(layerName),
		startTime,
		loop);
}

bool ScriptAnimatorRef::Stop(const std::string& layerName) const
{
	return Witchcraft::Animation::AnimationPlaybackController::Stop(Animator, SString::UTF8ToWstring(layerName));
}

bool ScriptAnimatorRef::SetLayerWeight(const std::string& layerName, float weight) const
{
	return Witchcraft::Animation::AnimationPlaybackController::SetLayerWeight(Animator, SString::UTF8ToWstring(layerName), weight);
}

bool ScriptAnimatorRef::SetLayerSpeed(const std::string& layerName, float speed) const
{
	return Witchcraft::Animation::AnimationPlaybackController::SetLayerSpeed(Animator, SString::UTF8ToWstring(layerName), speed);
}

bool ScriptAnimatorRef::SetLayerEnabled(const std::string& layerName, bool enabled) const
{
	return Witchcraft::Animation::AnimationPlaybackController::SetLayerEnabled(Animator, SString::UTF8ToWstring(layerName), enabled);
}

bool ScriptEntityRef::IsValid() const
{
	return Entity != nullptr && Ecs != nullptr;
}

ScriptEntityRef ScriptEntityRef::GetParent() const
{
	if (!IsValid())
		return ScriptEntityRef{};

	return ScriptEntityRef{ Ecs->GetParentEntity(Entity), Ecs, Runtime };
}

std::string ScriptEntityRef::GetName() const
{
	if (!IsValid())
		return {};

	return ScriptingSystemDetail::ToUtf8(Ecs->GetEntityName(Entity));
}

bool ScriptEntityRef::SetName(const std::string& name) const
{
	if (!IsValid() || name.empty())
		return false;

	return Ecs->RenameEntity(Entity, ScriptingSystemDetail::ToWstring(name));
}

std::string ScriptEntityRef::GetTag() const
{
	if (!IsValid())
		return {};

	return ScriptingSystemDetail::ToUtf8(Ecs->GetEntityTag(Entity));
}

bool ScriptEntityRef::SetTag(const std::string& tag) const
{
	if (!IsValid())
		return false;

	return Ecs->SetEntityTag(Entity, ScriptingSystemDetail::ToWstring(tag));
}

bool ScriptEntityRef::IsVisible() const
{
	return IsValid() && Ecs->IsEntityVisible(Entity);
}

bool ScriptEntityRef::SetVisible(bool visible) const
{
	return IsValid() && Ecs->SetEntityVisible(Entity, visible);
}

bool ScriptEntityRef::IsStatic() const
{
	return IsValid() && Ecs->IsEntityStatic(Entity);
}

bool ScriptEntityRef::SetStatic(bool isStatic) const
{
	return IsValid() && Ecs->SetEntityStatic(Entity, isStatic);
}

SceneEntityType ScriptEntityRef::GetSceneType() const
{
	if (!IsValid())
		return SceneEntityType::StaticScenery;

	SceneEntityType type = SceneEntityType::StaticScenery;
	(void)Ecs->GetEntitySceneType(Entity, &type);
	return type;
}

bool ScriptEntityRef::SetSceneType(SceneEntityType type) const
{
	return IsValid() && Ecs->SetEntitySceneType(Entity, type);
}

ScriptTransformRef ScriptEntityRef::GetTransform() const
{
	if (!IsValid())
		return ScriptTransformRef{};
	return ScriptTransformRef{ Entity, Ecs };
}

GeneralComponent* ScriptEntityRef::GetGeneralComponent() const
{
	return IsValid() ? Ecs->GetComponent<GeneralComponent>(Entity) : nullptr;
}

LightComponent* ScriptEntityRef::GetLightComponent() const
{
	return IsValid() ? Ecs->GetComponent<LightComponent>(Entity) : nullptr;
}

CameraComponent* ScriptEntityRef::GetCameraComponent() const
{
	return IsValid() ? Ecs->GetComponent<CameraComponent>(Entity) : nullptr;
}

MeshComponent* ScriptEntityRef::GetMeshComponent() const
{
	return IsValid() ? Ecs->GetComponent<MeshComponent>(Entity) : nullptr;
}

PhysicsComponent* ScriptEntityRef::GetPhysicsComponent() const
{
	return IsValid() ? Ecs->GetComponent<PhysicsComponent>(Entity) : nullptr;
}

RigidBodyComponent* ScriptEntityRef::GetRigidBodyComponent() const
{
	return IsValid() ? Ecs->GetComponent<RigidBodyComponent>(Entity) : nullptr;
}

ScriptingComponent* ScriptEntityRef::GetScriptingComponent() const
{
	return IsValid() ? Ecs->GetComponent<ScriptingComponent>(Entity) : nullptr;
}

BillboardComponent* ScriptEntityRef::GetBillboardComponent() const
{
	return IsValid() ? Ecs->GetComponent<BillboardComponent>(Entity) : nullptr;
}

BoneAttachmentComponent* ScriptEntityRef::GetBoneAttachmentComponent() const
{
	return IsValid() ? Ecs->GetComponent<BoneAttachmentComponent>(Entity) : nullptr;
}

SkeletonComponent* ScriptEntityRef::GetSkeletonComponent() const
{
	return IsValid() ? Ecs->GetComponent<SkeletonComponent>(Entity) : nullptr;
}

SkinnedMeshComponent* ScriptEntityRef::GetSkinnedMeshComponent() const
{
	return IsValid() ? Ecs->GetComponent<SkinnedMeshComponent>(Entity) : nullptr;
}

ScriptAnimatorRef ScriptEntityRef::GetAnimator() const
{
	if (!IsValid())
		return ScriptAnimatorRef{};
	return ScriptAnimatorRef{ Ecs->GetComponent<AnimatorComponent>(Entity) };
}

bool ScriptEntityRef::AddBoxCollider() const
{
	return IsValid() && Ecs->AddBoxColliderToEntity(Entity);
}

bool ScriptEntityRef::AddPlaneCollider() const
{
	return IsValid() && Ecs->AddPlaneColliderToEntity(Entity);
}

bool ScriptEntityRef::QueueCreateChild(const std::string& name) const
{
	if (!IsValid() || Runtime == nullptr || name.empty())
		return false;
	return Runtime->QueueCreateChild(Entity, SString::UTF8ToWstring(name));
}

bool ScriptEntityRef::QueueDestroy(bool destroyChildren) const
{
	if (!IsValid() || Runtime == nullptr)
		return false;
	return Runtime->QueueDestroyEntity(Entity, destroyChildren);
}

bool ScriptEntityRef::QueueAddComponent(const std::string& componentName) const
{
	if (!IsValid() || Runtime == nullptr || componentName.empty())
		return false;
	return Runtime->QueueAddComponent(Entity, SString::UTF8ToWstring(componentName));
}

bool ScriptEntityRef::QueueRemoveComponent(const std::string& componentName) const
{
	if (!IsValid() || Runtime == nullptr || componentName.empty())
		return false;
	return Runtime->QueueRemoveComponent(Entity, SString::UTF8ToWstring(componentName));
}

void ScriptingSystem::CreateScript(const wchar_t* filename, const wchar_t* name)
{
	(void)name;
	std::wstring buffer = L"local M = {}\n"
		L"\n"
		L"function M.OnStart(entity)\n"
		L"end\n"
		L"\n"
		L"function M.OnUpdate(entity, deltaTime)\n"
		L"end\n"
		L"\n"
		L"function M.OnAnimationEvent(entity, event)\n"
		L"end\n"
		L"\n"
		L"return M\n";

	std::wofstream script;
	script.open(filename);
	script << buffer;
	script.close();
}
