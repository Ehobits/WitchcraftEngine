#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <xstring>

//#define SOL_ALL_SAFETIES_ON 1
#define SOL_PRINT_ERRORS 1
//#define SOL_EXCEPTIONS 1
#include <sol/sol.hpp>

#include "Common/SceneEntityType.h"
#include "Engine/EngineUtils.h"
#include "SYSTEM/PhysicsSystem.h"

class WitchcraECS;
class SceneEntityBase;
class AnimatorComponent;
class BillboardComponent;
class BoneAttachmentComponent;
class CameraComponent;
class GeneralComponent;
class LightComponent;
class MeshComponent;
class PhysicsComponent;
class RenderDrawSetComponent;
class RigidBodyComponent;
class ScriptingComponent;
class SkeletonComponent;
class SkinnedMeshComponent;
class ScriptingSystem;

struct ScriptVector3
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
};

struct ScriptVector4
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	float W = 0.0f;
};

struct ScriptPhysicsColliderSnapshot
{
	std::uint32_t colliderType = static_cast<std::uint32_t>(PhysicsColliderType::Box);
	bool activeComponent = true;
	float staticFriction = PhysicsDefaultMaterial::StaticFriction;
	float dynamicFriction = PhysicsDefaultMaterial::DynamicFriction;
	float restitution = PhysicsDefaultMaterial::Restitution;
	ScriptVector3 center;
	ScriptVector3 size = { 1.0f, 1.0f, 1.0f };
};

struct ScriptTransformRef
{
	SceneEntityBase* Entity = nullptr;
	WitchcraECS* Ecs = nullptr;
	bool IsValid() const;
	ScriptVector3 GetPosition() const;
	void SetPosition(float x, float y, float z) const;
	ScriptVector3 GetRotation() const;
	void SetRotation(float x, float y, float z) const;
	ScriptVector3 GetScale() const;
	void SetScale(float x, float y, float z) const;
};

struct ScriptAnimatorRef
{
	AnimatorComponent* Animator = nullptr;
	bool IsValid() const;
	bool Play(const std::string& clipAssetPath, const std::string& layerName, float startTime, bool loop) const;
	bool CrossFade(const std::string& clipAssetPath, float fadeDuration, const std::string& layerName, float startTime, bool loop) const;
	bool Stop(const std::string& layerName) const;
	bool SetLayerWeight(const std::string& layerName, float weight) const;
	bool SetLayerSpeed(const std::string& layerName, float speed) const;
	bool SetLayerEnabled(const std::string& layerName, bool enabled) const;
};

struct ScriptEntityRef
{
	SceneEntityBase* Entity = nullptr;
	WitchcraECS* Ecs = nullptr;
	ScriptingSystem* Runtime = nullptr;
	bool IsValid() const;
	ScriptEntityRef GetParent() const;
	std::string GetName() const;
	bool SetName(const std::string& name) const;
	std::string GetTag() const;
	bool SetTag(const std::string& tag) const;
	bool IsVisible() const;
	bool SetVisible(bool visible) const;
	bool IsStatic() const;
	bool SetStatic(bool isStatic) const;
	SceneEntityType GetSceneType() const;
	bool SetSceneType(SceneEntityType type) const;
	ScriptTransformRef GetTransform() const;
	GeneralComponent* GetGeneralComponent() const;
	LightComponent* GetLightComponent() const;
	CameraComponent* GetCameraComponent() const;
	MeshComponent* GetMeshComponent() const;
	PhysicsComponent* GetPhysicsComponent() const;
	RigidBodyComponent* GetRigidBodyComponent() const;
	ScriptingComponent* GetScriptingComponent() const;
	BillboardComponent* GetBillboardComponent() const;
	BoneAttachmentComponent* GetBoneAttachmentComponent() const;
	SkeletonComponent* GetSkeletonComponent() const;
	SkinnedMeshComponent* GetSkinnedMeshComponent() const;
	ScriptAnimatorRef GetAnimator() const;
	bool AddBoxCollider() const;
	bool AddPlaneCollider() const;
	bool QueueCreateChild(const std::string& name) const;
	bool QueueDestroy(bool destroyChildren = true) const;
	bool QueueAddComponent(const std::string& componentName) const;
	bool QueueRemoveComponent(const std::string& componentName) const;
};

struct AnimationScriptEventNotification
{
	SceneEntityBase* OwnerEntity = nullptr;
	std::wstring EventName;
	std::wstring Parameter;
	std::wstring ScriptCallbackName;
	std::wstring ClipAssetPath;
	std::wstring LayerName;
	float EventTime = 0.0f;
	float PlaybackTime = 0.0f;
};

