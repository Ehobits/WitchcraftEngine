#include "WitchcraECSComponentLifecycleBridge.h"

#include "D3DWindow/D3DWindow.h"
#include "Engine/Engine.h"
#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/SkeletonComponent.h"
#include "ECS/COMPONENT/AnimatorComponent.h"
#include "ECS/COMPONENT/SkinnedMeshComponent.h"
#include "ECS/COMPONENT/SkinningRuntimeComponent.h"
#include "ECS/COMPONENT/RenderDrawSetComponent.h"
#include "ECS/COMPONENT/BillboardComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
#include "ECS/COMPONENT/ScriptingComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"

GeneralComponent* WitchcraECSComponentLifecycleBridge::AddGeneralComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	GeneralComponent* component = ecs.AddManagedComponent<GeneralComponent>(entity,
		[](GeneralComponent* newComponent)
		{
			newComponent->SetName(L"GeneralComponent");
		});

	if (component != nullptr)
		ecs.SyncGeneralComponentToFlecs(entity);

	return component;
}

TransformComponent* WitchcraECSComponentLifecycleBridge::AddTransformComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	TransformComponent* component = ecs.AddManagedComponent<TransformComponent>(entity,
		[](TransformComponent* newComponent)
		{
			newComponent->SetName(L"TransformComponent");
		});
	if (component != nullptr && entity != nullptr && entity->entity != 0)
	{
		Transform localTransform{};
		Transform worldTransform{};
		if (ecs.GetEntityLocalTransform(entity, &localTransform))
			component->SyncLocalTransformCache(localTransform);
		if (ecs.GetEntityWorldTransform(entity, &worldTransform))
			component->SyncGlobalTransformCache(worldTransform);
	}
	return component;
}

MeshComponent* WitchcraECSComponentLifecycleBridge::AddMeshComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.AddManagedComponent<MeshComponent>(entity,
		[](MeshComponent* newComponent)
		{
			newComponent->SetName(L"MeshComponent");
		});
}

SkeletonComponent* WitchcraECSComponentLifecycleBridge::AddSkeletonComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.AddManagedComponent<SkeletonComponent>(entity,
		[](SkeletonComponent* newComponent)
		{
			newComponent->SetName(L"SkeletonComponent");
		});
}

AnimatorComponent* WitchcraECSComponentLifecycleBridge::AddAnimatorComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.AddManagedComponent<AnimatorComponent>(entity,
		[](AnimatorComponent* newComponent)
		{
			newComponent->SetName(L"AnimatorComponent");
		});
}

SkinnedMeshComponent* WitchcraECSComponentLifecycleBridge::AddSkinnedMeshComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.AddManagedComponent<SkinnedMeshComponent>(entity,
		[](SkinnedMeshComponent* newComponent)
		{
			newComponent->SetName(L"SkinnedMeshComponent");
		});
}

SkinningRuntimeComponent* WitchcraECSComponentLifecycleBridge::AddSkinningRuntimeComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.AddManagedComponent<SkinningRuntimeComponent>(entity,
		[](SkinningRuntimeComponent* newComponent)
		{
			newComponent->SetName(L"SkinningRuntimeComponent");
		});
}

RenderDrawSetComponent* WitchcraECSComponentLifecycleBridge::AddRenderDrawSetComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.AddManagedComponent<RenderDrawSetComponent>(entity,
		[](RenderDrawSetComponent* newComponent)
		{
			newComponent->SetName(L"RenderDrawSetComponent");
		});
}

BillboardComponent* WitchcraECSComponentLifecycleBridge::AddBillboardComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.AddManagedComponent<BillboardComponent>(entity,
		[](BillboardComponent* newComponent)
		{
			newComponent->SetName(L"BillboardComponent");
		});
}

CameraComponent* WitchcraECSComponentLifecycleBridge::AddCameraComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	CameraComponent* component = ecs.AddManagedComponent<CameraComponent>(entity,
		[](CameraComponent* newComponent)
		{
			newComponent->SetName(L"CameraComponent");
		});

	if (component != nullptr)
		ecs.SyncCameraComponentToFlecs(entity);

	return component;
}

PhysicsComponent* WitchcraECSComponentLifecycleBridge::AddPhysicsComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	PhysicsComponent* component = ecs.AddManagedComponent<PhysicsComponent>(entity,
		[&ecs, entity](PhysicsComponent* newComponent)
		{
			newComponent->SetName(L"PhysicsComponent");
			newComponent->SetSyncCallback([&ecs, entity]()
				{
					ecs.SyncPhysicsComponentToFlecs(entity);
				});
		});

	if (component != nullptr)
		ecs.SyncPhysicsComponentToFlecs(entity);

	return component;
}

