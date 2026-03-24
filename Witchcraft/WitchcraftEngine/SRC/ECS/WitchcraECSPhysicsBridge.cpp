#include "WitchcraECSPhysicsBridge.h"

#include "ECS/COMPONENT/PhysicsComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"

EntityPhysicsComponentData WitchcraECSPhysicsBridge::BuildComponentData(PhysicsComponent& component)
{
	EntityPhysicsComponentData data;
	const std::vector<PhysicsBoxColliderSnapshot>& colliderSnapshots = component.GetColliderSnapshots();
	data.boxColliderCount = static_cast<std::uint32_t>(colliderSnapshots.size());
	data.colliders.reserve(colliderSnapshots.size());
	for (const PhysicsBoxColliderSnapshot& colliderSnapshot : colliderSnapshots)
	{
		EntityPhysicsComponentData::ColliderSnapshot colliderData;
		colliderData.activeComponent = colliderSnapshot.activeComponent;
		colliderData.staticFriction = colliderSnapshot.staticFriction;
		colliderData.dynamicFriction = colliderSnapshot.dynamicFriction;
		colliderData.restitution = colliderSnapshot.restitution;
		colliderData.center = colliderSnapshot.center;
		colliderData.size = colliderSnapshot.size;
		data.colliders.push_back(colliderData);
	}
	return data;
}

void WitchcraECSPhysicsBridge::SyncComponentToFlecs(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mPhysicsComponentDataId == 0)
		return;

	PhysicsComponent* component = ecs.GetComponent<PhysicsComponent>(entity);
	if (component == nullptr)
	{
		RemoveComponentDataFromFlecs(ecs, entity);
		return;
	}

	ecs.entityWorld.entity(entity->entity).set<EntityPhysicsComponentData>(BuildComponentData(*component));
}

void WitchcraECSPhysicsBridge::RemoveComponentDataFromFlecs(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mPhysicsComponentDataId == 0)
		return;

	ecs_remove_id(ecs.entityWorld, entity->entity, ecs.mPhysicsComponentDataId);
}

PhysicsComponent* WitchcraECSPhysicsBridge::AddComponent(WitchcraECS& ecs, SceneEntityBase* entity)
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

PhysicsComponent* WitchcraECSPhysicsBridge::ReplaceComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.ReplaceManagedComponent<PhysicsComponent>(entity);
}

bool WitchcraECSPhysicsBridge::RemoveComponent(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ecs.RemoveManagedComponent<PhysicsComponent>(entity, [&ecs, entity](PhysicsComponent*)
		{
			ecs.RemovePhysicsComponentDataFromFlecs(entity);
		});
}

bool WitchcraECSPhysicsBridge::RebuildComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return ReplaceComponent(ecs, entity) != nullptr;
}

bool WitchcraECSPhysicsBridge::RemoveComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	return RemoveComponent(ecs, entity);
}

bool WitchcraECSPhysicsBridge::GetEntityColliderSnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot)
{
	PhysicsComponent* physicsComponent = ecs.GetComponent<PhysicsComponent>(entity);
	if (physicsComponent == nullptr || outSnapshot == nullptr)
		return false;

	const PhysicsBoxColliderSnapshot* colliderSnapshot = physicsComponent->GetColliderSnapshot(index);
	if (colliderSnapshot == nullptr)
		return false;

	outSnapshot->activeComponent = colliderSnapshot->activeComponent;
	outSnapshot->staticFriction = colliderSnapshot->staticFriction;
	outSnapshot->dynamicFriction = colliderSnapshot->dynamicFriction;
	outSnapshot->restitution = colliderSnapshot->restitution;
	outSnapshot->center = colliderSnapshot->center;
	outSnapshot->size = colliderSnapshot->size;
	return true;
}

bool WitchcraECSPhysicsBridge::SetEntityColliderSnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot)
{
	PhysicsComponent* physicsComponent = ecs.GetComponent<PhysicsComponent>(entity);
	if (physicsComponent == nullptr)
		return false;

	PhysicsBoxColliderSnapshot colliderSnapshot;
	colliderSnapshot.activeComponent = snapshot.activeComponent;
	colliderSnapshot.staticFriction = snapshot.staticFriction;
	colliderSnapshot.dynamicFriction = snapshot.dynamicFriction;
	colliderSnapshot.restitution = snapshot.restitution;
	colliderSnapshot.center = snapshot.center;
	colliderSnapshot.size = snapshot.size;
	return physicsComponent->SetColliderSnapshot(index, colliderSnapshot);
}

bool WitchcraECSPhysicsBridge::GetSelectedEntityColliderSnapshot(const WitchcraECS& ecs, size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot)
{
	return GetEntityColliderSnapshot(ecs, ecs.selectedEntity, index, outSnapshot);
}

bool WitchcraECSPhysicsBridge::SetSelectedEntityColliderSnapshot(const WitchcraECS& ecs, size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot)
{
	return SetEntityColliderSnapshot(ecs, ecs.selectedEntity, index, snapshot);
}

bool WitchcraECSPhysicsBridge::AddBoxColliderToEntity(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return false;

	TransformComponent* transformComponent = ecs.GetComponent<TransformComponent>(entity);
	PhysicsComponent* physicsComponent = ecs.AddComponent<PhysicsComponent>(entity);
	if (transformComponent == nullptr || physicsComponent == nullptr)
		return false;

	physicsComponent->AddBoxCollider(transformComponent);
	ecs.SyncPhysicsComponentToFlecs(entity);
	return true;
}

bool WitchcraECSPhysicsBridge::AddBoxColliderToSelectedEntity(WitchcraECS& ecs)
{
	return AddBoxColliderToEntity(ecs, ecs.selectedEntity);
}
