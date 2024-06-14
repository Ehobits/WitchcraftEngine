#include "ScriptingSystem.h"
#include "HELPERS/Helpers.h"
#include "ServicesContainer/COMPONENT/GeneralComponent.h"
#include "ServicesContainer/COMPONENT/TransformComponent.h"
#include "ServicesContainer/COMPONENT/CameraComponent.h"
#include "ServicesContainer/COMPONENT/RigidbodyComponent.h"
#include "String/SStringUtils.h"
#include "Assets.h"

#include <fstream>
#include <filesystem>

#define COMPONENT_ERROR L"Failed to get %s because it was not found!"

bool ScriptingSystem::Init(ServicesContainer* ComponentServices)
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

	/* entity */
	lua_add_entity();
	lua_add_general_component();
	lua_add_transform_component();
	lua_add_camera_component();
	lua_add_rigidbody_component();
	lua_add_mesh_component();

	return true;
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
	//lua["Time"]["FrameTime"]   = []() { return game->GetFrameTime();   };
	//lua["Time"]["DeltaTime"]   = []() { return game->GetDeltaTime();   };
	//lua["Time"]["ElapsedTime"] = []() { return game->GetElapsedTime(); };
	//lua["Time"]["FrameCount"]  = []() { return game->GetFrameCount();  };
}
void ScriptingSystem::lua_add_input()
{
	lua["Input"] = sol::new_table();

	/* Mouse */
	//lua["Input"]["ShowCursor"] = []() { StarHelpers::ShowCursor(true); };
	//lua["Input"]["HideCursor"] = []() { StarHelpers::ShowCursor(false); };
	//lua["Input"]["HideCursor"]     = [](bool value) { game->HideCursor(value); };
	//lua["Input"]["LockCursor"]     = [](bool value) { game->LockCursor(value); };
	//lua["Input"]["IsCursorHidden"] = []() { return game->IsCursorHidden(); };
	//lua["Input"]["IsCursorLocked"] = []() { return game->IsCursorLocked(); };
	/* Keyboard */
	lua["Input"]["KeyCode"] = sol::new_table();

	//lua["Input"]["KeyCode"]["Q"] = DIK_Q;
	//lua["Input"]["KeyCode"]["W"] = DIK_W;
	//lua["Input"]["KeyCode"]["E"] = DIK_E;
	//lua["Input"]["KeyCode"]["R"] = DIK_R;
	//lua["Input"]["KeyCode"]["T"] = DIK_T;
	//lua["Input"]["KeyCode"]["Y"] = DIK_Y;
	//lua["Input"]["KeyCode"]["U"] = DIK_U;
	//lua["Input"]["KeyCode"]["I"] = DIK_I;
	//lua["Input"]["KeyCode"]["O"] = DIK_O;
	//lua["Input"]["KeyCode"]["P"] = DIK_P;

	//lua["Input"]["KeyCode"]["A"] = DIK_A;
	//lua["Input"]["KeyCode"]["S"] = DIK_S;
	//lua["Input"]["KeyCode"]["D"] = DIK_D;
	//lua["Input"]["KeyCode"]["F"] = DIK_F;
	//lua["Input"]["KeyCode"]["G"] = DIK_G;
	//lua["Input"]["KeyCode"]["H"] = DIK_H;
	//lua["Input"]["KeyCode"]["J"] = DIK_J;
	//lua["Input"]["KeyCode"]["K"] = DIK_K;
	//lua["Input"]["KeyCode"]["L"] = DIK_L;

	//lua["Input"]["KeyCode"]["Z"] = DIK_Z;
	//lua["Input"]["KeyCode"]["X"] = DIK_X;
	//lua["Input"]["KeyCode"]["C"] = DIK_C;
	//lua["Input"]["KeyCode"]["V"] = DIK_V;
	//lua["Input"]["KeyCode"]["B"] = DIK_B;
	//lua["Input"]["KeyCode"]["N"] = DIK_N;
	//lua["Input"]["KeyCode"]["M"] = DIK_M;

	//lua["Input"]["KeyCode"]["1"] = DIK_1;
	//lua["Input"]["KeyCode"]["2"] = DIK_2;
	//lua["Input"]["KeyCode"]["3"] = DIK_3;
	//lua["Input"]["KeyCode"]["4"] = DIK_4;
	//lua["Input"]["KeyCode"]["5"] = DIK_5;
	//lua["Input"]["KeyCode"]["6"] = DIK_6;
	//lua["Input"]["KeyCode"]["7"] = DIK_7;
	//lua["Input"]["KeyCode"]["8"] = DIK_8;
	//lua["Input"]["KeyCode"]["9"] = DIK_9;
	//lua["Input"]["KeyCode"]["0"] = DIK_0;

	//lua["Input"]["KeyCode"]["F1"] = DIK_F1;
	//lua["Input"]["KeyCode"]["F2"] = DIK_F2;
	//lua["Input"]["KeyCode"]["F3"] = DIK_F3;
	//lua["Input"]["KeyCode"]["F4"] = DIK_F4;
	//lua["Input"]["KeyCode"]["F5"] = DIK_F5;
	//lua["Input"]["KeyCode"]["F6"] = DIK_F6;
	//lua["Input"]["KeyCode"]["F7"] = DIK_F7;
	//lua["Input"]["KeyCode"]["F8"] = DIK_F8;
	//lua["Input"]["KeyCode"]["F9"] = DIK_F9;
	//lua["Input"]["KeyCode"]["F10"] = DIK_F10;
	//lua["Input"]["KeyCode"]["F11"] = DIK_F11;
	//lua["Input"]["KeyCode"]["F12"] = DIK_F12;

	//lua["Input"]["KeyCode"]["Up"] = DIK_UP;
	//lua["Input"]["KeyCode"]["Down"] = DIK_DOWN;
	//lua["Input"]["KeyCode"]["Left"] = DIK_LEFT;
	//lua["Input"]["KeyCode"]["Right"] = DIK_RIGHT;

	//lua["Input"]["KeyCode"]["LeftControl"] = DIK_LCONTROL;
	//lua["Input"]["KeyCode"]["RightControl"] = DIK_RCONTROL;

	//lua["Input"]["KeyCode"]["LeftShift"] = DIK_LSHIFT;
	//lua["Input"]["KeyCode"]["RightShift"] = DIK_RSHIFT;

	//lua["Input"]["KeyCode"]["LeftAlt"] = DIK_LALT;
	//lua["Input"]["KeyCode"]["RightAlt"] = DIK_RALT;
}
void ScriptingSystem::lua_add_bounding_box()
{
	using namespace DirectX;

	sol::usertype<BoundingBox> boundingBox = lua.new_usertype<BoundingBox>(
		"BoundingBox",
		sol::constructors<BoundingBox(), BoundingBox(DirectX::XMFLOAT3, DirectX::XMFLOAT3)>());
}

