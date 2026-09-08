#include "WitchcraECSEntityHierarchyBridge.h"

#include <vector>

void WitchcraECSEntityHierarchyBridge::ApplyEntityNameChange(WitchcraECS& ecs, SceneEntityBase* entity, const std::wstring& newName)
{
	if (entity == nullptr || newName.empty())
		return;

	const std::wstring oldName = ecs.GetEntityName(entity);
	if (oldName == newName)
		return;

	WitchcraECSEntityHierarchyBridge::RemoveEntityNameIndexEntry(ecs, oldName, entity);
	entity->SetName(newName);
	WitchcraECSEntityHierarchyBridge::AddEntityNameIndexEntry(ecs, entity);
}

void WitchcraECSEntityHierarchyBridge::EnsureEntityNameMatchesScope(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* parent)
{
	if (entity == nullptr)
		return;

	const std::wstring oldName = ecs.GetEntityName(entity);
	const std::wstring uniqueName = ecs.GetUniqueEntityName(oldName, parent);
	if (oldName == uniqueName)
		return;

	ApplyEntityNameChange(ecs, entity, uniqueName);
}

bool WitchcraECSEntityHierarchyBridge::IsSkeletonHierarchyDropTarget(const WitchcraECS& ecs, SceneEntityBase* entity)
{
	for (SceneEntityBase* current = entity; current != nullptr; current = ecs.GetParentEntity(current))
	{
		if (ecs.IsSkeletonHierarchyEntity(current))
			return true;
		if (current != entity && ecs.HasSkeletonData(current))
			return true;
	}

	return false;
}

void WitchcraECSEntityHierarchyBridge::RegisterEntitySubtreeIndices(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* parent)
{
	if (entity == nullptr)
		return;

	UpdateEntityParentIndexEntry(ecs, entity, parent);
	AddEntityNameIndexEntry(ecs, entity);

	for (SceneEntityBase* child : ecs.GetEntityChildren(entity))
		RegisterEntitySubtreeIndices(ecs, child, entity);
}

void WitchcraECSEntityHierarchyBridge::UnregisterEntitySubtreeIndices(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	for (SceneEntityBase* child : ecs.GetEntityChildren(entity))
		UnregisterEntitySubtreeIndices(ecs, child);

	RemoveEntityIndexEntry(ecs, entity);
}

void WitchcraECSEntityHierarchyBridge::AddEntityNameIndexEntry(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	ecs.mEntityNameIndex[ecs.GetEntityName(entity)].push_back(entity);
}

void WitchcraECSEntityHierarchyBridge::RemoveEntityNameIndexEntry(WitchcraECS& ecs, const std::wstring& name, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	auto it = ecs.mEntityNameIndex.find(name);
	if (it == ecs.mEntityNameIndex.end())
		return;

	auto& entitiesWithSameName = it->second;
	entitiesWithSameName.erase(
		std::remove(entitiesWithSameName.begin(), entitiesWithSameName.end(), entity),
		entitiesWithSameName.end());

	if (entitiesWithSameName.empty())
		ecs.mEntityNameIndex.erase(it);
}

void WitchcraECSEntityHierarchyBridge::UpdateEntityParentIndexEntry(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* parent)
{
	if (entity == nullptr)
		return;

	ecs.mParentEntityIndex[entity] = parent;
}

void WitchcraECSEntityHierarchyBridge::RemoveEntityParentIndexEntry(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	ecs.mParentEntityIndex.erase(entity);
}

void WitchcraECSEntityHierarchyBridge::RemoveEntityIndexEntry(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	RemoveEntityNameIndexEntry(ecs, ecs.GetEntityName(entity), entity);
	RemoveEntityParentIndexEntry(ecs, entity);
}

bool WitchcraECSEntityHierarchyBridge::ContainsEntity(const WitchcraECS& ecs, SceneEntityBase* root, SceneEntityBase* target)
{
	if (root == nullptr || target == nullptr)
		return false;
	if (root == target)
		return true;

	for (SceneEntityBase* child : ecs.GetEntityChildren(root))
	{
		if (ContainsEntity(ecs, child, target))
			return true;
	}

	return false;
}

void WitchcraECSEntityHierarchyBridge::DeleteEntityTree(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	for (SceneEntityBase* child : ecs.GetEntityChildren(entity))
	{
		DeleteEntityTree(ecs, child);
	}

	if (entity->entity != 0)
		ecs_delete(ecs.entityWorld, entity->entity);

	delete entity;
}

void WitchcraECSEntityHierarchyBridge::CreateEntity(WitchcraECS& ecs, std::wstring name, SceneEntityBase* entity)
{
	CreateEntity(ecs, std::move(name), entity, ecs.selectedEntity);
}

