#include "WitchcraECSScriptingBridge.h"

#include "ECS/COMPONENT/ScriptingComponent.h"

#include <filesystem>

void WitchcraECSScriptingBridge::SyncComponentToFlecs(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mScriptingComponentDataId == 0)
		return;

	ScriptingComponent* component = ecs.GetComponent<ScriptingComponent>(entity);
	if (component == nullptr)
	{
		RemoveComponentDataFromFlecs(ecs, entity);
		return;
	}

	component->BindEntity(&ecs, entity);

	const EntityScriptingComponentData* currentData = static_cast<const EntityScriptingComponentData*>(
		ecs_get_id(ecs.entityWorld, entity->entity, ecs.mScriptingComponentDataId));
	if (currentData == nullptr)
		ecs.entityWorld.entity(entity->entity).set<EntityScriptingComponentData>(EntityScriptingComponentData{});
}

void WitchcraECSScriptingBridge::RemoveComponentDataFromFlecs(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mScriptingComponentDataId == 0)
		return;

	ecs_remove_id(ecs.entityWorld, entity->entity, ecs.mScriptingComponentDataId);
}

bool WitchcraECSScriptingBridge::GetEntitySnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, EntityScriptingComponentData* outSnapshot)
{
	if (outSnapshot == nullptr)
		return false;

	if (entity == nullptr || entity->entity == 0 || ecs.mScriptingComponentDataId == 0)
		return false;

	const EntityScriptingComponentData* scriptingData = static_cast<const EntityScriptingComponentData*>(
		ecs_get_id(ecs.entityWorld, entity->entity, ecs.mScriptingComponentDataId));
	if (scriptingData == nullptr)
		return false;

	*outSnapshot = *scriptingData;
	outSnapshot->scriptCount = static_cast<std::uint32_t>(outSnapshot->scripts.size());
	return true;
}

bool WitchcraECSScriptingBridge::GetEntityScriptSnapshot(const WitchcraECS& ecs, SceneEntityBase* entity, size_t index, EntityScriptingComponentData::ScriptSnapshot* outSnapshot)
{
	if (outSnapshot == nullptr)
		return false;

	EntityScriptingComponentData snapshot;
	if (!GetEntitySnapshot(ecs, entity, &snapshot) || index >= snapshot.scripts.size())
		return false;

	*outSnapshot = snapshot.scripts[index];
	return true;
}

bool WitchcraECSScriptingBridge::AddEntityScript(WitchcraECS& ecs, SceneEntityBase* entity, const std::wstring& scriptPath)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mScriptingComponentDataId == 0 || scriptPath.empty())
		return false;
	if (ecs.IsEnvironmentEntity(entity))
		return false;

	ScriptingComponent* component = ecs.AddComponent<ScriptingComponent>(entity);
	if (component == nullptr)
		return false;
	ecs.SyncScriptingComponentToFlecs(entity);

	EntityScriptingComponentData snapshot;
	if (!GetEntitySnapshot(ecs, entity, &snapshot))
		snapshot = EntityScriptingComponentData{};

	const bool duplicatedPath =
		std::find(snapshot.scriptPaths.begin(), snapshot.scriptPaths.end(), scriptPath) != snapshot.scriptPaths.end() ||
		std::any_of(snapshot.scripts.begin(), snapshot.scripts.end(),
			[&scriptPath](const EntityScriptingComponentData::ScriptSnapshot& script)
			{
				return script.filePath == scriptPath;
			});
	if (duplicatedPath)
		return false;

	EntityScriptingComponentData::ScriptSnapshot scriptSnapshot;
	scriptSnapshot.filePath = scriptPath;
	scriptSnapshot.fileName = std::filesystem::path(scriptPath).filename().wstring();
	if (scriptSnapshot.fileName.empty())
		scriptSnapshot.fileName = scriptPath;
	scriptSnapshot.activeComponent = true;
	scriptSnapshot.error = false;

	snapshot.scriptPaths.push_back(scriptSnapshot.filePath);
	snapshot.scripts.push_back(scriptSnapshot);
	snapshot.scriptCount = static_cast<std::uint32_t>(snapshot.scripts.size());
	ecs.entityWorld.entity(entity->entity).set<EntityScriptingComponentData>(snapshot);
	return true;
}

bool WitchcraECSScriptingBridge::SetEntityScriptActive(WitchcraECS& ecs, SceneEntityBase* entity, size_t index, bool active)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mScriptingComponentDataId == 0)
		return false;

	ScriptingComponent* component = ecs.AddComponent<ScriptingComponent>(entity);
	if (component == nullptr)
		return false;
	ecs.SyncScriptingComponentToFlecs(entity);

	EntityScriptingComponentData snapshot;
	if (!GetEntitySnapshot(ecs, entity, &snapshot) || index >= snapshot.scripts.size())
		return false;

	snapshot.scripts[index].activeComponent = active;
	snapshot.scriptCount = static_cast<std::uint32_t>(snapshot.scripts.size());
	ecs.entityWorld.entity(entity->entity).set<EntityScriptingComponentData>(snapshot);
	return true;
}

bool WitchcraECSScriptingBridge::RemoveEntityScript(WitchcraECS& ecs, SceneEntityBase* entity, size_t index)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mScriptingComponentDataId == 0)
		return false;

	ScriptingComponent* component = ecs.AddComponent<ScriptingComponent>(entity);
	if (component == nullptr)
		return false;
	ecs.SyncScriptingComponentToFlecs(entity);

	EntityScriptingComponentData snapshot;
	if (!GetEntitySnapshot(ecs, entity, &snapshot) || index >= snapshot.scripts.size())
		return false;

	snapshot.scripts.erase(snapshot.scripts.begin() + static_cast<std::ptrdiff_t>(index));
	if (index < snapshot.scriptPaths.size())
		snapshot.scriptPaths.erase(snapshot.scriptPaths.begin() + static_cast<std::ptrdiff_t>(index));
	else
	{
		snapshot.scriptPaths.clear();
		snapshot.scriptPaths.reserve(snapshot.scripts.size());
		for (const EntityScriptingComponentData::ScriptSnapshot& script : snapshot.scripts)
			snapshot.scriptPaths.push_back(script.filePath);
	}

	snapshot.scriptCount = static_cast<std::uint32_t>(snapshot.scripts.size());
	ecs.entityWorld.entity(entity->entity).set<EntityScriptingComponentData>(snapshot);
	return true;
}