/* entity */
void ScriptingSystem::lua_add_entity()
{
	sol::usertype<EntityX> entity = lua.new_usertype<EntityX>(
		"ComponentServicesContainer");
	entity["CreateEntity"] = &EntityX::CreateEntity;
	entity["AddComponent"] = &EntityX::AddComponent;
	entity["GetComponent"] = &EntityX::GetComponent;
	entity["HasComponent"] = &EntityX::HasComponent;
	entity["RemoveComponent"] = &EntityX::RemoveComponent;
}
void ScriptingSystem::lua_add_general_component()
{
	sol::usertype<GeneralComponent> component = lua.new_usertype<GeneralComponent>(
		"GeneralComponent");
	component["SetName"] = &GeneralComponent::SetName;
	component["GetName"] = &GeneralComponent::GetName;
	component["SetTag"] = &GeneralComponent::SetTag;
	component["GetTag"] = &GeneralComponent::GetTag;
	component["SetActive"] = &GeneralComponent::SetEnabled;
	component["GetActive"] = &GeneralComponent::IsEnabled;
	component["SetStatic"] = &GeneralComponent::SetStatic;
	component["GetStatic"] = &GeneralComponent::IsStatic;
}
void ScriptingSystem::lua_add_transform_component()
{
	sol::usertype<TransformComponent> component = lua.new_usertype<TransformComponent>(
		"TransformComponent");
	component["SetBoundingBox"] = &TransformComponent::SetBoundingBox;
	component["GetBoundingBox"] = &TransformComponent::GetBoundingBox;
	component["SetPosition"] = &TransformComponent::SetPosition3f;
	component["SetRotation"] = &TransformComponent::SetRotation3f;
	component["SetScale"] = &TransformComponent::SetScale3f;
	component["SetTransform"] = &TransformComponent::SetTransform;
	//component["AddPosition"] = &TransformComponent::AddPosition;
	//component["AddRotationYawPitchRoll"] = &TransformComponent::AddRotationYawPitchRoll;
	//component["AddRotationQuaternion"] = &TransformComponent::AddRotationQuaternion;
	//component["AddScale"] = &TransformComponent::AddScale;
	//component["AddTransform"] = &TransformComponent::AddTransform;
	component["GetPosition"] = &TransformComponent::GetPosition;
	component["GetRotation"] = &TransformComponent::GetRotation;
	component["GetScale"] = &TransformComponent::GetScale;
	component["GetTransform"] = &TransformComponent::GetTransform;
	component["GetLocalPosition"] = &TransformComponent::GetLocalPosition;
	component["GetLocalRotation"] = &TransformComponent::GetLocalRotation;
	component["GetLocalScale"] = &TransformComponent::GetLocalScale;
	component["GetLocalTransform"] = &TransformComponent::GetLocalTransform;
	//component["LookAt"] = &TransformComponent::LookAt;
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
	component["SetEnabled"] = &CameraComponent::SetEnabled;
	component["GetEnabled"] = &CameraComponent::IsEnabled;
	component["SetScale"] = &CameraComponent::SetScale;
	component["GetScale"] = &CameraComponent::GetScale;
}
void ScriptingSystem::lua_add_rigidbody_component()
{
	sol::usertype<RigidBodyComponent> component = lua.new_usertype<RigidBodyComponent>(
		"RigidbodyComponent");

	component["SetMass"] = &RigidBodyComponent::SetMass;
	//component["GetMass"] = &RigidBodyComponent::GetMass;
	//component["SetLinearVelocity"] = &RigidBodyComponent::SetLinearVelocity;
	//component["GetLinearVelocity"] = &RigidBodyComponent::GetLinearVelocity;
	//component["SetAngularVelocity"] = &RigidBodyComponent::SetAngularVelocity;
	//component["GetAngularVelocity"] = &RigidBodyComponent::GetAngularVelocity;
	component["SetLinearDamping"] = &RigidBodyComponent::SetLinearDamping;
	//component["GetLinearDamping"] = &RigidBodyComponent::GetLinearDamping;
	component["SetAngularDamping"] = &RigidBodyComponent::SetAngularDamping;
	//component["GetAngularDamping"] = &RigidBodyComponent::GetAngularDamping;
	component["SetGravity"] = &RigidBodyComponent::UseGravity;
	//component["GetGravity"] = &RigidBodyComponent::HasUseGravity;
	component["SetKinematic"] = &RigidBodyComponent::SetKinematic;
	//component["IsKinematic"] = &RigidBodyComponent::IsKinematic;
	component["AddForce"] = &RigidBodyComponent::AddForce;
	component["AddTorque"] = &RigidBodyComponent::AddTorque;
	component["ClearForce"] = &RigidBodyComponent::ClearForce;
	component["ClearTorque"] = &RigidBodyComponent::ClearTorque;
}
void ScriptingSystem::lua_add_mesh_component()
{
	//sol::usertype<MeshComponent> component = lua.new_usertype<MeshComponent>(
	//	"MeshComponent");

	//component["GetNumVertices"] = &MeshComponent::GetNumVertices;
	//component["GetNumFaces"] = &MeshComponent::GetNumFaces;

	//component["AddVertices"] = &MeshComponent::AddVertices;
	//component["AddIndices"] = &MeshComponent::AddIndices;

	//component["SetupMesh"] = &MeshComponent::SetupMesh;

	///* --- */

	//sol::usertype<Vertex> vertex = lua.new_usertype<Vertex>(
	//	"Vertex");

	//vertex["position"] = &Vertex::position;
	//vertex["normal"] = &Vertex::normal;
	//vertex["texCoords"] = &Vertex::texCoords;
}