void WitchcraECSEntityHierarchyBridge::CreateEntity(WitchcraECS& ecs, std::wstring name, SceneEntityBase* entity, SceneEntityBase* parent)
{
	CreateEntity(ecs, std::move(name), entity, parent, 0);
}

void WitchcraECSEntityHierarchyBridge::CreateEntity(
	WitchcraECS& ecs,
	std::wstring name,
	SceneEntityBase* entity,
	SceneEntityBase* parent,
	flecs::entity_t desiredEntityId)
{
	if (entity == nullptr)
		return;

	entity->SetName(name);
	if (parent != nullptr)
	{
		parent->AddChild(name, entity);
		if (desiredEntityId != 0)
		{
			ecs_entity_desc_t desc = { 0 };
			desc.id = desiredEntityId;
			desc.parent = parent->entity;
			entity->entity = ecs_entity_init(ecs.entityWorld, &desc);
		}
		else
		{
			entity->entity = ecs_new_w_pair(ecs.entityWorld, EcsChildOf, parent->entity);
		}
	}
	else
	{
		if (ecs.GetEntityName(entity) == WitchcraECS::GetEnvironmentEntityName())
			ecs.entities.insert(ecs.entities.begin(), entity);
		else
			ecs.entities.push_back(entity);
		ecs_entity_desc_t desc = { 0 };
		desc.id = desiredEntityId;
		entity->entity = ecs_entity_init(ecs.entityWorld, &desc);
	}

	ecs.RefreshEntityTypeTags(entity);
	ecs.InitializeEntityTransformState(entity);
	ecs.SyncGeneralComponentToFlecs(entity);
	ecs.SyncCameraComponentToFlecs(entity);
	ecs.SyncPhysicsComponentToFlecs(entity);
	ecs.SyncRigidBodyComponentToFlecs(entity);
	ecs.SyncScriptingComponentToFlecs(entity);
	RegisterEntitySubtreeIndices(ecs, entity, parent);
	ecs.MarkSceneDirty();

	if (parent == nullptr && ecs.GetEntityName(entity) == WitchcraECS::GetEnvironmentEntityName())
		ecs.mEnvironmentEntity = entity;
}

void WitchcraECSEntityHierarchyBridge::DestroyEntity(WitchcraECS& ecs, std::wstring name, bool destroyChildren)
{
	SceneEntityBase* target = ecs.GetEntity(name);
	if (target == nullptr)
		return;

	DestroyEntity(ecs, target, destroyChildren);
}

bool WitchcraECSEntityHierarchyBridge::DestroyEntity(WitchcraECS& ecs, SceneEntityBase* target, bool destroyChildren)
{
	if (target == nullptr || !ecs.HasEntity(target))
		return false;

	SceneEntityBase* parent = ecs.GetParentEntity(target);
	if (destroyChildren)
	{
		DestroyEntitySubtree(ecs, target, parent);
		return true;
	}

	RemoveEntityPreserveChildren(ecs, target, parent);
	return true;
}

void WitchcraECSEntityHierarchyBridge::RemoveRootEntityPointer(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	auto it = std::find(ecs.entities.begin(), ecs.entities.end(), entity);
	if (it != ecs.entities.end())
		ecs.entities.erase(it);
}

void WitchcraECSEntityHierarchyBridge::FinalizeEntityHierarchyChange(WitchcraECS& ecs)
{
	ecs.MarkAllTransformsDirty();
}

void WitchcraECSEntityHierarchyBridge::MoveChildrenUpOneLevel(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* parent)
{
	if (entity == nullptr)
		return;

	std::vector<SceneEntityBase*> children = entity->ReleaseChildren();
	for (SceneEntityBase* child : children)
	{
		if (child == nullptr)
			continue;

		if (child->entity != 0 && entity->entity != 0)
			ecs_remove_pair(ecs.entityWorld, child->entity, EcsChildOf, entity->entity);

		EnsureEntityNameMatchesScope(ecs, child, parent);

		if (parent != nullptr)
		{
			parent->AddChild(ecs.GetEntityName(child), child);
			UpdateEntityParentIndexEntry(ecs, child, parent);
			if (child->entity != 0 && parent->entity != 0)
				ecs_add_pair(ecs.entityWorld, child->entity, EcsChildOf, parent->entity);
		}
		else
		{
			ecs.entities.push_back(child);
			UpdateEntityParentIndexEntry(ecs, child, nullptr);
		}
	}
}

