#include "WitchcraECSInspectorBridge.h"

#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/LightComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
#include "ECS/COMPONENT/ScriptingComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"

namespace
{
	constexpr const wchar_t* kUnknownTypeLabel = L"未知";
	constexpr const wchar_t* kMeshTypeLabel = L"网格";
	constexpr const wchar_t* kCameraTypeLabel = L"相机";
	constexpr const wchar_t* kLightTypeLabel = L"灯光";
	constexpr const wchar_t* kEntityTypeLabel = L"实体";
	constexpr const wchar_t* kSkyTypeLabel = L"天空";
}

std::wstring WitchcraECSInspectorBridge::BuildEntityTypeLabelByTags(const WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0)
		return kUnknownTypeLabel;

	if (ecs.mMeshEntityTypeTagId != 0 && ecs_has_id(ecs.entityWorld, entity->entity, ecs.mMeshEntityTypeTagId))
		return kMeshTypeLabel;
	if (ecs.mCameraEntityTypeTagId != 0 && ecs_has_id(ecs.entityWorld, entity->entity, ecs.mCameraEntityTypeTagId))
		return kCameraTypeLabel;
	if (ecs.mLightEntityTypeTagId != 0 && ecs_has_id(ecs.entityWorld, entity->entity, ecs.mLightEntityTypeTagId))
		return kLightTypeLabel;
	if (ecs.mUnknownEntityTypeTagId != 0 && ecs_has_id(ecs.entityWorld, entity->entity, ecs.mUnknownEntityTypeTagId))
		return kEntityTypeLabel;

	return kUnknownTypeLabel;
}

bool WitchcraECSInspectorBridge::IsSkyEntityForInspector(const WitchcraECS& ecs, SceneEntityBase* entity)
{
	const MeshComponent* meshComponent = ecs.GetComponent<MeshComponent>(entity);
	return meshComponent != nullptr && meshComponent->GetRenderLayerIndex() == 天空渲染项目;
}

void WitchcraECSInspectorBridge::FillEntityComponentPointers(const WitchcraECS& ecs, SceneEntityBase* entity, EntityComponentView& view)
{
	view.entity = entity;
	view.generalComponent = ecs.GetComponent<GeneralComponent>(entity);
	view.cameraComponent = ecs.GetComponent<CameraComponent>(entity);
	view.transformComponent = ecs.GetComponent<TransformComponent>(entity);
	view.meshComponent = ecs.GetComponent<MeshComponent>(entity);
	view.lightComponent = ecs.GetComponent<LightComponent>(entity);
	view.physicsComponent = ecs.GetComponent<PhysicsComponent>(entity);
	view.scriptingComponent = ecs.GetComponent<ScriptingComponent>(entity);
	view.rigidbodyComponent = ecs.GetComponent<RigidBodyComponent>(entity);
	ecs.GetEntityEditableLocalTransform(entity, &view.editableLocalTransform);
}

bool WitchcraECSInspectorBridge::HasInspectableComponentsInView(const EntityComponentView& view)
{
	return view.generalComponent != nullptr ||
		view.cameraComponent != nullptr ||
		view.transformComponent != nullptr ||
		view.meshComponent != nullptr ||
		view.lightComponent != nullptr ||
		view.physicsComponent != nullptr ||
		view.scriptingComponent != nullptr ||
		view.rigidbodyComponent != nullptr;
}

std::wstring WitchcraECSInspectorBridge::BuildInspectorEntityTypeLabel(const WitchcraECS& ecs, SceneEntityBase* entity, bool isSkyEntity)
{
	if (isSkyEntity)
		return kSkyTypeLabel;

	std::wstring entityTypeLabel = BuildEntityTypeLabelByTags(ecs, entity);
	return entityTypeLabel.empty() ? std::wstring(kUnknownTypeLabel) : entityTypeLabel;
}

std::wstring WitchcraECSInspectorBridge::GetEntityTypeLabel(const WitchcraECS& ecs, SceneEntityBase* entity)
{
	return BuildEntityTypeLabelByTags(ecs, entity);
}

bool WitchcraECSInspectorBridge::BuildEntityComponentView(const WitchcraECS& ecs, SceneEntityBase* entity, EntityComponentView* outView)
{
	if (outView == nullptr)
		return false;

	*outView = EntityComponentView{};
	if (entity == nullptr || !ecs.HasEntity(entity))
		return false;

	FillEntityComponentPointers(ecs, entity, *outView);
	outView->isSkyEntity = IsSkyEntityForInspector(ecs, entity);
	outView->entityTypeLabel = BuildInspectorEntityTypeLabel(ecs, entity, outView->isSkyEntity);
	outView->hasInspectableComponents = HasInspectableComponentsInView(*outView);
	return true;
}

bool WitchcraECSInspectorBridge::BuildSelectedEntityComponentView(const WitchcraECS& ecs, EntityComponentView* outView)
{
	return BuildEntityComponentView(ecs, ecs.selectedEntity, outView);
}

bool WitchcraECSInspectorBridge::HasInspectableComponents(const WitchcraECS& ecs, SceneEntityBase* entity)
{
	EntityComponentView view;
	return BuildEntityComponentView(ecs, entity, &view) && view.hasInspectableComponents;
}

