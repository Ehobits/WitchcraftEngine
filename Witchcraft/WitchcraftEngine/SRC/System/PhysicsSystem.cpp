#include "PhysicsSystem.h"
#include "HELPERS/Helpers.h"
#include "ServicesContainer/COMPONENT/TransformComponent.h"
#include "String/SStringUtils.h"

bool PhysicsSystem::Init(D3DWindow* dx)
{
	physicsProcesor = PhysicsProcesor::xGPU;

	//gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);

	//gPvd = PxCreatePvd(*gFoundation);
	//transport = physx::PxDefaultPvdSocketTransportCreate(SString::WstringToUTF8(PVD_HOST).c_str(), PVD_PORT, 10);
	//if (!gPvd->connect(*transport, physx::PxPvdInstrumentationFlag::eALL))
	//	StarHelpers::AddLog(L"[PhysX] -> PhysX Visual Debugger is not connected!\n[PhysX] -> HOST %s PORT %i", PVD_HOST, PVD_PORT);

	//gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, physx::PxTolerancesScale(), true, gPvd);

	//physx::PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
	//sceneDesc.gravity = NONE_GRAVITY;

	//if (physicsProcesor == PhysicsProcesor::xCPU)
	//{
	//	gDispatcher = physx::PxDefaultCpuDispatcherCreate(2);
	//}
	//else if (physicsProcesor == PhysicsProcesor::xGPU)
	//{
	//	physx::PxCudaContextManagerDesc cudaContextManagerDesc;
	//	gCudaContextManager = PxCreateCudaContextManager(*gFoundation, cudaContextManagerDesc, PxGetProfilerCallback());
	//	//cudaContextManagerDesc.graphicsDevice = dx->GetDevice();
	//	if (gCudaContextManager) if (!gCudaContextManager->contextIsValid()) StarHelpers::AddLog(L"[PhysX] -> Cuda Context Manager Error!");

	//	gDispatcher = physx::PxDefaultCpuDispatcherCreate(4);			//Create a CPU dispatcher using 4 worther threads
	//	sceneDesc.cpuDispatcher = gDispatcher;
	//	sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

	//	sceneDesc.flags |= physx::PxSceneFlag::eENABLE_GPU_DYNAMICS;
	//	sceneDesc.flags |= physx::PxSceneFlag::eENABLE_PCM;
	//	sceneDesc.flags |= physx::PxSceneFlag::eENABLE_STABILIZATION;
	//	sceneDesc.broadPhaseType = physx::PxBroadPhaseType::eGPU;
	//	sceneDesc.gpuMaxNumPartitions = 8;
	//}
	//else if (physicsProcesor == PhysicsProcesor::xUnknown)
	//{
	//	StarHelpers::AddLog(L"[PhysX] -> 处理器错误，未选定用GPU还是CPU进行处理!");
	//}

	//sceneDesc.cpuDispatcher = gDispatcher;
	//sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

	//gScene = gPhysics->createScene(sceneDesc);
	//physx::PxPvdSceneClient* pvdClient = gScene->getScenePvdClient();
	//if (pvdClient)
	//{
	//	pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
	//	pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
	//	pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
	//}

	/***
	gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
	gPvd = PxCreatePvd(*gFoundation);
	transport = physx::PxDefaultPvdSocketTransportCreate(PVD_HOST, PVD_PORT, 10);
	if (!gPvd->connect(*transport, physx::PxPvdInstrumentationFlag::eALL))
		StarHelpers::AddLog("[PhysX] -> PhysX Visual Debugger is not connected!\n[PhysX] -> HOST %s PORT %i", PVD_HOST, PVD_PORT);
	gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, physx::PxTolerancesScale(), true, gPvd);
	physx::PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
	sceneDesc.gravity = NONE_GRAVITY;
	gDispatcher = physx::PxDefaultCpuDispatcherCreate(2);
	sceneDesc.cpuDispatcher = gDispatcher;
	sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
	gScene = gPhysics->createScene(sceneDesc);
	physx::PxPvdSceneClient* pvdClient = gScene->getScenePvdClient();
	if (pvdClient)
	{
		pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
		pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
		pvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
	}
	***/

	return true;
}

void PhysicsSystem::Update(float DeltaTime)
{
	//gScene->simulate(1.0f / 60.0f);
	//gScene->simulate(DeltaTime);
	//gScene->fetchResults(true);
}