void WitchcraECSEntityHierarchyBridge::DestroyEntitySubtree(WitchcraECS& ecs, SceneEntityBase* target, SceneEntityBase* parent)
{
	if (target == nullptr)
		return;

	if (ContainsEntity(ecs, target, ecs.selectedEntity))
		ecs.selectedEntity = nullptr;

	ecs.mHierarchySelectedEntities.erase(
		std::remove_if(
			ecs.mHierarchySelectedEntities.begin(),
			ecs.mHierarchySelectedEntities.end(),
			[&](SceneEntityBase* selected)
			{
				return selected != nullptr && ContainsEntity(ecs, target, selected);
			}),
		ecs.mHierarchySelectedEntities.end());
	if (ecs.selectedEntity == nullptr && !ecs.mHierarchySelectedEntities.empty())
		ecs.selectedEntity = ecs.mHierarchySelectedEntities.back();

	UnregisterEntitySubtreeIndices(ecs, target);
	ecs.DestroyEntitySubtreeComponents(target);

	if (parent == nullptr)
		RemoveRootEntityPointer(ecs, target);
	else
		parent->RemoveChild(target);

	DeleteEntityTree(ecs, target);
	FinalizeEntityHierarchyChange(ecs);
	ecs.MarkSceneDirty();
}

void WitchcraECSEntityHierarchyBridge::RemoveEntityPreserveChildren(WitchcraECS& ecs, SceneEntityBase* target, SceneEntityBase* parent)
{
	if (target == nullptr)
		return;

	if (ecs.selectedEntity == target)
		ecs.selectedEntity = nullptr;

	ecs.mHierarchySelectedEntities.erase(
		std::remove(ecs.mHierarchySelectedEntities.begin(), ecs.mHierarchySelectedEntities.end(), target),
		ecs.mHierarchySelectedEntities.end());
	if (ecs.selectedEntity == nullptr && !ecs.mHierarchySelectedEntities.empty())
		ecs.selectedEntity = ecs.mHierarchySelectedEntities.back();

	RemoveEntityIndexEntry(ecs, target);
	if (parent == nullptr)
		RemoveRootEntityPointer(ecs, target);

	MoveChildrenUpOneLevel(ecs, target, parent);
	ecs.DestroyEntityComponents(target);
	if (target->entity != 0)
		ecs_delete(ecs.entityWorld, target->entity);

	if (parent != nullptr)
		parent->RemoveChild(target);

	delete target;
	FinalizeEntityHierarchyChange(ecs);
}

bool WitchcraECSEntityHierarchyBridge::RenameEntity(WitchcraECS& ecs, SceneEntityBase* entity, const std::wstring& newName)
{
	if (entity == nullptr || newName.empty())
		return false;

	if (ecs.GetEntityName(entity) == newName)
		return true;

	SceneEntityBase* parent = ecs.GetParentEntity(entity);
	if (!ecs.IsEntityNameAvailable(newName, parent, entity))
		return false;

	ApplyEntityNameChange(ecs, entity, newName);
	ecs.MarkSceneDirty();
	return true;
}

bool WitchcraECSEntityHierarchyBridge::CanReparentEntityInHierarchy(const WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* newParent)
{
	if (entity == nullptr || !ecs.HasEntity(entity))
		return false;

	if (ecs.IsSkeletonHierarchyEntity(entity))
		return false;

	if (ecs.IsEnvironmentEntity(entity))
		return false;

	if (ecs.IsAmbientLightEntity(entity) && newParent != ecs.GetEnvironmentEntity())
		return false;

	if (newParent == nullptr)
		return true;

	if (!ecs.HasEntity(newParent))
		return false;

	if (IsSkeletonHierarchyDropTarget(ecs, newParent))
		return false;

	if (ecs.IsEnvironmentEntity(newParent) && !ecs.IsAmbientLightEntity(entity))
		return false;

	if (entity == newParent)
		return false;

	if (ContainsEntity(ecs, entity, newParent))
		return false;

	return true;
}

bool WitchcraECSEntityHierarchyBridge::ReparentEntityInHierarchy(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* newParent)
{
	if (!CanReparentEntityInHierarchy(ecs, entity, newParent))
		return false;

	SceneEntityBase* oldParent = ecs.GetParentEntity(entity);
	if (oldParent == newParent)
		return true;

	if (oldParent != nullptr)
	{
		oldParent->RemoveChild(entity);
		if (entity->entity != 0 && oldParent->entity != 0)
			ecs_remove_pair(ecs.entityWorld, entity->entity, EcsChildOf, oldParent->entity);
	}
	else
	{
		RemoveRootEntityPointer(ecs, entity);
	}

	EnsureEntityNameMatchesScope(ecs, entity, newParent);

	if (newParent != nullptr)
	{
		newParent->AddChild(ecs.GetEntityName(entity), entity);
		UpdateEntityParentIndexEntry(ecs, entity, newParent);
		if (entity->entity != 0 && newParent->entity != 0)
			ecs_add_pair(ecs.entityWorld, entity->entity, EcsChildOf, newParent->entity);
	}
	else
	{
		ecs.entities.push_back(entity);
		UpdateEntityParentIndexEntry(ecs, entity, nullptr);
	}

	FinalizeEntityHierarchyChange(ecs);
	ecs.MarkSceneDirty();
	return true;
}
