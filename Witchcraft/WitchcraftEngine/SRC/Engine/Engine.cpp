#include "Engine.h"

#include "HELPERS/Helpers.h"
#include "ECS/Component/BillboardComponent.h"
#include "ECS/Component/SkeletonComponent.h"
#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/SkinningRuntimeComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

KeyboardClass* Engine::GetKeyboard()
{
	return &keyboard;
}

MouseClass* Engine::GetMouse()
{
	return &mouse;
}

PhysicsSystem* Engine::GetphysicsSystem()
{
	return &physicsSystem;
}

ScriptingSystem* Engine::GetscriptingSystem()
{
	return &scriptingSystem;
}

ProjectSceneSystem* Engine::GetprojectSceneSystem()
{
	return &projectSceneSystem;
}

WitchcraECS* Engine::GetECS()
{
	return &ecs;
}

D3DWindow* Engine::GetD3DWindow()
{
	return m_dx;
}

ConsoleWindow* Engine::GetConsoleWindow()
{
	return m_editor != nullptr ? m_editor->GetConsoleWindow() : nullptr;
}

Editor* Engine::GetEditor()
{
	return m_editor;
}

void Engine::AddObject(std::wstring name, Transform* tf, CreateItem item, const std::wstring& materialName, CreateLightType lightType, SceneEntityType sceneType)
{
	ctrateObject.name = name;
	if (tf != nullptr)
		ctrateObject.transform = *tf;
	else
		ctrateObject.transform = Transform{};
	ctrateObject.item = item;
	ctrateObject.materialName = materialName.empty() ? L"autoMat" : materialName;
	ctrateObject.lightType = lightType;
	ctrateObject.sceneType = sceneType;
	b_createObject = true;
}

std::wstring Engine::ResolveDataModelPath(const std::wstring& fileName)
{
	if (fileName.empty())
		return L"";

	std::filesystem::path probe = std::filesystem::current_path();
	while (!probe.empty())
	{
		const std::filesystem::path candidate = probe / L"DATA" / L"Models" / fileName;
		if (std::filesystem::exists(candidate))
			return candidate.lexically_normal().wstring();

		const std::filesystem::path parent = probe.parent_path();
		if (parent == probe)
			break;
		probe = parent;
	}

	return (std::filesystem::path(L"DATA") / L"Models" / fileName).wstring();
}

bool Engine::IsNearlyZero(float value)
{
	return std::abs(value) <= kTransformEpsilon;
}

bool Engine::IsDefaultPosition(const DirectX::XMFLOAT3& position)
{
	return IsNearlyZero(position.x) && IsNearlyZero(position.y) && IsNearlyZero(position.z);
}

void Engine::ApplyDefaultLightPreset(EntityLightComponentData* lightData, Transform* transform, CreateLightType lightType)
{
	if (lightData == nullptr)
		return;

	switch (lightType)
	{
	case CreateAmbientLight:
		lightData->kind = kAmbientLightKind;
		lightData->type = kDirectionalShaderLightType;
		lightData->color = { 0.45f, 0.45f, 0.45f };
		lightData->power = 0.35f;
		lightData->castShadow = false;
		lightData->enableVolumetric = false;
		lightData->volumetricIntensity = 0.0f;
		lightData->volumetricAttenuationDistance = 0.0f;
		if (transform != nullptr)
			transform->rotation = kDefaultForwardLightRotation;
		break;
	case CreateSpotLight:
		lightData->kind = kSpotLightKind;
		lightData->type = kSpotShaderLightType;
		lightData->color = { 0.42f, 0.42f, 0.42f };
		lightData->power = 10.0f;
		lightData->castShadow = true;
		lightData->enableVolumetric = true;
		lightData->volumetricIntensity = 1.0f;
		lightData->volumetricAttenuationDistance = 20.0f;
		if (transform != nullptr)
		{
			transform->rotation = kDefaultForwardLightRotation;
			if (IsDefaultPosition(transform->position))
				transform->position = { 0.0f, 0.0f, -5.0f };
		}
		break;
	case CreatePointLight:
		lightData->kind = kPointLightKind;
		lightData->type = kPointShaderLightType;
		lightData->color = { 0.42f, 0.42f, 0.42f };
		lightData->power = 28.0f;
		lightData->castShadow = true;
		lightData->enableVolumetric = true;
		lightData->volumetricIntensity = 1.0f;
		lightData->volumetricAttenuationDistance = 20.0f;
		if (transform != nullptr)
		{
			transform->rotation = kDefaultForwardLightRotation;
			if (IsDefaultPosition(transform->position))
				transform->position = { 0.0f, 2.0f, -2.0f };
		}
		break;
	case CreateDirectionalLight:
	default:
		lightData->kind = kDirectionalLightKind;
		lightData->type = kDirectionalShaderLightType;
		lightData->color = { 0.42f, 0.42f, 0.42f };
		lightData->power = 1.2f;
		lightData->castShadow = true;
		lightData->enableVolumetric = true;
		lightData->volumetricIntensity = 1.0f;
		lightData->volumetricAttenuationDistance = 20.0f;
		if (transform != nullptr)
			transform->rotation = kDefaultDirectionalLightRotation;
		break;
	}
}

