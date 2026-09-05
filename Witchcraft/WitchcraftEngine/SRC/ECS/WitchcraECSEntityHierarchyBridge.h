#pragma once

#include "WitchcraECS.h"

class WitchcraECSEntityHierarchyBridge
{
public:
	static void RegisterEntitySubtreeIndices(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* parent);
	static void UnregisterEntitySubtreeIndices(WitchcraECS& ecs, SceneEntityBase* entity);
	static void AddEntityNameIndexEntry(WitchcraECS& ecs, SceneEntityBase* entity);
	static void RemoveEntityNameIndexEntry(WitchcraECS& ecs, const std::wstring& name, SceneEntityBase* entity);
	static void UpdateEntityParentIndexEntry(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* parent);
	static void RemoveEntityParentIndexEntry(WitchcraECS& ecs, SceneEntityBase* entity);
	static void RemoveEntityIndexEntry(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool ContainsEntity(const WitchcraECS& ecs, SceneEntityBase* root, SceneEntityBase* target);
	static void DeleteEntityTree(WitchcraECS& ecs, SceneEntityBase* entity);
	static void CreateEntity(WitchcraECS& ecs, std::wstring name, SceneEntityBase* entity);
	static void CreateEntity(WitchcraECS& ecs, std::wstring name, SceneEntityBase* entity, SceneEntityBase* parent);
	static void DestroyEntity(WitchcraECS& ecs, std::wstring name, bool destroyChildren = true);
	static bool DestroyEntity(WitchcraECS& ecs, SceneEntityBase* target, bool destroyChildren = true);
	static void RemoveRootEntityPointer(WitchcraECS& ecs, SceneEntityBase* entity);
	static void FinalizeEntityHierarchyChange(WitchcraECS& ecs);
	static void MoveChildrenUpOneLevel(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* parent);
	static void DestroyEntitySubtree(WitchcraECS& ecs, SceneEntityBase* target, SceneEntityBase* parent);
	static void RemoveEntityPreserveChildren(WitchcraECS& ecs, SceneEntityBase* target, SceneEntityBase* parent);
	static bool RenameEntity(WitchcraECS& ecs, SceneEntityBase* entity, const std::wstring& newName);
	static bool CanReparentEntityInHierarchy(const WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* newParent);
	static bool ReparentEntityInHierarchy(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* newParent);

private:
	static void ApplyEntityNameChange(WitchcraECS& ecs, SceneEntityBase* entity, const std::wstring& newName);
	static void EnsureEntityNameMatchesScope(WitchcraECS& ecs, SceneEntityBase* entity, SceneEntityBase* parent);
	static bool IsSkeletonHierarchyDropTarget(const WitchcraECS& ecs, SceneEntityBase* entity);
};