sol::state& ScriptingSystem::GetState()
{
	return lua;
}

/*** --- EntityX --- ***/

void EntityX::CreateEntity()
{
	//entity = ComponentServices->CreateEntity();
	//ComponentServices->GetComponent<GeneralComponent>(ComponentServices->root).AddChild(entity);
}

void EntityX::AddComponent(const char* component_name)
{
	//if (strcmp(component_name, "MeshComponent") == 0)
	//{
	//	//if (!ComponentServices->HasComponent<MeshComponent>(ComponentServices->selected))
	//	//	ComponentServices->AddComponent<MeshComponent>(ComponentServices->selected);
	//}
	//else if (strcmp(component_name, "TextMeshComponent") == 0)
	//{
	//	if (!ComponentServices->HasComponent<TextMeshComponent>(ComponentServices->selected))
	//		ComponentServices->AddComponent<TextMeshComponent>(ComponentServices->selected);
	//}
	//else if (strcmp(component_name, "CameraComponent") == 0)
	//{
	//	if (!ComponentServices->HasComponent<CameraComponent>(ComponentServices->selected))
	//		ComponentServices->AddComponent<CameraComponent>(ComponentServices->selected);
	//}
	//else if (strcmp(component_name, "RigidbodyComponent") == 0)
	//{
	//	if (!ComponentServices->HasComponent<RigidBodyComponent>(ComponentServices->selected))
	//	{
	//		ComponentServices->AddComponent<RigidBodyComponent>(ComponentServices->selected);
	//		ComponentServices->GetComponent<RigidBodyComponent>(ComponentServices->selected).CreateActor();
	//	}
	//}
	////else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);
}
sol::object EntityX::GetComponent(const char* component_name)
{
	sol::object component;

	//if (strcmp(component_name, "GeneralComponent") == 0)
	//{
	//	if (ComponentServices->HasComponent<GeneralComponent>(entity))
	//	{
	//		auto& entt_comp = ComponentServices->GetComponent<GeneralComponent>(entity);
	//		component = sol::make_object(scriptingSystem.GetState(), &entt_comp);
	//	}
	//	//else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);
	//}
	//else if (strcmp(component_name, "TransformComponent") == 0)
	//{
	//	if (ComponentServices->HasComponent<TransformComponent>(entity))
	//	{
	//		auto& entt_comp = ComponentServices->GetComponent<TransformComponent>(entity);
	//		component = sol::make_object(scriptingSystem.GetState(), &entt_comp);
	//	}
	//	//else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);
	//}
	//else if (strcmp(component_name, "MeshComponent") == 0)
	//{
	//	//if (ComponentServices->HasComponent<MeshComponent>(entity))
	//	//{
	//	//	auto& entt_comp = ComponentServices->GetComponent<MeshComponent>(entity);
	//	//	component = sol::make_object(scriptingSystem.GetState(), &entt_comp);
	//	//}
	//	//else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);
	//}
	//else if (strcmp(component_name, "TextMeshComponent") == 0)
	//{
	//	if (ComponentServices->HasComponent<TextMeshComponent>(entity))
	//	{
	//		auto& entt_comp = ComponentServices->GetComponent<TextMeshComponent>(entity);
	//		component = sol::make_object(scriptingSystem.GetState(), &entt_comp);
	//	}
	//	//else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);
	//}
	//else if (strcmp(component_name, "CameraComponent") == 0)
	//{
	//	if (ComponentServices->HasComponent<CameraComponent>(entity))
	//	{
	//		auto& entt_comp = ComponentServices->GetComponent<CameraComponent>(entity);
	//		component = sol::make_object(scriptingSystem.GetState(), &entt_comp);
	//	}
	//	//else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);
	//}
	//else if (strcmp(component_name, "RigidbodyComponent") == 0)
	//{
	//	if (ComponentServices->HasComponent<RigidBodyComponent>(entity))
	//	{
	//		auto& entt_comp = ComponentServices->GetComponent<RigidBodyComponent>(entity);
	//		component = sol::make_object(scriptingSystem.GetState(), &entt_comp);
	//	}
	//	//else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);
	//}
	////else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);

	return component;
}
bool EntityX::HasComponent(const char* component_name)
{
	////if (strcmp(component_name, "MeshComponent") == 0)
	////	return ComponentServices->HasComponent<MeshComponent>(entity);
	//if (strcmp(component_name, "TextMeshComponent") == 0)
	//	return ComponentServices->HasComponent<TextMeshComponent>(entity);
	//else if (strcmp(component_name, "CameraComponent") == 0)
	//	return ComponentServices->HasComponent<CameraComponent>(entity);
	//else if (strcmp(component_name, "RigidbodyComponent") == 0)
	//	return ComponentServices->HasComponent<RigidBodyComponent>(entity);
	////else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);

	return false;
}
void EntityX::RemoveComponent(const char* component_name)
{
	////if (strcmp(component_name, "MeshComponent") == 0)
	////	ComponentServices->RemoveComponent<MeshComponent>(entity);
	//if (strcmp(component_name, "TextMeshComponent") == 0)
	//	ComponentServices->RemoveComponent<TextMeshComponent>(entity);
	//else if (strcmp(component_name, "CameraComponent") == 0)
	//	ComponentServices->RemoveComponent<CameraComponent>(entity);
	//else if (strcmp(component_name, "RigidbodyComponent") == 0)
	//	ComponentServices->RemoveComponent<RigidBodyComponent>(entity);
	////else consoleWindow->AddWarningMessage(COMPONENT_ERROR, component_name);
}

void ScriptingSystem::CreateScript(const wchar_t* filename, const wchar_t* name)
{
	std::wstring buffer = std::wstring(name) + L" = {}\n"
		L"\n"
		L"-- Use this for initialization\n"
		L"function " + std::wstring(name) + L":Start()\n"
		L"end\n"
		L"\n"
		L"-- Update is called once per frame\n"
		L"function " + std::wstring(name) + L":Update()\n"
		L"end";

	std::wofstream script;
	script.open(filename);
	script << buffer;
	script.close();
}