void Engine::EngineStart(D3DWindow* dx, Editor* editor, std::wstring MainPath)
{
	m_dx = dx;
	m_editor = editor;
	ecs.SetConsoleWindow(GetConsoleWindow());
	(void)MainPath;

	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> 正在初始化模型系统...");
	if (!modelSystem.Init(m_dx))
		EngineHelpers::AddLog(L"[Engine] -> 模型系统初始化失败！");
	/* --------------------------- */
	projectSceneSystem.Init(m_dx, &ecs, this);
	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> 初始化物理系统...");
	if (!physicsSystem.Init(m_dx))
		EngineHelpers::AddLog(L"[Engine] -> 物理系统初始化失败！");
	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> 正在初始化Lua脚本系统...");
	if (!scriptingSystem.Init())
		EngineHelpers::AddLog(L"[Engine] -> Lua脚本系统初始化失败！");
	/* --------------------------- */

	timer.Reset();
	ecs.Init();
	sceneLightSystem.SyncSceneLights(&ecs, m_dx);
	m_dx->RebuildRenderItemsFromEntities(&ecs);
}

void Engine::CreateDefaultSkyEntity()
{
	CreateSkyEntity(L"Sky");
}

void Engine::QueueCreateSkyEntity(const std::wstring& name, const std::wstring& skyTexturePath)
{
	createSkyRequest.name = name;
	createSkyRequest.skyTexturePath = skyTexturePath;
	b_createSkyEntity = true;
}

void Engine::CreateSkyEntity(const std::wstring& name)
{
	CreateSkyEntity(name, L"DATA/HDRIs/scythian_tombs_2_4k.png");
}

void Engine::CreateSkyEntity(const std::wstring& name, const std::wstring& skyTexturePath)
{
	std::wstring entityName = name.empty() ? L"天空" : name;
	if (!ecs.IsEntityNameAvailable(entityName, nullptr))
		return;

	AggregateGraphicObj* skyAggregateGraphicObj = m_dx->GetAggregateGraphicObj(L"shapeGeo");
	if (skyAggregateGraphicObj == nullptr)
		return;

	auto* skyEntity = ecs.CreateMeshEntity(entityName, nullptr);
	if (skyEntity == nullptr)
		return;

	if (!ecs.ConfigureMeshEntity(
		skyEntity,
		this,
		entityName,
		L"",
		entityName,
		天空渲染项目,
		m_dx->GetOrCreateSkyMaterial(skyTexturePath)))
	{
		return;
	}

	if (!ecs.SetMeshEntityExternalGeometry(skyEntity, L"shapeGeo", skyAggregateGraphicObj))
		return;

	ecs.SetEntitySceneType(skyEntity, SceneEntityType::Sky, false);
	ecs.SetEntityEditableLocalTransform(skyEntity, Transform{});
	m_dx->AddRenderItemsFromEntity(skyEntity, &ecs);
	ecs.SelectEntityForHierarchy(skyEntity);
}

