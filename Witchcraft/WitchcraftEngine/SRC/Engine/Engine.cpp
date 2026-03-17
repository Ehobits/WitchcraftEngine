#include "Engine.h"

#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
#include "HELPERS/Helpers.h"
#include <memory>

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

void Engine::AddObject(std::wstring name, Transform* tf, CreateItem item, const std::wstring& materialName)
{
	ctrateObject.name = name;
	if (tf != nullptr)
		ctrateObject.transform = *tf;
	else
		ctrateObject.transform = Transform{};
	ctrateObject.item = item;
	ctrateObject.materialName = materialName.empty() ? L"autoMat" : materialName;
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
	projectSceneSystem.Init(m_dx);
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
	m_dx->RebuildRenderItemsFromEntities(ecs.GetRootEntities(), &ecs);
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
	if (ecs.GetEntity(entityName) != nullptr)
		return;

	AggregateGraphicObj* skyAggregateGraphicObj = m_dx->GetAggregateGraphicObj(L"shapeGeo");
	if (skyAggregateGraphicObj == nullptr)
		return;

	auto* skyEntity = new SceneEntityBase();

	auto* generalComponent = new GeneralComponent();
	generalComponent->SetName(entityName);
	skyEntity->AddChildComponent(L"GeneralComponent", generalComponent);

	auto* transformComponent = new TransformComponent();
	skyEntity->AddChildComponent(L"TransformComponent", transformComponent);

	auto* meshComponent = new MeshComponent();
	meshComponent->SetName(entityName);
	meshComponent->SetMeshName(entityName);
	meshComponent->SetExternalRenderGeometry(L"shapeGeo", skyAggregateGraphicObj);
	meshComponent->SetRenderLayerIndex(天空渲染项目);
	meshComponent->SetDefaultMaterialName(m_dx->GetOrCreateSkyMaterial(skyTexturePath));
	skyEntity->AddChildComponent(L"MeshComponent", meshComponent);

	SceneEntityBase* previousSelection = ecs.GetSelectedEntity();
	ecs.SetSelectedEntity(nullptr);
	ecs.CreateEntity(entityName, skyEntity);
	ecs.SetEntityEditableLocalTransform(skyEntity, Transform{});
	ecs.SetSelectedEntity(previousSelection);
	m_dx->RebuildRenderItemsFromEntities(ecs.GetRootEntities(), &ecs);
}

void Engine::EngineProcess()
{
	timer.Tick();

	UpdateComponent(); /* update all entity transforms */
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
		auto addBaseComponents = [&](SceneEntityBase* entity)
		{
			auto* generalComponent = new GeneralComponent();
			generalComponent->SetName(ctrateObject.name);
			entity->AddChildComponent(L"GeneralComponent", generalComponent);

			auto* transformComponent = new TransformComponent();
			entity->AddChildComponent(L"TransformComponent", transformComponent);
		};

		auto createPrimitiveEntity = [&](const std::wstring& modelPath) -> SceneEntityBase*
		{
			std::vector<Mesh> model = assimpLoader.LoadRawModel(modelPath);
			if (model.empty())
				return nullptr;

			auto* entity = new SceneEntityBase();
			addBaseComponents(entity);

			auto* meshComponent = new MeshComponent();
			meshComponent->SetName(ctrateObject.name);
			meshComponent->SetFileName(modelPath);
			meshComponent->SetMeshName(ctrateObject.name);
			meshComponent->SetRenderLayerIndex(不透明物体渲染项目);
			meshComponent->SetDefaultMaterialName(ctrateObject.materialName.empty() ? L"autoMat" : ctrateObject.materialName);

			for (const auto& vertex : model[0].vertices)
			{
				meshComponent->AddVertices(vertex);
			}

			for (const auto index : model[0].indices32)
			{
				meshComponent->AddIndices(index);
			}

			entity->AddChildComponent(L"MeshComponent", meshComponent);
			meshComponent->SetupMesh(entity->GetChildrenContainer(), m_dx, meshComponent->GetIndexCount(), meshComponent->GetVertexCount());
			return entity;
		};

		switch (ctrateObject.item)
		{
		case CreateItem::EmptyItem:
		{
			auto* entity = new SceneEntityBase();
			addBaseComponents(entity);
			ecs.CreateEntity(ctrateObject.name, entity);
			ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
		}
		break;
		case CreateItem::CameraItem:
		{
			auto* entity = new SceneEntityBase();
			addBaseComponents(entity);

			auto* cameraComponent = new CameraComponent();
			cameraComponent->SetDXWindow(m_dx);
			entity->AddChildComponent(L"CameraComponent", cameraComponent);

			ecs.CreateEntity(ctrateObject.name, entity);
			ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
		}
		break;
		case CreateItem::BoxItem:
		case CreateItem::SphereItem:
		case CreateItem::CapsuleItem:
		case CreateItem::PlaneItem:
		case CreateItem::UnknownItem:
		default:
		{
			std::wstring modelPath = L"DATA\\Models\\Cube.obj";
			switch (ctrateObject.item)
			{
			case CreateItem::SphereItem:
				modelPath = L"DATA\\Models\\Sphere.obj";
				break;
			case CreateItem::CapsuleItem:
				modelPath = L"DATA\\Models\\Capsule.obj";
				break;
			case CreateItem::PlaneItem:
				modelPath = L"DATA\\Models\\Plane.obj";
				break;
			case CreateItem::BoxItem:
			case CreateItem::UnknownItem:
			default:
				modelPath = L"DATA\\Models\\Cube.obj";
				break;
			}

			SceneEntityBase* entity = createPrimitiveEntity(modelPath);
			if (entity != nullptr)
			{
				ecs.CreateEntity(ctrateObject.name, entity);
				ecs.SetEntityEditableLocalTransform(entity, ctrateObject.transform);
			}
		}
		break;
		}
			m_dx->RebuildRenderItemsFromEntities(ecs.GetRootEntities(), &ecs);
		b_createObject = false;
		ctrateObject = Object{};
	}
}

void Engine::GamePlayUpdate()
{
	//if (game->GetGameState() == GameState::GamePlay)
	//{
	//	physicsSystem->Update();

	//	auto view = ComponentServices->registry.view<ScriptingComponent>();
	//	for (auto entity : view)
	//		ComponentServices->registry.get<ScriptingComponent>(entity).lua_call_update();
	//}
	
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
