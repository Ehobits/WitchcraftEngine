#pragma once

#include "WitchcraECS.h"

class WitchcraECSInspectorBridge
{
public:
	static std::wstring GetEntityTypeLabel(const WitchcraECS& ecs, SceneEntityBase* entity);
	static bool BuildEntityComponentView(const WitchcraECS& ecs, SceneEntityBase* entity, EntityComponentView* outView);
	static bool BuildSelectedEntityComponentView(const WitchcraECS& ecs, EntityComponentView* outView);
	static bool HasInspectableComponents(const WitchcraECS& ecs, SceneEntityBase* entity);
	static bool RenameSelectedEntity(WitchcraECS& ecs, const std::wstring& newName);
	static bool SetSelectedEntityTag(WitchcraECS& ecs, const std::wstring& newTag);
	static bool SetSelectedEntityStatic(WitchcraECS& ecs, bool isStatic);
	static bool SetSelectedEntityVisible(WitchcraECS& ecs, bool visible);
	static bool GetSelectedEntityCameraFov(const WitchcraECS& ecs, float* outFov);
	static bool SetSelectedEntityCameraFov(WitchcraECS& ecs, float fov);
	static bool GetSelectedEntityCameraNear(const WitchcraECS& ecs, float* outNearZ);
	static bool SetSelectedEntityCameraNear(WitchcraECS& ecs, float nearZ);
	static bool GetSelectedEntityCameraFar(const WitchcraECS& ecs, float* outFarZ);
	static bool SetSelectedEntityCameraFar(WitchcraECS& ecs, float farZ);
	static bool GetSelectedEntityCameraScale(const WitchcraECS& ecs, float* outScale);
	static bool SetSelectedEntityCameraScale(WitchcraECS& ecs, float scale);
	static bool RestoreSelectedEntityCameraScale(WitchcraECS& ecs);
	static bool GetSelectedEntityRigidBodySnapshot(const WitchcraECS& ecs, EntityRigidBodyComponentData* outSnapshot);
	static bool SetSelectedEntityRigidBodySnapshot(WitchcraECS& ecs, const EntityRigidBodyComponentData& snapshot);
	static bool GetSelectedEntityScriptingSnapshot(const WitchcraECS& ecs, EntityScriptingComponentData* outSnapshot);
	static bool GetSelectedEntityScriptSnapshot(const WitchcraECS& ecs, size_t index, EntityScriptingComponentData::ScriptSnapshot* outSnapshot);
	static bool SetSelectedEntityScriptActive(WitchcraECS& ecs, size_t index, bool active);
	static bool RemoveSelectedEntityScript(WitchcraECS& ecs, size_t index);
	static bool GetSelectedEntityPhysicsColliderSnapshot(const WitchcraECS& ecs, size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot);
	static bool SetSelectedEntityPhysicsColliderSnapshot(WitchcraECS& ecs, size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot);
	static bool AddRigidbodyToSelectedEntity(WitchcraECS& ecs);
	static bool AddBoxColliderToSelectedEntity(WitchcraECS& ecs);
	static bool AddScriptToSelectedEntity(WitchcraECS& ecs, const std::wstring& scriptPath);

private:
	static std::wstring BuildEntityTypeLabelByTags(const WitchcraECS& ecs, SceneEntityBase* entity);
	static bool IsSkyEntityForInspector(const WitchcraECS& ecs, SceneEntityBase* entity);
	static void FillEntityComponentPointers(const WitchcraECS& ecs, SceneEntityBase* entity, EntityComponentView& view);
	static bool HasInspectableComponentsInView(const EntityComponentView& view);
	static std::wstring BuildInspectorEntityTypeLabel(const WitchcraECS& ecs, SceneEntityBase* entity, bool isSkyEntity);
};