void Engine::EngineProcess()
{
	timer.Tick();

	UpdateComponent(); /* update all entity transforms */
	const float frameDeltaTime = timer.DeltaTime();
	const bool shouldAdvancePlayRuntime = m_playModeActive && (!m_playModePaused || m_playModeStepRequested) && m_playModeTimeScale > 0.0f;
	const float playRuntimeDeltaTime = shouldAdvancePlayRuntime ? (std::max)(0.0f, frameDeltaTime * m_playModeTimeScale) : 0.0f;
	ecs.Update(frameDeltaTime);
	if (shouldAdvancePlayRuntime)
		scriptingSystem.UpdateRuntime(&ecs, playRuntimeDeltaTime);
	m_animationSystem.Update(&ecs, m_playModeActive ? playRuntimeDeltaTime : frameDeltaTime, m_playModeActive ? &scriptingSystem : nullptr);
	if (shouldAdvancePlayRuntime)
		physicsSystem.Update(playRuntimeDeltaTime, &ecs);
	m_playModeStepRequested = false;

	if(m_editor)
		m_editor->Update();

	// 先处理输入/编辑器中的相机变更，再更新渲染常量。
	// 否则透明排序与主 Pass 视图矩阵会出现一帧错位，表现为移动时闪烁/遮挡跳变。
	sceneLightSystem.SyncSceneLights(&ecs, m_dx);
	m_dx->Update();

}

void Engine::EngineShutdown()
{
	EngineHelpers::AddLog(L"[Engine] -> 关闭/清理...");
	StopPlayMode();
	physicsSystem.Shutdown();
	ecs.Clear();
	m_dx->DestroyRender();
}

bool Engine::StartPlayMode()
{
	if (m_playModeActive)
		return true;

	if (ConsoleWindow* consoleWindow = GetConsoleWindow())
		consoleWindow->HandlePlayModeStarting();

	m_playModeSceneSnapshot = WSceneFileData{};
	m_playModeSceneSnapshotValid = false;
	if (!projectSceneSystem.CaptureSceneSnapshot(&m_playModeSceneSnapshot))
	{
		EngineHelpers::AddLog(L"[Engine] -> 启动播放模式失败：无法捕获场景快照。");
		return false;
	}
	m_playModeSceneSnapshotValid = true;

	if (!scriptingSystem.StartRuntime(&ecs))
	{
		m_playModeSceneSnapshot = WSceneFileData{};
		m_playModeSceneSnapshotValid = false;
		return false;
	}

	m_playModeActive = true;
	m_playModePaused = false;
	m_playModeStepRequested = false;
	m_playModeTimeScale = 1.0f;
	return true;
}

void Engine::StopPlayMode()
{
	if (!m_playModeActive)
		return;

	scriptingSystem.StopRuntime(&ecs);
	m_playModeActive = false;
	m_playModePaused = false;
	m_playModeStepRequested = false;
	ecs.ClearHierarchySelection();

	if (m_playModeSceneSnapshotValid)
	{
		if (!projectSceneSystem.RestoreSceneSnapshot(m_playModeSceneSnapshot))
			EngineHelpers::AddLog(L"[Engine] -> StopPlayMode警告：场景快照还原失败。");
	}

	m_playModeSceneSnapshot = WSceneFileData{};
	m_playModeSceneSnapshotValid = false;
}

