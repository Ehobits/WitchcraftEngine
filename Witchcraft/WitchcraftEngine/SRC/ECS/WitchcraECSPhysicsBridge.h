#pragma once

#include "WitchcraECS.h"

class WitchcraECSPhysicsBridge
{
public:
	static EntityPhysicsComponentData BuildComponentData(PhysicsComponent& component);
	static void SyncComponentToFlecs(WitchcraECS& ecs, SceneEntityBase* entity);
	static void RemoveComponentDataFromFlecs(WitchcraECS& ecs, SceneEntityBase* entity);
	static PhysicsComponent* AddComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static PhysicsComponent* ReplaceComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RebuildComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool GetEntityColliderSnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot);
	static bool SetEntityColliderSnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot);
	static bool GetSelectedEntityColliderSnapshot(const WitchcraECS& ecs, size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot);
	static bool SetSelectedEntityColliderSnapshot(const WitchcraECS& ecs, size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot);
	static bool AddBoxColliderToEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool AddBoxColliderToSelectedEntity(WitchcraECS& ecs);
};
