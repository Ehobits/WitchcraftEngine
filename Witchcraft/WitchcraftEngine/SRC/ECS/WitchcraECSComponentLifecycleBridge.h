#pragma once

#include "WitchcraECS.h"

class WitchcraECSComponentLifecycleBridge
{
public:
	static GeneralComponent* AddGeneralComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static TransformComponent* AddTransformComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static MeshComponent* AddMeshComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static CameraComponent* AddCameraComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static PhysicsComponent* AddPhysicsComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static ScriptingComponent* AddScriptingComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static RigidBodyComponent* AddRigidbodyComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static GeneralComponent* ReplaceGeneralComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static TransformComponent* ReplaceTransformComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static MeshComponent* ReplaceMeshComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static CameraComponent* ReplaceCameraComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static PhysicsComponent* ReplacePhysicsComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static ScriptingComponent* ReplaceScriptingComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static RigidBodyComponent* ReplaceRigidbodyComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveGeneralComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveTransformComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveMeshComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static void DestroyMeshComponentInstance(MeshComponent* component);
	static bool RemoveCameraComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemovePhysicsComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveScriptingComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveRigidbodyComponent(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RebuildMeshComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RebuildCameraComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RebuildPhysicsComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RebuildScriptingComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RebuildRigidbodyComponentOnEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveMeshComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveCameraComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemovePhysicsComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveScriptingComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RemoveRigidbodyComponentFromEntity(WitchcraECS& ecs, SceneEntityBase* entity);
};