bool Engine::ApplyPlayModeRuntimeChanges()
{
	if (!m_playModeActive)
		return false;
	if (!m_playModePaused)
	{
		EngineHelpers::AddLog(L"[Engine] -> ApplyPlayModeRuntimeChanges blocked: Play Mode must be paused.");
		return false;
	}
	if (!m_playModeSceneSnapshotValid)
	{
		EngineHelpers::AddLog(L"[Engine] -> ApplyPlayModeRuntimeChanges failed: original Play Mode snapshot is missing.");
		return false;
	}

	WSceneFileData runtimeSceneSnapshot;
	if (!projectSceneSystem.CaptureSceneSnapshot(&runtimeSceneSnapshot))
	{
		EngineHelpers::AddLog(L"[Engine] -> ApplyPlayModeRuntimeChanges failed: could not capture runtime scene snapshot.");
		return false;
	}

	std::unordered_map<std::wstring, const WSceneEntityData*> runtimeEntitiesById;
	for (const WSceneEntityData& runtimeEntity : runtimeSceneSnapshot.Entities)
	{
		if (!runtimeEntity.Id.empty())
			runtimeEntitiesById[runtimeEntity.Id] = &runtimeEntity;
	}

	WSceneFileData appliedSceneSnapshot = m_playModeSceneSnapshot;
	for (WSceneEntityData& originalEntity : appliedSceneSnapshot.Entities)
	{
		auto runtimeIt = runtimeEntitiesById.find(originalEntity.Id);
		if (runtimeIt != runtimeEntitiesById.end() && runtimeIt->second != nullptr)
			originalEntity.LocalTransform = runtimeIt->second->LocalTransform;
	}

	StopPlayMode();

	if (!projectSceneSystem.RestoreSceneSnapshot(appliedSceneSnapshot))
	{
		EngineHelpers::AddLog(L"[Engine] -> ApplyPlayModeRuntimeChanges failed: could not restore merged scene snapshot.");
		return false;
	}

	return true;
}

bool Engine::IsPlayModeActive() const
{
	return m_playModeActive;
}

void Engine::PausePlayMode()
{
	if (!m_playModeActive)
		return;

	m_playModePaused = true;
}

void Engine::ResumePlayMode()
{
	if (!m_playModeActive)
		return;

	m_playModePaused = false;
	m_playModeStepRequested = false;
}

void Engine::TogglePlayModePaused()
{
	if (!m_playModeActive)
		return;

	m_playModePaused = !m_playModePaused;
	if (!m_playModePaused)
		m_playModeStepRequested = false;
}

bool Engine::IsPlayModePaused() const
{
	return m_playModeActive && m_playModePaused;
}

bool Engine::RequestPlayModeStepFrame()
{
	if (!m_playModeActive || !m_playModePaused || m_playModeTimeScale <= 0.0f)
		return false;

	m_playModeStepRequested = true;
	return true;
}

float Engine::GetPlayModeTimeScale() const
{
	return m_playModeTimeScale;
}

void Engine::SetPlayModeTimeScale(float timeScale)
{
	m_playModeTimeScale = std::clamp(timeScale, 0.0f, 8.0f);
}

