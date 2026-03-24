#include "Engine.h"

#include "HELPERS/Helpers.h"
#include <filesystem>
#include <memory>

namespace
{
	constexpr float kDirectionalShaderLightType = 0.0f;
	constexpr float kPointShaderLightType = 1.0f;
	constexpr float kSpotShaderLightType = 2.0f;
	constexpr DirectX::XMFLOAT3 kDefaultDirectionalLightRotation = { 35.2643897f, 45.0f, 0.0f };
	constexpr DirectX::XMFLOAT3 kDefaultForwardLightRotation = { 0.0f, 0.0f, 0.0f };

	std::wstring ResolveDataModelPath(const std::wstring& fileName)
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

	constexpr unsigned int kAmbientLightKind = 0u;
	constexpr unsigned int kDirectionalLightKind = 1u;
	constexpr unsigned int kPointLightKind = 2u;
	constexpr unsigned int kSpotLightKind = 3u;

	void ApplyDefaultLightPreset(EntityLightComponentData* lightData, Transform* transform, CreateLightType lightType)
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
			if (transform != nullptr)
				transform->rotation = kDefaultForwardLightRotation;
			break;
		case CreateSpotLight:
			lightData->kind = kSpotLightKind;
			lightData->type = kSpotShaderLightType;
			lightData->color = { 0.42f, 0.42f, 0.42f };
			lightData->power = 10.0f;
			lightData->castShadow = true;
			if (transform != nullptr)
				transform->rotation = kDefaultForwardLightRotation;
			break;
		case CreatePointLight:
			lightData->kind = kPointLightKind;
			lightData->type = kPointShaderLightType;
			lightData->color = { 0.42f, 0.42f, 0.42f };
			lightData->power = 28.0f;
			lightData->castShadow = true;
			if (transform != nullptr)
				transform->rotation = kDefaultForwardLightRotation;
			break;
		case CreateDirectionalLight:
		default:
			lightData->kind = kDirectionalLightKind;
			lightData->type = kDirectionalShaderLightType;
			lightData->color = { 0.42f, 0.42f, 0.42f };
			lightData->power = 1.2f;
			lightData->castShadow = true;
			if (transform != nullptr)
				transform->rotation = kDefaultDirectionalLightRotation;
			break;
		}
	}
}

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

void Engine::AddObject(std::wstring name, Transform* tf, CreateItem item, const std::wstring& materialName, CreateLightType lightType)
{
	ctrateObject.name = name;
	if (tf != nullptr)
		ctrateObject.transform = *tf;
	else
		ctrateObject.transform = Transform{};
	ctrateObject.item = item;
	ctrateObject.materialName = materialName.empty() ? L"autoMat" : materialName;
	ctrateObject.lightType = lightType;
	b_createObject = true;
}

void Engine::EngineStart(D3DWindow* dx, Editor* editor, std::wstring MainPath)
{
	m_dx = dx;
	m_editor = editor;
	(void)MainPath;

	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> Initializing Model System...");
	if (!modelSystem.Init(m_dx))
		EngineHelpers::AddLog(L"[Engine] -> Failed to initialize Model System!");
	/* --------------------------- */
	projectSceneSystem.Init(m_dx, &ecs, this);
	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> Initializing Physics System...");
	if (!physicsSystem.Init(m_dx))
		EngineHelpers::AddLog(L"[Engine] -> Failed to initialize Physics System!");
	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> Initializing Lua Script System...");
	if (!scriptingSystem.Init())
		EngineHelpers::AddLog(L"[Engine] -> Failed to initialize Lua Script System!");
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

	ecs.SetEntityEditableLocalTransform(skyEntity, Transform{});
	m_dx->AddRenderItemsFromEntity(skyEntity, &ecs);
}

void Engine::EngineProcess()
{
	timer.Tick();

	UpdateComponent(); /* update all entity transforms */
	sceneLightSystem.SyncSceneLights(&ecs, m_dx);
	m_dx->Update();
	ecs.Update(timer.DeltaTime());

	if(m_editor)
		m_editor->Update();

}

void Engine::EngineShutdown()
{
	EngineHelpers::AddLog(L"[Engine] -> Shutting/Cleaning...");
	ecs.Clear();
	m_dx->DestroyRender();
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
		}
		break;
		case CreateItem::CameraItem:
		{
			auto* entity = ecs.CreateCameraEntity(ctrateObject.name, nullptr);
			ecs.SetCameraEntityEngine(entity, this);

			ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
		}
		break;
		case CreateItem::LightItem:
		{
			auto* entity = ecs.CreateLightEntity(ctrateObject.name, nullptr);
			if (entity != nullptr)
			{
				EntityLightComponentData lightData;
				ApplyDefaultLightPreset(&lightData, &ctrateObject.transform, ctrateObject.lightType);
				ecs.SetEntityLightSnapshot(entity, lightData);
				ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
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
				createdRenderableEntity = entity;
			}
		}
		break;
		}

		if (createdRenderableEntity != nullptr)
			m_dx->AddRenderItemsFromEntity(createdRenderableEntity, &ecs);

		b_createObject = false;
		ctrateObject = Object{};
	}
}

void Engine::GamePlayUpdate()
{
	// Legacy GameRunTime update loop removed; unfinished runtime preview path stays disabled.

	
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