struct ScriptingRuntimeStats
{
	std::uint32_t HostEntityCount = 0;
	std::uint32_t ScriptEntryCount = 0;
	std::uint32_t ActiveScriptEntryCount = 0;
	std::uint32_t RuntimeInstanceCount = 0;
	std::uint32_t ErrorInstanceCount = 0;
	std::uint64_t ErrorRevision = 0;
	std::wstring LastErrorMessage;
	std::wstring LastErrorScriptPath;
};

class ScriptingSystem
{
public:
	bool Init();
	bool StartRuntime(WitchcraECS* ecs);
	void UpdateRuntime(WitchcraECS* ecs, float deltaTime);
	void StopRuntime(WitchcraECS* ecs);
	bool IsRuntimeActive() const;
	const ScriptingRuntimeStats& GetRuntimeStats() const;
	sol::state& GetState();
	void CreateScript(const wchar_t* filename, const wchar_t* name);
	void NotifyAnimationEvent(const AnimationScriptEventNotification& notification);
	bool QueueCreateChild(SceneEntityBase* parentEntity, const std::wstring& name);
	bool QueueDestroyEntity(SceneEntityBase* entity, bool destroyChildren);
	bool QueueAddComponent(SceneEntityBase* entity, const std::wstring& componentName);
	bool QueueRemoveComponent(SceneEntityBase* entity, const std::wstring& componentName);
	void FlushRuntimeCommands(WitchcraECS* ecs);

private:
	void ScanRuntimeScripts(WitchcraECS* ecs);
	void ScanRuntimeScriptsRecursive(WitchcraECS* ecs, SceneEntityBase* entity);
	void BuildRuntimeScriptInstances(WitchcraECS* ecs);
	void BuildRuntimeScriptInstancesRecursive(WitchcraECS* ecs, SceneEntityBase* entity);
	bool AddRuntimeScriptInstance(SceneEntityBase* entity, std::size_t scriptIndex, const std::wstring& scriptPath);
	void CallRuntimeStartCallbacks();
	void CallRuntimeUpdateCallbacks(float deltaTime);

	struct RuntimeCommand
	{
		enum class Type
		{
			CreateChild,
			DestroyEntity,
			AddComponent,
			RemoveComponent
		};

		Type CommandType = Type::CreateChild;
		SceneEntityBase* TargetEntity = nullptr;
		std::wstring Name;
		std::wstring ComponentName;
		bool DestroyChildren = true;
	};

	bool ExecuteRuntimeCommand(WitchcraECS* ecs, const RuntimeCommand& command);

	struct RuntimeScriptInstance
	{
		SceneEntityBase* OwnerEntity = nullptr;
		std::size_t ScriptIndex = 0;
		std::wstring ScriptPath;
		std::optional<sol::table> InstanceTable;
		bool Started = false;
		bool Error = false;
	};

	void RecordRuntimeScriptError(RuntimeScriptInstance& runtimeScript, const std::wstring& message);
	void RefreshRuntimeInstanceStats();

	sol::state lua;
	bool mRuntimeActive = false;
	WitchcraECS* mRuntimeEcs = nullptr;
	ScriptingRuntimeStats mRuntimeStats;
	float mRuntimeDeltaTime = 0.0f;
	float mRuntimeElapsedTime = 0.0f;
	std::uint64_t mRuntimeFrameCount = 0;
	std::vector<RuntimeScriptInstance> mRuntimeScripts;
	std::vector<RuntimeCommand> mRuntimeCommandBuffer;

private:
	/* system */
	void lua_add_console();
	void lua_add_time();
	void lua_add_input();
	void lua_add_bounding_box();

	/* entity */
	void lua_add_entity();
	void lua_add_general_component();
	void lua_add_transform_component();
	void lua_add_camera_component();
	void lua_add_light_component();
	void lua_add_billboard_component();
	void lua_add_bone_attachment_component();
	void lua_add_mesh_component();
	void lua_add_physics_component();
	void lua_add_rigidbody_component();
	void lua_add_scripting_component();
	void lua_add_skeleton_component();
	void lua_add_skinned_mesh_component();
};

struct ScriptBuffer
{
public:
	std::wstring filePath;
	std::wstring fileName;
	std::wstring fileNameToUpper; /* for imgui */

public:
	bool activeComponent = true;
	bool error = false;
};