void Engine::UpdateComponent()
{
	if (b_createSkyEntity)
	{
		CreateSkyEntity(createSkyRequest.name, createSkyRequest.skyTexturePath);
		b_createSkyEntity = false;
		createSkyRequest = SkyCreateRequest{};
	}

	if (b_createObject)
	{
		SceneEntityBase* createdRenderableEntity = nullptr;
		SceneEntityBase* createdEntity = nullptr;

		auto createPrimitiveEntity = [&](const std::wstring& modelPath) -> SceneEntityBase*
		{
			std::vector<Mesh> model = assimpLoader.LoadRawModel(modelPath);
			if (model.empty())
				return nullptr;

			auto* entity = ecs.CreateMeshEntity(ctrateObject.name, nullptr);
			if (entity == nullptr)
				return nullptr;

			if (!ecs.ConfigureMeshEntity(
				entity,
				this,
				ctrateObject.name,
				modelPath,
				ctrateObject.name,
				不透明物体渲染项目,
				ctrateObject.materialName.empty() ? L"autoMat" : ctrateObject.materialName))
			{
				return nullptr;
			}

			if (!ecs.AppendMeshEntityVertices(entity, model[0].vertices))
				return nullptr;
			if (!ecs.AppendMeshEntityIndices(entity, model[0].indices32))
				return nullptr;
			if (!ecs.SetupMeshEntity(entity, m_dx))
				return nullptr;
			return entity;
		};

		switch (ctrateObject.item)
		{
		case CreateItem::EmptyItem:
		{
			auto* entity = ecs.CreateBasicEntity(ctrateObject.name, nullptr, ComponentType::Co_Unk);
			ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
			ecs.SetEntitySceneType(entity, ctrateObject.sceneType, false);
			createdEntity = entity;
		}
		break;
		case CreateItem::SkeletonItem:
		{
			auto* entity = ecs.CreateSkeletonEntity(ctrateObject.name, nullptr);
			if (entity != nullptr)
			{
				ecs.AddComponent<SkeletonComponent>(entity);
				ecs.AddComponent<AnimatorComponent>(entity);
				ecs.AddComponent<SkinningRuntimeComponent>(entity);
				ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
				ecs.SetEntitySceneType(entity, ctrateObject.sceneType, false);
				createdEntity = entity;
			}
		}
		break;
		case CreateItem::CameraItem:
		{
			auto* entity = ecs.CreateCameraEntity(ctrateObject.name, nullptr);
			ecs.SetCameraEntityEngine(entity, this);

			ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
			ecs.SetEntitySceneType(entity, ctrateObject.sceneType, false);
			createdEntity = entity;
		}
		break;
		case CreateItem::LightItem:
		{
			SceneEntityBase* lightParent = nullptr;
			if (ctrateObject.lightType == CreateAmbientLight)
				lightParent = ecs.EnsureEnvironmentEntity();

			auto* entity = ecs.CreateLightEntity(ctrateObject.name, lightParent);
			if (entity != nullptr)
			{
				EntityLightComponentData lightData;
				ApplyDefaultLightPreset(&lightData, &ctrateObject.transform, ctrateObject.lightType);
				ecs.SetEntityLightSnapshot(entity, lightData);
				ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
				ecs.SetEntitySceneType(entity, ctrateObject.sceneType, false);
				if (m_dx != nullptr)
				{
					m_dx->FreshenLightCBs();
					m_dx->FreshenMaterialCBs();
					m_dx->FreshenObjectCBs();
				}
				createdEntity = entity;
			}
		}
		break;
		case CreateItem::BillboardItem:
		{
			auto* entity = ecs.CreateBasicEntity(ctrateObject.name, nullptr, ComponentType::Co_Billboard);
			if (entity != nullptr)
			{
				BillboardComponent* billboardComponent = ecs.AddBillboardComponent(entity);
				if (billboardComponent != nullptr)
				{
					billboardComponent->SetMaterialName(
						ctrateObject.materialName.empty() ? L"autoMat" : ctrateObject.materialName);
					billboardComponent->SetSize(
						(std::max)(ctrateObject.transform.scale.x, 0.001f),
						(std::max)(ctrateObject.transform.scale.y, 0.001f));
				}

				ctrateObject.transform.scale = { 1.0f, 1.0f, 1.0f };
				ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
				ecs.SetEntitySceneType(entity, ctrateObject.sceneType, false);
				createdEntity = entity;
				createdRenderableEntity = entity;
			}
		}
		break;
		case CreateItem::BoxItem:
		case CreateItem::SphereItem:
		case CreateItem::CapsuleItem:
		case CreateItem::PlaneItem:
		case CreateItem::UnknownItem:
		default:
		{
			std::wstring modelPath = ResolveDataModelPath(L"Cube.obj");
			switch (ctrateObject.item)
			{
			case CreateItem::SphereItem:
				modelPath = ResolveDataModelPath(L"Sphere.obj");
				break;
			case CreateItem::CapsuleItem:
				modelPath = ResolveDataModelPath(L"Capsule.obj");
				break;
			case CreateItem::PlaneItem:
				modelPath = ResolveDataModelPath(L"Plane.obj");
				break;
			case CreateItem::BoxItem:
			case CreateItem::UnknownItem:
			default:
				modelPath = ResolveDataModelPath(L"Cube.obj");
				break;
			}

			SceneEntityBase* entity = createPrimitiveEntity(modelPath);
			if (entity != nullptr)
			{
				ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
				ecs.SetEntitySceneType(entity, ctrateObject.sceneType);
				createdEntity = entity;
				createdRenderableEntity = entity;
			}
		}
		break;
		}

		if (createdRenderableEntity != nullptr)
			m_dx->AddRenderItemsFromEntity(createdRenderableEntity, &ecs);
		if (createdEntity != nullptr)
			ecs.SelectEntityForHierarchy(createdEntity);

		b_createObject = false;
		ctrateObject = Object{};
	}
}

