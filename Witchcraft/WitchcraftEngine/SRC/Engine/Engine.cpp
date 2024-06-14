#include "Engine.h"

#include "ServicesContainer/COMPONENT/GeneralComponent.h"
#include "ServicesContainer/COMPONENT/CameraComponent.h"
#include "ServicesContainer/COMPONENT/TransformComponent.h"
#include "ServicesContainer/COMPONENT/RigidbodyComponent.h"
#include "HELPERS/Helpers.h"

KeyboardClass* Engine::GetKeyboard()
{
	return &keyboard;
}

MouseClass* Engine::GetMouse()
{
	return &mouse;
}

ServicesContainer* Engine::GetEntity()
{
	return &ComponentServices;
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

void Engine::EngineStart(D3DWindow* dx, Editor* editor, std::wstring MainPath)
{
	m_dx = dx;
	m_editor = editor;

	//game->InitTime();
	assimpLoader.LoadModel(MainPath);

	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> Initializing Model System...");
	if (!modelSystem.Init(m_dx))
		EngineHelpers::AddLog(L"[Engine] -> Failed to initialize Model System!");
	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> Initializing Component Component System...");
	if (!ComponentServices.Init(m_dx, L"未命名场景"))
		EngineHelpers::AddLog(L"[Engine] -> Failed to initialize Component Component System!");
	projectSceneSystem.Init(m_dx, &ComponentServices);
	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> Initializing Physics System...");
	if (!physicsSystem.Init(m_dx))
		EngineHelpers::AddLog(L"[Engine] -> Failed to initialize Physics System!");
	/* --------------------------- */
	EngineHelpers::AddLog(L"[Engine] -> Initializing Lua Script System...");
	if (!scriptingSystem.Init(&ComponentServices))
		EngineHelpers::AddLog(L"[Engine] -> Failed to initialize Lua Script System!");
	/* --------------------------- */

	/*
	unsigned int ii = 16;
	float xx = 0.0f;
	float yy = 0.0f;
	float zz = 0.0f;
	for (unsigned int aa = 0; aa < ii; aa++)
	{
		for (unsigned int bb = 0; bb < ii; bb++)
		{
			for (unsigned int cc = 0; cc < ii; cc++)
			{
				auto entity = ComponentServices->CreateEntity();
				ComponentServices->CreateCubeEntity(entity);
				auto& transformComponent = ComponentServices->GetComponent<TransformComponent>(entity);
				Vector3 cube = transformComponent.GetPosition();
				transformComponent.SetPosition(Vector3(xx, yy, zz));
				ComponentServices->AddComponent<RigidBodyComponent>(entity);
				ComponentServices->GetComponent<RigidBodyComponent>(entity).CreateActor();
				ComponentServices->GetComponent<PhysicsComponent>(entity).AddBoxCollider();


				yy = yy + 2.0f;
			}
			xx = xx + 2.0f;
			yy = 0.0f;
		}
		zz = zz + 2.0f;
		xx = 0.0f;
	}

	auto entity = ComponentServices->CreateEntity();
	ComponentServices->CreateCubeEntity(entity);
	auto& transformComponent = ComponentServices->GetComponent<TransformComponent>(entity);
	Vector3 cube = transformComponent.GetPosition();
	transformComponent.SetPosition(Vector3(16.0f, 16.0f, -16.0f));
	transformComponent.SetScale(Vector3(5.0f, 5.0f, 5.0f));
	ComponentServices->AddComponent<RigidBodyComponent>(entity);
	ComponentServices->GetComponent<RigidBodyComponent>(entity).CreateActor();
	ComponentServices->GetComponent<PhysicsComponent>(entity).AddBoxCollider();
	ComponentServices->GetComponent<RigidBodyComponent>(entity).AddForce(Vector3(0.0f, 0.0f, 1000.0f));
	ComponentServices->GetComponent<RigidBodyComponent>(entity).SetMass(100.0f);
	*/
	timer.Reset();
}

void Engine::EngineProcess()
{
	timer.Tick();

	//DirectX::XMMATRIX projectionMatrix = MathHelps::Identity;
	//DirectX::XMMATRIX viewMatrix = MathHelps::Identity;

	//GamePlayUpdate(DeltaTime); /* update physics, scripts when playing */
	UpdateComponent(); /* update all entity transforms */
	m_dx->Update();
	if(m_editor)
		m_editor->Update();

	//if (game->GetGameState() == GameState::GamePlay)
	//{
	//	DirectX::XMMATRIX projectionMatrix = MathHelps::Identity;
	//	DirectX::XMMATRIX viewMatrix = MathHelps::Identity;
	//	bool goodCamera = FindGoodCamera(projectionMatrix, viewMatrix);
	//	if (goodCamera)
	//	{
	//		game->BeginTime();
	//		{
	//			RenderEnvironment(
	//				projectionMatrix,
	//				viewMatrix,
	//				clearColor,
	//				game->GetRenderTargetView(),
	//				game->GetDepthStencilView(),
	//				game->GetViewport(),
	//				game->GetSwapChain());
	//		}
	//		game->EndTime();
	//	}
	//}
}

void Engine::EngineShutdown()
{
	EngineHelpers::AddLog(L"[Engine] -> Shutting/Cleaning...");
	modelSystem.Shutdown();
	physicsSystem.Shutdown();
	m_dx->Shutdown();
}

void Engine::UpdateComponent()
{
	//Transform matrix;

	//auto& pGC = ComponentServices.registry.get<GeneralComponent>(entity);
	//if (ComponentServices.registry.any_of<TransformComponent>(entity))
	//{
	//	auto& pTC = ComponentServices.registry.get<TransformComponent>(entity);
	//	matrix = pTC.GetTransform();
	//}

	//for (size_t i = 0; i < pGC.GetChildren().size(); i++)
	//{
	//	entt::entity child = pGC.GetChildren()[i];
	//	if (ComponentServices.registry.any_of<TransformComponent>(child))
	//	{
	//		auto& chTC = ComponentServices.registry.get<TransformComponent>(child);
	//		chTC.SetTransform(matrix);
	//	}

	//	UpdateComponent(child);
	//}
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
				//this->gfx.Camera3D.AdjustPosition(0.0f, Camera3DSpeed * dt, 0.0f);
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
