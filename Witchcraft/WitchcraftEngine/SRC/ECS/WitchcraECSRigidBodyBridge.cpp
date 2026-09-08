#include "WitchcraECSRigidBodyBridge.h"

#include "ECS/COMPONENT/RigidbodyComponent.h"

EntityRigidBodyComponentData WitchcraECSRigidBodyBridge::BuildComponentData(RigidBodyComponent& component)
{
	EntityRigidBodyComponentData data;
	data.mass = component.GetMass();
	data.linearDamping = component.GetLinearDamping();
	data.angularDamping = component.GetAngularDamping();
	data.useGravity = component.HasUseGravity();
	data.kinematic = component.IsKinematic();
	data.linearLockX = component.GetLinearLockX();
	data.linearLockY = component.GetLinearLockY();
	data.linearLockZ = component.GetLinearLockZ();
	data.angularLockX = component.GetAngularLockX();
	data.angularLockY = component.GetAngularLockY();
	data.angularLockZ = component.GetAngularLockZ();
	return data;
}

void WitchcraECSRigidBodyBridge::SyncComponentToFlecs(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mRigidBodyComponentDataId == 0)
		return;

	RigidBodyComponent* component = ecs.GetComponent<RigidBodyComponent>(entity);
	if (component == nullptr)
	{
		RemoveComponentDataFromFlecs(ecs, entity);
		return;
	}

	ecs.entityWorld.entity(entity->entity).set<EntityRigidBodyComponentData>(BuildComponentData(*component));
}

void WitchcraECSRigidBodyBridge::RemoveComponentDataFromFlecs(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mRigidBodyComponentDataId == 0)
		return;

	ecs_remove_id(ecs.entityWorld, entity->entity, ecs.mRigidBodyComponentDataId);
}

bool WitchcraECSRigidBodyBridge::GetEntitySnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, EntityRigidBodyComponentData* outSnapshot)
{
	RigidBodyComponent* rigidBodyComponent = ecs.GetComponent<RigidBodyComponent>(entity);
	if (rigidBodyComponent == nullptr || outSnapshot == nullptr)
		return false;

	*outSnapshot = BuildComponentData(*rigidBodyComponent);
	return true;
}

bool WitchcraECSRigidBodyBridge::SetEntitySnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, const EntityRigidBodyComponentData& snapshot)
{
	RigidBodyComponent* rigidBodyComponent = ecs.GetComponent<RigidBodyComponent>(entity);
	if (rigidBodyComponent == nullptr)
		return false;

	rigidBodyComponent->SetMass(snapshot.mass);
	rigidBodyComponent->SetLinearDamping(snapshot.linearDamping);
	rigidBodyComponent->SetAngularDamping(snapshot.angularDamping);
	rigidBodyComponent->UseGravity(snapshot.useGravity);
	rigidBodyComponent->SetKinematic(snapshot.kinematic);
	rigidBodyComponent->SetLinearLockX(snapshot.linearLockX);
	rigidBodyComponent->SetLinearLockY(snapshot.linearLockY);
	rigidBodyComponent->SetLinearLockZ(snapshot.linearLockZ);
	rigidBodyComponent->SetAngularLockX(snapshot.angularLockX);
	rigidBodyComponent->SetAngularLockY(snapshot.angularLockY);
	rigidBodyComponent->SetAngularLockZ(snapshot.angularLockZ);
	const_cast<WitchcraECS&>(ecs).MarkSceneDirty();
	return true;
}
