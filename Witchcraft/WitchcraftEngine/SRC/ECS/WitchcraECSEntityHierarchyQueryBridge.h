#pragma once

#include "WitchcraECS.h"

class WitchcraECSEntityHierarchyQueryBridge
{
public:
	static SceneEntityBase* GetParentEntity(const WitchcraECS& ecs, SceneEntityBase* entity);
	static bool IsRootEntity(const WitchcraECS& ecs, SceneEntityBase* entity);
	static bool IsEntityNameAvailable(const WitchcraECS& ecs, const std::wstring& candidate, SceneEntityBase* parent = nullptr, SceneEntityBase* ignoreEntity = nullptr);
	static std::wstring GetUniqueEntityName(const WitchcraECS& ecs, const std::wstring& desiredName, SceneEntityBase* parent = nullptr);
	static const std::vector<SceneEntityBase*>& GetEntityChildren(const WitchcraECS& ecs, SceneEntityBase* entity);
	static UINT GetEntityChildCount(const WitchcraECS& ecs, SceneEntityBase* entity);
	static bool SelectEntityForHierarchy(WitchcraECS& ecs, SceneEntityBase* entity);
	static void ClearHierarchySelection(WitchcraECS& ecs);
	static bool DeleteEntityFromHierarchy(WitchcraECS& ecs, SceneEntityBase* entity, bool destroyChildren);
	static bool HasSelectedEntity(const WitchcraECS& ecs);
	static SceneEntityBase* GetSelectedEntity(const WitchcraECS& ecs);
};