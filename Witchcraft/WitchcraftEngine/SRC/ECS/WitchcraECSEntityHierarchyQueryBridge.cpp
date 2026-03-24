#include "WitchcraECSEntityHierarchyQueryBridge.h"

SceneEntityBase* WitchcraECSEntityHierarchyQueryBridge::GetParentEntity(const WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return nullptr;

	auto it = ecs.mParentEntityIndex.find(entity);
	if (it == ecs.mParentEntityIndex.end())
		return nullptr;

	return it->second;
}

bool WitchcraECSEntityHierarchyQueryBridge::IsRootEntity(const WitchcraECS& ecs, SceneEntityBase* entity)
{
	return entity != nullptr && GetParentEntity(ecs, entity) == nullptr && ecs.HasEntity(entity);
}

bool WitchcraECSEntityHierarchyQueryBridge::IsEntityNameAvailable(const WitchcraECS& ecs, const std::wstring& candidate, SceneEntityBase* parent, SceneEntityBase* ignoreEntity)
{
	if (candidate.empty())
		return false;

	if (parent != nullptr)
	{
		for (SceneEntityBase* child : GetEntityChildren(ecs, parent))
		{
			if (child == nullptr || child == ignoreEntity)
				continue;
			if (ecs.GetEntityName(child) == candidate)
				return false;
		}

		return true;
	}

	for (SceneEntityBase* rootEntity : ecs.entities)
	{
		if (rootEntity == nullptr || rootEntity == ignoreEntity)
			continue;
		if (ecs.GetEntityName(rootEntity) == candidate)
			return false;
	}

	return true;
}

std::wstring WitchcraECSEntityHierarchyQueryBridge::GetUniqueEntityName(const WitchcraECS& ecs, const std::wstring& desiredName, SceneEntityBase* parent)
{
	const std::wstring baseName = desiredName.empty() ? L"Entity" : desiredName;
	if (IsEntityNameAvailable(ecs, baseName, parent))
		return baseName;

	UINT suffix = 1;
	while (true)
	{
		const std::wstring candidate = baseName + L"_" + std::to_wstring(suffix);
		if (IsEntityNameAvailable(ecs, candidate, parent))
			return candidate;
		++suffix;
	}
}

const std::vector<SceneEntityBase*>& WitchcraECSEntityHierarchyQueryBridge::GetEntityChildren(const WitchcraECS&, SceneEntityBase* entity)
{
	static const std::vector<SceneEntityBase*> emptyChildren;
	return entity != nullptr ? entity->GetChildrenEntity() : emptyChildren;
}

UINT WitchcraECSEntityHierarchyQueryBridge::GetEntityChildCount(const WitchcraECS& ecs, SceneEntityBase* entity)
{
	return static_cast<UINT>(GetEntityChildren(ecs, entity).size());
}

bool WitchcraECSEntityHierarchyQueryBridge::SelectEntityForHierarchy(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
	{
		ecs.selectedEntity = nullptr;
		return true;
	}

	if (!ecs.HasEntity(entity))
		return false;

	ecs.selectedEntity = entity;
	return true;
}

void WitchcraECSEntityHierarchyQueryBridge::ClearHierarchySelection(WitchcraECS& ecs)
{
	ecs.selectedEntity = nullptr;
}

bool WitchcraECSEntityHierarchyQueryBridge::DeleteEntityFromHierarchy(WitchcraECS& ecs, SceneEntityBase* entity, bool destroyChildren)
{
	return ecs.DestroyEntity(entity, destroyChildren);
}

bool WitchcraECSEntityHierarchyQueryBridge::HasSelectedEntity(const WitchcraECS& ecs)
{
	return ecs.selectedEntity != nullptr;
}

SceneEntityBase* WitchcraECSEntityHierarchyQueryBridge::GetSelectedEntity(const WitchcraECS& ecs)
{
	return ecs.selectedEntity;
}