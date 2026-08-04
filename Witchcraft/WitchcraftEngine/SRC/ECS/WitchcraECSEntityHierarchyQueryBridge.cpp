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

bool WitchcraECSEntityHierarchyQueryBridge::SelectEntityForHierarchy(WitchcraECS& ecs, SceneEntityBase* entity, bool additive)
{
	if (entity == nullptr)
	{
		ecs.selectedEntity = nullptr;
		if (!additive)
			ecs.mHierarchySelectedEntities.clear();
		ecs.LogDebugMessage(L"[Selection] SelectEntityForHierarchy(null), additive=%d, count=%u", additive ? 1 : 0, static_cast<UINT>(ecs.mHierarchySelectedEntities.size()));
		return true;
	}

	if (!ecs.HasEntity(entity))
	{
		ecs.LogDebugMessage(L"[Selection] Reject select: entity not found, name=%s, additive=%d", ecs.GetEntityName(entity).c_str(), additive ? 1 : 0);
		return false;
	}

	if (!additive)
		ecs.mHierarchySelectedEntities.clear();

	auto selectedIt = std::find(ecs.mHierarchySelectedEntities.begin(), ecs.mHierarchySelectedEntities.end(), entity);
	if (selectedIt == ecs.mHierarchySelectedEntities.end())
		ecs.mHierarchySelectedEntities.push_back(entity);

	ecs.selectedEntity = entity;
	ecs.LogDebugMessage(L"[Selection] Selected: name=%s, additive=%d, count=%u", ecs.GetEntityName(entity).c_str(), additive ? 1 : 0, static_cast<UINT>(ecs.mHierarchySelectedEntities.size()));
	return true;
}

bool WitchcraECSEntityHierarchyQueryBridge::IsEntitySelectedInHierarchy(const WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
		return false;

	if (ecs.selectedEntity == entity)
		return true;

	return std::find(ecs.mHierarchySelectedEntities.begin(), ecs.mHierarchySelectedEntities.end(), entity) != ecs.mHierarchySelectedEntities.end();
}

void WitchcraECSEntityHierarchyQueryBridge::ClearHierarchySelection(WitchcraECS& ecs)
{
	ecs.selectedEntity = nullptr;
	ecs.mHierarchySelectedEntities.clear();
	ecs.LogDebugMessage(L"[Selection] ClearHierarchySelection()");
}

bool WitchcraECSEntityHierarchyQueryBridge::DeleteEntityFromHierarchy(WitchcraECS& ecs, SceneEntityBase* entity, bool destroyChildren)
{
	return ecs.DestroyEntity(entity, destroyChildren);
}

bool WitchcraECSEntityHierarchyQueryBridge::HasSelectedEntity(const WitchcraECS& ecs)
{
	return GetSelectedEntity(ecs) != nullptr;
}

SceneEntityBase* WitchcraECSEntityHierarchyQueryBridge::GetSelectedEntity(const WitchcraECS& ecs)
{
	if (ecs.selectedEntity != nullptr && ecs.HasEntity(ecs.selectedEntity))
		return ecs.selectedEntity;

	if (ecs.mHierarchySelectedEntities.empty())
		return nullptr;

	return ecs.mHierarchySelectedEntities.back();
}

std::vector<SceneEntityBase*> WitchcraECSEntityHierarchyQueryBridge::GetHierarchySelectionSnapshot(const WitchcraECS& ecs)
{
	std::vector<SceneEntityBase*> result;
	result.reserve(ecs.mHierarchySelectedEntities.size());

	for (SceneEntityBase* entity : ecs.mHierarchySelectedEntities)
	{
		if (entity != nullptr && ecs.HasEntity(entity))
			result.push_back(entity);
	}

	return result;
}

std::vector<SceneEntityBase*> WitchcraECSEntityHierarchyQueryBridge::GetHierarchySelectionRootSnapshot(const WitchcraECS& ecs)
{
	std::vector<SceneEntityBase*> selection = GetHierarchySelectionSnapshot(ecs);
	std::vector<SceneEntityBase*> result;
	result.reserve(selection.size());

	for (SceneEntityBase* entity : selection)
	{
		bool hasSelectedAncestor = false;
		SceneEntityBase* parent = GetParentEntity(ecs, entity);
		while (parent != nullptr)
		{
			if (std::find(selection.begin(), selection.end(), parent) != selection.end())
			{
				hasSelectedAncestor = true;
				break;
			}
			parent = GetParentEntity(ecs, parent);
		}

		if (!hasSelectedAncestor)
			result.push_back(entity);
	}

	return result;
}
