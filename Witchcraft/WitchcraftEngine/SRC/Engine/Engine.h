#pragma once

#include <xstring>

#include <Windows.h>

#include "Timer.h"
#include "D3DWindow/D3DWindow.h"
#include "Editor/Editor.h"
#include "System/PhysicsSystem.h"
#include "System/ModelSystem.h"
#include "System/ScriptingSystem.h"
#include "System/ProjectSceneSystem.h"
#include "HELPERS/Helpers.h"
#include "UserInput/Keyboard/KeyboardClass.h"
#include "UserInput/Mouse/MouseClass.h"
#include "Engine/EngineUtils.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "ECS/WitchcraECS.h"

struct Object
{
	std::wstring name;
	Transform transform;
	CreateItem item = CreateItem::UnknownItem;
	std::wstring materialName = L"autoMat";
};

struct SkyCreateRequest
{
	std::wstring name;
	std::wstring skyTexturePath;
};

class Engine
{
public:
	void EngineStart(D3DWindow* dx, Editor* editor, std::wstring MainPath);
	void EngineProcess();
	void EngineShutdown();
	void UpdateComponent();
	void GamePlayUpdate();
	void CreateDefaultSkyEntity();
	void CreateSkyEntity(const std::wstring& name);
	void CreateSkyEntity(const std::wstring& name, const std::wstring& skyTexturePath);
	void QueueCreateSkyEntity(const std::wstring& name, const std::wstring& skyTexturePath);

	void TimerStart();
	void TimerStop();

	KeyboardClass* GetKeyboard();
	MouseClass* GetMouse();

	PhysicsSystem* GetphysicsSystem();
	ScriptingSystem* GetscriptingSystem();
	ProjectSceneSystem* GetprojectSceneSystem();
	WitchcraECS* GetECS();

	void AddObject(std::wstring name, Transform* tf, CreateItem item = CreateItem::UnknownItem, const std::wstring& materialName = L"autoMat");

	Timer timer;

private:
	D3DWindow* m_dx = nullptr;
	Editor* m_editor = nullptr;
	AssimpLoader assimpLoader;
	WitchcraECS ecs;
	ModelSystem modelSystem;
	PhysicsSystem physicsSystem;
	ScriptingSystem scriptingSystem;
	ProjectSceneSystem projectSceneSystem;
	KeyboardClass keyboard;
	MouseClass mouse;

	bool b_createObject = false;
	Object ctrateObject;
	bool b_createSkyEntity = false;
	SkyCreateRequest createSkyRequest;
};