void PhysicsSystem::Shutdown()
{
	//if (gScene)              gScene->release();
	//if (gDispatcher)         gDispatcher->release();
	//if (gPhysics)            gPhysics->release();
	//if (gPvd)                gPvd->release();
	//if (transport)           transport->release();
	//if (gCudaContextManager) gCudaContextManager->release();
	//if (gFoundation)         gFoundation->release();
}

//physx::PxPhysics* PhysicsSystem::GetPhysics()
//{
//	return gPhysics;
//}
//physx::PxScene* PhysicsSystem::GetScene()
//{
//	return gScene;
//}
//
//physx::PxShape* BoxColliderBuffer::GetShape() const
//{
//	return pxShape;
//}
//physx::PxMaterial* BoxColliderBuffer::GetMaterial() const
//{
//	return pxMaterial;
//}
void BoxColliderBuffer::SetStaticFriction(float value)
{
	if (value < 0.0f) return;
	//if (pxMaterial) pxMaterial->setStaticFriction(value);
}
//float BoxColliderBuffer::GetStaticFriction()
//{
//	//if (!pxMaterial) return 0.0f;
//	return pxMaterial->getStaticFriction();
//}
void BoxColliderBuffer::SetDynamicFriction(float value)
{
	if (value < 0.0f) return;
	//if (pxMaterial) pxMaterial->setDynamicFriction(value);
}
//float BoxColliderBuffer::GetDynamicFriction()
//{
//	if (!pxMaterial) return 0.0f;
//	return pxMaterial->getDynamicFriction();
//}
void BoxColliderBuffer::SetRestitution(float value)
{
	if (value < 0.0f) return;
	//if (pxMaterial) pxMaterial->setRestitution(value);
}
//float BoxColliderBuffer::GetRestitution()
//{
//	if (!pxMaterial) return 0.0f;
//	return pxMaterial->getRestitution();
//}
void BoxColliderBuffer::SetCenter(DirectX::XMFLOAT3 value)
{
	//if (pxShape) pxShape->setLocalPose(physx::PxTransform(value.x, value.y, value.z));
}
//DirectX::XMFLOAT3 BoxColliderBuffer::GetCenter() const
//{
//	physx::PxTransform pxTransform;
//	if (pxShape) pxTransform = pxShape->getLocalPose();
//	return DirectX::XMFLOAT3(pxTransform.p.x, pxTransform.p.y, pxTransform.p.z);
//}
void BoxColliderBuffer::SetSize(DirectX::XMFLOAT3 value)
{
	//physx::PxBoxGeometry boxGeometry;
	//const physx::PxGeometry* geometry;
	//if (pxShape)
	//{
	//	geometry = &pxShape->getGeometry();
	//}
	//boxGeometry = physx::PxBoxGeometry(value.x, value.y, value.z);
	//if (pxShape) pxShape->setGeometry((physx::PxGeometry&)boxGeometry);
}
//DirectX::XMFLOAT3 BoxColliderBuffer::GetSize() const
//{
//	const physx::PxBoxGeometry* boxGeometry;
//	const physx::PxGeometry* geometry;
//	if (pxShape)
//	{
//		geometry = &pxShape->getGeometry();
//		boxGeometry = (physx::PxBoxGeometry*)geometry;
//		return DirectX::XMFLOAT3(boxGeometry->halfExtents.x, boxGeometry->halfExtents.y, boxGeometry->halfExtents.z);
//	}
//	else
//		return DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
//}

void BoxColliderBuffer::CreateShape(ServicesContainer* ComponentServices)
{
	//if (pxShape) pxShape->release();
	//if (ComponentServices->registry.any_of<TransformComponent>(entity))
	{
		//auto& transformComponent = ComponentServices->registry.get<TransformComponent>(entity);
		//pxShape = physicsSystem.GetPhysics()->createShape(physx::PxBoxGeometry(
		//	transformComponent.GetScale().x,
		//	transformComponent.GetScale().y,
		//	transformComponent.GetScale().z), *pxMaterial, true);
		//if (!pxShape) StarHelpers::AddLog(L"[PhysX] -> Failed to create the box shape!");
	}
}
void BoxColliderBuffer::CreateMaterial()
{
	//if (pxMaterial) pxMaterial->release();
	//pxMaterial = physicsSystem.GetPhysics()->createMaterial(
	//	STATIC_FRICTION,
	//	DYNAMIC_FRICTION,
	//	RESTITUTION);
	//if (!pxMaterial) StarHelpers::AddLog(L"[PhysX] -> Failed to create the material!");
}