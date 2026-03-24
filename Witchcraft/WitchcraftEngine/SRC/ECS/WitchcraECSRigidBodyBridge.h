#pragma once

#include "WitchcraECS.h"

class WitchcraECSRigidBodyBridge
{
public:
	static EntityRigidBodyComponentData BuildComponentData(RigidBodyComponent& component);
	static void SyncComponentToFlecs(WitchcraECS& ecs, SceneEntityBase* entity);
	static void RemoveComponentDataFromFlecs(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool GetEntitySnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, EntityRigidBodyComponentData* outSnapshot);
	static bool SetEntitySnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, const EntityRigidBodyComponentData& snapshot);
};
