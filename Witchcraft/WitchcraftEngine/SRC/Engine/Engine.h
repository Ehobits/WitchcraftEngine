#pragma once

#include <xstring>

#include <Windows.h>

#include "Common/TransformSharedTypes.h"
#include "Timer.h"
#include "D3DWindow/D3DWindow.h"
#include "Editor/Editor.h"
#include "System/PhysicsSystem.h"
#include "System/Animation/AnimationSystem.h"
#include "System/ModelSystem.h"
#include "System/ScriptingSystem.h"
#include "System/ProjectSceneSystem.h"
#include "System/SceneLightSystem.h"
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
	CreateLightType lightType = CreateDirectionalLight;
	SceneEntityType sceneType = SceneEntityType::StaticScenery;
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
	D3DWindow* GetD3DWindow();
	ConsoleWindow* GetConsoleWindow();
	Editor* GetEditor();

	void AddObject(std::wstring name, Transform* tf, CreateItem item = CreateItem::UnknownItem, const std::wstring& materialName = L"autoMat", CreateLightType lightType = CreateDirectionalLight, SceneEntityType sceneType = SceneEntityType::StaticScenery);

	Timer timer;

private:
	std::wstring ResolveDataModelPath(const std::wstring& fileName);
	bool IsNearlyZero(float value);
	bool IsDefaultPosition(const DirectX::XMFLOAT3& position);
	void ApplyDefaultLightPreset(EntityLightComponentData* lightData, Transform* transform, CreateLightType lightType);

private:
	D3DWindow* m_dx = nullptr;
	Editor* m_editor = nullptr;
	AssimpLoader assimpLoader;
	WitchcraECS ecs;
	ModelSystem modelSystem;
	AnimationSystem m_animationSystem;
	PhysicsSystem physicsSystem;
	ScriptingSystem scriptingSystem;
	ProjectSceneSystem projectSceneSystem;
	SceneLightSystem sceneLightSystem;
	KeyboardClass keyboard;
	MouseClass mouse;

	bool b_createObject = false;
	Object ctrateObject;
	bool b_createSkyEntity = false;
	SkyCreateRequest createSkyRequest;

	float kTransformEpsilon = 1e-4f;

	float kDirectionalShaderLightType = 0.0f;
	float kPointShaderLightType = 1.0f;
	float kSpotShaderLightType = 2.0f;

	DirectX::XMFLOAT3 kDefaultForwardLightRotation = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 kDefaultDirectionalLightRotation = { 0.0f, 0.0f, 0.0f };
	UINT kAmbientLightKind = 0u;
	UINT kDirectionalLightKind = 1u;
	UINT kSpotLightKind = 2u;
	UINT kPointLightKind = 3u;

};
