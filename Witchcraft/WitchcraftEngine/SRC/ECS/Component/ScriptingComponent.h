#pragma once

#include <Windows.h>

#include "System/ScriptingSystem.h"
#include "BaseComponent.h"

class WitchcraECS;
class SceneEntityBase;
struct EntityScriptingComponentData;

class ScriptingComponent : public BaseComponent
{
public:
	void BindEntity(WitchcraECS* ecs, SceneEntityBase* ownerEntity);
	void AddScript(const wchar_t* path);
	bool RemoveScript(size_t index);
	void RecompileScripts();
	size_t GetScriptCount() const;
	const std::vector<ScriptBuffer>& GetScripts() const;
	const ScriptBuffer* GetScript(size_t index) const;
	bool SetScriptActive(size_t index, bool active);

public:
	void lua_call_start();
	void lua_call_update();

public:
	virtual ComponentType GetComponentType() { return ComponentType::Co_Scripting; }

private:
	void lua_add_entity_from_component();
	bool TryGetSnapshot(EntityScriptingComponentData* outSnapshot) const;
	void RebuildCacheFromSnapshot() const;

	WitchcraECS* mEcs = nullptr;
	SceneEntityBase* mOwnerEntity = nullptr;
	mutable std::vector<ScriptBuffer> mCachedScripts;
};
