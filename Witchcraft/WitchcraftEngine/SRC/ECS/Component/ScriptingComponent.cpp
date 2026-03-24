#include "ScriptingComponent.h"

#include "ECS/WitchcraECS.h"

void ScriptingComponent::BindEntity(WitchcraECS* ecs, SceneEntityBase* ownerEntity)
{
	mEcs = ecs;
	mOwnerEntity = ownerEntity;
}

bool ScriptingComponent::TryGetSnapshot(EntityScriptingComponentData* outSnapshot) const
{
	if (outSnapshot == nullptr)
		return false;

	if (mEcs == nullptr || mOwnerEntity == nullptr || !mEcs->HasEntity(mOwnerEntity))
		return false;

	return mEcs->GetEntityScriptingSnapshot(mOwnerEntity, outSnapshot);
}

void ScriptingComponent::RebuildCacheFromSnapshot() const
{
	EntityScriptingComponentData snapshot;
	if (!TryGetSnapshot(&snapshot))
	{
		mCachedScripts.clear();
		return;
	}

	mCachedScripts.clear();
	mCachedScripts.reserve(snapshot.scripts.size());
	for (const EntityScriptingComponentData::ScriptSnapshot& scriptSnapshot : snapshot.scripts)
	{
		ScriptBuffer scriptBuffer;
		scriptBuffer.filePath = scriptSnapshot.filePath;
		scriptBuffer.fileName = scriptSnapshot.fileName;
		scriptBuffer.fileNameToUpper = scriptBuffer.fileName;
		scriptBuffer.activeComponent = scriptSnapshot.activeComponent;
		scriptBuffer.error = scriptSnapshot.error;
		mCachedScripts.push_back(std::move(scriptBuffer));
	}
}

void ScriptingComponent::AddScript(const wchar_t* path)
{
	if (path == nullptr || path[0] == L'\0' || mEcs == nullptr || mOwnerEntity == nullptr)
		return;

	mEcs->AddScriptToEntity(mOwnerEntity, path);
}

bool ScriptingComponent::RemoveScript(size_t index)
{
	if (mEcs == nullptr || mOwnerEntity == nullptr)
		return false;

	return mEcs->RemoveEntityScript(mOwnerEntity, index);
}

void ScriptingComponent::lua_call_start()
{
}

void ScriptingComponent::lua_call_update()
{
}

void ScriptingComponent::RecompileScripts()
{
}

size_t ScriptingComponent::GetScriptCount() const
{
	RebuildCacheFromSnapshot();
	return mCachedScripts.size();
}

const std::vector<ScriptBuffer>& ScriptingComponent::GetScripts() const
{
	RebuildCacheFromSnapshot();
	return mCachedScripts;
}

const ScriptBuffer* ScriptingComponent::GetScript(size_t index) const
{
	RebuildCacheFromSnapshot();
	if (index >= mCachedScripts.size())
		return nullptr;

	return &mCachedScripts[index];
}

bool ScriptingComponent::SetScriptActive(size_t index, bool active)
{
	if (mEcs == nullptr || mOwnerEntity == nullptr)
		return false;

	return mEcs->SetEntityScriptActive(mOwnerEntity, index, active);
}

void ScriptingComponent::lua_add_entity_from_component()
{
}