ScriptingComponent* WitchcraECSComponentLifecycleBridge::AddScriptingComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	ScriptingComponent* component = ecs.AddManagedComponent<ScriptingComponent>(entity,
		[](ScriptingComponent* newComponent)
		{
			newComponent->SetName(L"ScriptingComponent");
		});

	if (component != nullptr)
		ecs.SyncScriptingComponentToFlecs(entity);

	return component;
}

RigidBodyComponent* WitchcraECSComponentLifecycleBridge::AddRigidbodyComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	RigidBodyComponent* component = ecs.AddManagedComponent<RigidBodyComponent>(entity,
		[&ecs, entity](RigidBodyComponent* newComponent)
		{
			newComponent->SetName(L"RigidbodyComponent");
			newComponent->SetSyncCallback([&ecs, entity]()
				{
					ecs.SyncRigidBodyComponentToFlecs(entity);
				});
		});

	if (component != nullptr)
		ecs.SyncRigidBodyComponentToFlecs(entity);

	return component;
}

GeneralComponent* WitchcraECSComponentLifecycleBridge::ReplaceGeneralComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return nullptr;

	GeneralComponent* oldComponent = ecs.GetComponent<GeneralComponent>(entity);
	if (oldComponent == nullptr)
		return AddGeneralComponent(ecs, entity);

	const bool wasVisible = ecs.IsEntitySelfVisible(entity);
	const ComponentType componentType = ecs.GetEntityGeneralComponentType(entity);

	RemoveGeneralComponent(ecs, entity);
	GeneralComponent* newComponent = AddGeneralComponent(ecs, entity);
	if (newComponent != nullptr)
	{
		ecs.SetEntityVisible(entity, wasVisible);
		ecs.SetEntityGeneralComponentType(entity, componentType);
	}

	return newComponent;
}

TransformComponent* WitchcraECSComponentLifecycleBridge::ReplaceTransformComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<TransformComponent>(entity);
}

MeshComponent* WitchcraECSComponentLifecycleBridge::ReplaceMeshComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return nullptr;

	MeshComponent* oldComponent = ecs.GetComponent<MeshComponent>(entity);
	if (oldComponent == nullptr)
		return AddMeshComponent(ecs, entity);

	ServicesContainer& services = WitchcraECS::GetEntityServices(entity);

	MeshComponent snapshot;
	Engine* mEngine = nullptr;
	snapshot.CopySettingsFrom(*oldComponent);
	snapshot.CopyCpuGeometryFrom(*oldComponent);
	mEngine = oldComponent->GetEngine();

	D3DWindow* dx = mEngine != nullptr ? mEngine->GetD3DWindow() : nullptr;
	const bool hasGeometry = dx != nullptr &&
		!snapshot.GetGeometryName().empty() &&
		dx->HasShapeGeometry(snapshot.GetGeometryName());
	const bool hasRenderItem = dx != nullptr &&
		!snapshot.GetMeshName().empty() &&
		dx->GetRenderItem(snapshot.GetMeshName()) != nullptr;

	services.RemoveService(ECSComponentServiceTraits<MeshComponent>::Name);

	MeshComponent* newComponent = AddMeshComponent(ecs, entity);
	if (newComponent == nullptr)
	{
		services.AddService(ECSComponentServiceTraits<MeshComponent>::Name, oldComponent);
		ecs.RefreshEntityTypeTags(entity);
		return oldComponent;
	}

	newComponent->CopySettingsFrom(snapshot);
	newComponent->CopyCpuGeometryFrom(snapshot);
	newComponent->SetEngine(mEngine);

	const bool needsGeometryUpload = dx != nullptr && newComponent->OwnsGeometry() && !hasGeometry;
	if (needsGeometryUpload)
	{
		const UINT indexCount = newComponent->GetIndexCount();
		const UINT vertexCount = newComponent->GetVertexCount();
		if (indexCount > 0 && vertexCount > 0)
			newComponent->SetupMesh(ecs.GetComponent<TransformComponent>(entity), dx, indexCount, vertexCount);
	}

	const bool canRestoreRenderItem =
		hasRenderItem ||
		hasGeometry ||
		needsGeometryUpload ||
		!newComponent->OwnsGeometry();
	if (dx != nullptr && canRestoreRenderItem)
		dx->AddRenderItemsFromEntity(entity, &ecs);

	delete oldComponent;
	ecs.RefreshEntityTypeTags(entity);
	ecs.MarkSceneDirty();
	return newComponent;
}

SkeletonComponent* WitchcraECSComponentLifecycleBridge::ReplaceSkeletonComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<SkeletonComponent>(entity);
}

