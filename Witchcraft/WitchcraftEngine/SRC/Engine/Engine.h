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
#include "../ECS/WitchcraECS.h"

class Engine
{
public:
	void EngineStart(D3DWindow* dx, Editor* editor, std::wstring MainPath);
	void EngineProcess();
	void EngineShutdown();
	void UpdateComponent();
	void GamePlayUpdate();

	void TimerStart();
	void TimerStop();

	KeyboardClass* GetKeyboard();
	MouseClass* GetMouse();

	ServicesContainer* GetEntity();
	PhysicsSystem* GetphysicsSystem();
	ScriptingSystem* GetscriptingSystem();
	ProjectSceneSystem* GetprojectSceneSystem();

	Timer timer;

private:
	D3DWindow* m_dx = nullptr;
	Editor* m_editor = nullptr;
	AssimpLoader assimpLoader;
	ServicesContainer ComponentServices;
	WitchcraECS ecs;
	ModelSystem modelSystem;
	PhysicsSystem physicsSystem;
	ScriptingSystem scriptingSystem;
	ProjectSceneSystem projectSceneSystem;
	KeyboardClass keyboard;
	MouseClass mouse;

};