void Engine::GamePlayUpdate()
{
	// 更新用户输入
	{
		while (!keyboard.CharBufferIsEmpty())
		{
			BYTE ch = keyboard.ReadChar();
		}

		while (!keyboard.KeyBufferIsEmpty())
		{
			KeyboardEvent kbe = keyboard.ReadKey();
			BYTE keycode = kbe.GetKeyCode();
			if (kbe.IsPress())
			{
			}

		}

		{
			UINT MovementDirection = MOVE_NOT_SPECIFIDE;
			bool MoveCamera = false;

			if (keyboard.KeyIsPressed('W'))
			{
				MovementDirection = MovementDirection + MOVE_DEEPEN;
				MoveCamera = true;
			}
			if (keyboard.KeyIsPressed('S'))
			{
				MovementDirection = MovementDirection + MOVE_FROMAW;
				MoveCamera = true;
			}
			if (keyboard.KeyIsPressed('A'))
			{
				MovementDirection = MovementDirection + MOVE_LEFT;
				MoveCamera = true;
			}
			if (keyboard.KeyIsPressed('D'))
			{
				MovementDirection = MovementDirection + MOVE_RIGHT;
				MoveCamera = true;
			}
			if (keyboard.KeyIsPressed(VK_SPACE))
			{
			}

			if (MoveCamera && (MovementDirection != MOVE_NOT_SPECIFIDE))
			{
				DirectX::XMFLOAT3 distance(0.0f,0.0f,0.0f);
				if (MovementDirection == MOVE_UP)
					distance.y = -1.0f;
				else if (MovementDirection == MOVE_DOWN)
					distance.y = +1.0f;
				else if (MovementDirection == MOVE_DEEPEN)
					distance.z = -1.0f;
				else if (MovementDirection == MOVE_FROMAW)
					distance.z = +1.0f;
				else if (MovementDirection == MOVE_LEFT)
					distance.x = -1.0f;
				else if (MovementDirection == MOVE_RIGHT)
					distance.x = +1.0f;
				else if (MovementDirection == (MOVE_UP + MOVE_LEFT))
				{
					distance.y = -1.0f;
					distance.x = -1.0f;
				}
				else if (MovementDirection == (MOVE_UP + MOVE_RIGHT))
				{
					distance.y = -1.0f;
					distance.x = +1.0f;
				}
				else if (MovementDirection == (MOVE_DOWN + MOVE_LEFT))
				{
					distance.y = +1.0f;
					distance.x = -1.0f;
				}
				else if (MovementDirection == (MOVE_DOWN + MOVE_RIGHT))
				{
					distance.y = +1.0f;
					distance.x = +1.0f;
				}
				else if (MovementDirection == (MOVE_LEFT + MOVE_DEEPEN))
				{
					distance.x = -1.0f;
					distance.z = -1.0f;
				}
				else if (MovementDirection == (MOVE_LEFT + MOVE_FROMAW))
				{
					distance.x = -1.0f;
					distance.z = +1.0f;
				}
				else if (MovementDirection == (MOVE_RIGHT + MOVE_DEEPEN))
				{
					distance.x = +1.0f;
					distance.z = -1.0f;
				}
				else if (MovementDirection == (MOVE_RIGHT + MOVE_FROMAW))
				{
					distance.x = +1.0f;
					distance.z = +1.0f;
				}
				m_dx->MoveCamera(timer.DeltaTime(), distance);
				MoveCamera = false;
			}
		}
	}
}

void Engine::TimerStart()
{
	timer.Start();
}

void Engine::TimerStop()
{
	timer.Stop();
}