AnimatorComponent* WitchcraECSComponentLifecycleBridge::ReplaceAnimatorComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<AnimatorComponent>(entity);
}

SkinnedMeshComponent* WitchcraECSComponentLifecycleBridge::ReplaceSkinnedMeshComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<SkinnedMeshComponent>(entity);
}

SkinningRuntimeComponent* WitchcraECSComponentLifecycleBridge::ReplaceSkinningRuntimeComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<SkinningRuntimeComponent>(entity);
}

RenderDrawSetComponent* WitchcraECSComponentLifecycleBridge::ReplaceRenderDrawSetComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<RenderDrawSetComponent>(entity);
}

BillboardComponent* WitchcraECSComponentLifecycleBridge::ReplaceBillboardComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<BillboardComponent>(entity);
}

CameraComponent* WitchcraECSComponentLifecycleBridge::ReplaceCameraComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return nullptr;

	CameraComponent* oldComponent = ecs.GetComponent<CameraComponent>(entity);
	if (oldComponent == nullptr)
		return AddCameraComponent(ecs, entity);

	float snapshotFov = 0.25f;
	float snapshotNearZ = 1.0f;
	float snapshotFarZ = 1000.0f;
	float snapshotScale = 1.0f;
	ecs.GetEntityCameraFov(entity, &snapshotFov);
	ecs.GetEntityCameraNear(entity, &snapshotNearZ);
	ecs.GetEntityCameraFar(entity, &snapshotFarZ);
	ecs.GetEntityCameraScale(entity, &snapshotScale);
	Engine* Engine = oldComponent->GetEngine();

	RemoveCameraComponent(ecs, entity);
	CameraComponent* newComponent = AddCameraComponent(ecs, entity);
	if (newComponent != nullptr)
	{
		newComponent->SetEngine(Engine);
		ecs.SetEntityCameraFov(entity, snapshotFov);
		ecs.SetEntityCameraNear(entity, snapshotNearZ);
		ecs.SetEntityCameraFar(entity, snapshotFarZ);
		ecs.SetEntityCameraScale(entity, snapshotScale);
	}

	return newComponent;
}

PhysicsComponent* WitchcraECSComponentLifecycleBridge::ReplacePhysicsComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<PhysicsComponent>(entity);
}

ScriptingComponent* WitchcraECSComponentLifecycleBridge::ReplaceScriptingComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return nullptr;

	ScriptingComponent* oldComponent = ecs.GetComponent<ScriptingComponent>(entity);
	if (oldComponent == nullptr)
		return AddScriptingComponent(ecs, entity);

	EntityScriptingComponentData snapshot{};
	const bool hasSnapshot = ecs.GetEntityScriptingSnapshot(entity, &snapshot);

	RemoveScriptingComponent(ecs, entity);
	ScriptingComponent* newComponent = AddScriptingComponent(ecs, entity);
	if (newComponent != nullptr && hasSnapshot && entity->entity != 0 && ecs.mScriptingComponentDataId != 0)
		ecs.entityWorld.entity(entity->entity).set<EntityScriptingComponentData>(snapshot);

	return newComponent;
}

RigidBodyComponent* WitchcraECSComponentLifecycleBridge::ReplaceRigidbodyComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<RigidBodyComponent>(entity);
}

bool WitchcraECSComponentLifecycleBridge::RemoveGeneralComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<GeneralComponent>(entity, [&ecs, entity](GeneralComponent*)
		{
			ecs.RemoveGeneralComponentDataFromFlecs(entity);
		});
}

bool WitchcraECSComponentLifecycleBridge::RemoveTransformComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<TransformComponent>(entity, [](TransformComponent*) {});
}

bool WitchcraECSComponentLifecycleBridge::RemoveMeshComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return false;

	ServicesContainer& services = WitchcraECS::GetEntityServices(entity);
	MeshComponent* component = services.FindServiceAs<MeshComponent>(ECSComponentServiceTraits<MeshComponent>::Name);
	if (component == nullptr)
		return false;

	DestroyMeshComponentInstance(component);
	delete component;
	services.RemoveService(ECSComponentServiceTraits<MeshComponent>::Name);

	ecs.RefreshEntityTypeTags(entity);
	ecs.MarkSceneDirty();
	return true;
}

bool WitchcraECSComponentLifecycleBridge::RemoveSkeletonComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<SkeletonComponent>(entity, [](SkeletonComponent*) {});
}

bool WitchcraECSComponentLifecycleBridge::RemoveAnimatorComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<AnimatorComponent>(entity, [](AnimatorComponent*) {});
}

