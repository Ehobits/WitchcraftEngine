#pragma once

#include "WitchcraECS.h"

class WitchcraECSScriptingBridge
{
public:
	static void SyncComponentToFlecs(WitchcraECS& ecs, SceneEntityBase* entity);
	static void RemoveComponentDataFromFlecs(WitchcraECS& ecs, SceneEntityBase* entity);
	static bool GetEntitySnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, EntityScriptingComponentData* outSnapshot);
	static bool GetEntityScriptSnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, size_t index, EntityScriptingComponentData::ScriptSnapshot* outSnapshot);
	static bool AddEntityScript(WitchcraECS& ecs, SceneEntityBase* entity, const std::wstring& scriptPath);
	static bool SetEntityScriptActive(WitchcraECS& ecs, SceneEntityBase* entity, size_t index, bool active);
	static bool RemoveEntityScript(WitchcraECS& ecs, SceneEntityBase* entity, size_t index);
};