bool WitchcraECSInspectorBridge::RenameSelectedEntity(WitchcraECS& ecs, const std::wstring& newName)
{
	return ecs.RenameEntity(ecs.selectedEntity, newName);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityTag(WitchcraECS& ecs, const std::wstring& newTag)
{
	return ecs.SetEntityTag(ecs.selectedEntity, newTag);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityStatic(WitchcraECS& ecs, bool isStatic)
{
	return ecs.SetEntityStatic(ecs.selectedEntity, isStatic);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityVisible(WitchcraECS& ecs, bool visible)
{
	return ecs.SetEntityVisible(ecs.selectedEntity, visible);
}

bool WitchcraECSInspectorBridge::GetSelectedEntityCameraFov(const WitchcraECS& ecs, float* outFov)
{
	return ecs.GetEntityCameraFov(ecs.selectedEntity, outFov);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityCameraFov(WitchcraECS& ecs, float fov)
{
	return ecs.SetEntityCameraFov(ecs.selectedEntity, fov);
}

bool WitchcraECSInspectorBridge::GetSelectedEntityCameraNear(const WitchcraECS& ecs, float* outNearZ)
{
	return ecs.GetEntityCameraNear(ecs.selectedEntity, outNearZ);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityCameraNear(WitchcraECS& ecs, float nearZ)
{
	return ecs.SetEntityCameraNear(ecs.selectedEntity, nearZ);
}

bool WitchcraECSInspectorBridge::GetSelectedEntityCameraFar(const WitchcraECS& ecs, float* outFarZ)
{
	return ecs.GetEntityCameraFar(ecs.selectedEntity, outFarZ);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityCameraFar(WitchcraECS& ecs, float farZ)
{
	return ecs.SetEntityCameraFar(ecs.selectedEntity, farZ);
}

bool WitchcraECSInspectorBridge::GetSelectedEntityCameraScale(const WitchcraECS& ecs, float* outScale)
{
	return ecs.GetEntityCameraScale(ecs.selectedEntity, outScale);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityCameraScale(WitchcraECS& ecs, float scale)
{
	return ecs.SetEntityCameraScale(ecs.selectedEntity, scale);
}

bool WitchcraECSInspectorBridge::RestoreSelectedEntityCameraScale(WitchcraECS& ecs)
{
	return ecs.RestoreEntityCameraScale(ecs.selectedEntity);
}

bool WitchcraECSInspectorBridge::GetSelectedEntityRigidBodySnapshot(const WitchcraECS& ecs, EntityRigidBodyComponentData* outSnapshot)
{
	return ecs.GetEntityRigidBodySnapshot(ecs.selectedEntity, outSnapshot);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityRigidBodySnapshot(WitchcraECS& ecs, const EntityRigidBodyComponentData& snapshot)
{
	return ecs.SetEntityRigidBodySnapshot(ecs.selectedEntity, snapshot);
}

bool WitchcraECSInspectorBridge::GetSelectedEntityScriptingSnapshot(const WitchcraECS& ecs, EntityScriptingComponentData* outSnapshot)
{
	return ecs.GetEntityScriptingSnapshot(ecs.selectedEntity, outSnapshot);
}

bool WitchcraECSInspectorBridge::GetSelectedEntityScriptSnapshot(const WitchcraECS& ecs, size_t index, EntityScriptingComponentData::ScriptSnapshot* outSnapshot)
{
	return ecs.GetEntityScriptSnapshot(ecs.selectedEntity, index, outSnapshot);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityScriptActive(WitchcraECS& ecs, size_t index, bool active)
{
	return ecs.SetEntityScriptActive(ecs.selectedEntity, index, active);
}

bool WitchcraECSInspectorBridge::RemoveSelectedEntityScript(WitchcraECS& ecs, size_t index)
{
	return ecs.RemoveEntityScript(ecs.selectedEntity, index);
}

bool WitchcraECSInspectorBridge::GetSelectedEntityPhysicsColliderSnapshot(const WitchcraECS& ecs, size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot)
{
	return ecs.GetEntityPhysicsColliderSnapshot(ecs.selectedEntity, index, outSnapshot);
}

bool WitchcraECSInspectorBridge::SetSelectedEntityPhysicsColliderSnapshot(WitchcraECS& ecs, size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot)
{
	return ecs.SetEntityPhysicsColliderSnapshot(ecs.selectedEntity, index, snapshot);
}

bool WitchcraECSInspectorBridge::AddRigidbodyToSelectedEntity(WitchcraECS& ecs)
{
	return ecs.AddRigidbodyToEntity(ecs.selectedEntity);
}

bool WitchcraECSInspectorBridge::AddBoxColliderToSelectedEntity(WitchcraECS& ecs)
{
	return ecs.AddBoxColliderToEntity(ecs.selectedEntity);
}

bool WitchcraECSInspectorBridge::AddScriptToSelectedEntity(WitchcraECS& ecs, const std::wstring& scriptPath)
{
	return ecs.AddScriptToEntity(ecs.selectedEntity, scriptPath);
}