bool WitchcraECSComponentLifecycleBridge::RemoveSkinnedMeshComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<SkinnedMeshComponent>(entity, [](SkinnedMeshComponent*) {});
}

bool WitchcraECSComponentLifecycleBridge::RemoveSkinningRuntimeComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<SkinningRuntimeComponent>(entity, [](SkinningRuntimeComponent*) {});
}

bool WitchcraECSComponentLifecycleBridge::RemoveRenderDrawSetComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<RenderDrawSetComponent>(entity, [](RenderDrawSetComponent*) {});
}

bool WitchcraECSComponentLifecycleBridge::RemoveBillboardComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<BillboardComponent>(entity, [](BillboardComponent*) {});
}

void WitchcraECSComponentLifecycleBridge::DestroyMeshComponentInstance(MeshComponent* component)
{
	if (component == nullptr)
		return;

	component->ReleaseResources();
	component->ClearCache();
	static_cast<BaseComponent*>(component)->Destroy();
}

bool WitchcraECSComponentLifecycleBridge::RemoveCameraComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<CameraComponent>(entity, [&ecs, entity](CameraComponent*)
		{
			ecs.RemoveCameraComponentDataFromFlecs(entity);
		});
}

bool WitchcraECSComponentLifecycleBridge::RemovePhysicsComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<PhysicsComponent>(entity, [&ecs, entity](PhysicsComponent*)
		{
			ecs.RemovePhysicsComponentDataFromFlecs(entity);
		});
}

bool WitchcraECSComponentLifecycleBridge::RemoveScriptingComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<ScriptingComponent>(entity, [&ecs, entity](ScriptingComponent*)
		{
			ecs.RemoveScriptingComponentDataFromFlecs(entity);
		});
}

bool WitchcraECSComponentLifecycleBridge::RemoveRigidbodyComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<RigidBodyComponent>(entity,
		[&ecs, entity](RigidBodyComponent* component)
		{
			ecs.RemoveRigidBodyComponentDataFromFlecs(entity);
			component->ReleaseActor();
		});
}

bool WitchcraECSComponentLifecycleBridge::RebuildMeshComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ReplaceMeshComponent(ecs, entity) != nullptr;
}

bool WitchcraECSComponentLifecycleBridge::RebuildCameraComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return false;

	CameraComponent* oldComponent = ecs.GetComponent<CameraComponent>(entity);
	if (oldComponent == nullptr)
		return AddCameraComponent(ecs, entity) != nullptr;

	float snapshotFov = 0.25f;
	float snapshotNearZ = 1.0f;
	float snapshotFarZ = 1000.0f;
	float snapshotScale = 1.0f;
	ecs.GetEntityCameraFov(entity, &snapshotFov);
	ecs.GetEntityCameraNear(entity, &snapshotNearZ);
	ecs.GetEntityCameraFar(entity, &snapshotFarZ);
	ecs.GetEntityCameraScale(entity, &snapshotScale);
	Engine* Engine = oldComponent->GetEngine();

	CameraComponent* newComponent = ReplaceCameraComponent(ecs, entity);
	if (newComponent == nullptr)
		return false;

	newComponent->SetEngine(Engine);
	ecs.SetEntityCameraFov(entity, snapshotFov);
	ecs.SetEntityCameraNear(entity, snapshotNearZ);
	ecs.SetEntityCameraFar(entity, snapshotFarZ);
	ecs.SetEntityCameraScale(entity, snapshotScale);
	return true;
}

bool WitchcraECSComponentLifecycleBridge::RebuildPhysicsComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ReplacePhysicsComponent(ecs, entity) != nullptr;
}

bool WitchcraECSComponentLifecycleBridge::RebuildScriptingComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ReplaceScriptingComponent(ecs, entity) != nullptr;
}

bool WitchcraECSComponentLifecycleBridge::RebuildRigidbodyComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ReplaceRigidbodyComponent(ecs, entity) != nullptr;
}

bool WitchcraECSComponentLifecycleBridge::RemoveMeshComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return RemoveMeshComponent(ecs, entity);
}

bool WitchcraECSComponentLifecycleBridge::RemoveCameraComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return RemoveCameraComponent(ecs, entity);
}

bool WitchcraECSComponentLifecycleBridge::RemovePhysicsComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return RemovePhysicsComponent(ecs, entity);
}

bool WitchcraECSComponentLifecycleBridge::RemoveScriptingComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return RemoveScriptingComponent(ecs, entity);
}

bool WitchcraECSComponentLifecycleBridge::RemoveRigidbodyComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return RemoveRigidbodyComponent(ecs, entity);
}
