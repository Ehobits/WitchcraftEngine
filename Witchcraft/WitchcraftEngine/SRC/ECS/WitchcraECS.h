#pragma once

#include "Engine/EngineUtils.h"
#include "D3DWindow/D3D12_framework.h"
#include "Common/MeshSharedTypes.h"
#include "Common/TransformSharedTypes.h"
#include "System/ProjectSceneSystem.h"
#include "System/PhysicsSystem.h"
#include "ServicesContainer/ServicesContainer.h"
#include "Component/BaseComponent.h"

#include <flecs.h>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

class Engine;
class D3DWindow;

class GeneralComponent;
class CameraComponent;
class TransformComponent;
class MeshComponent;
class LightComponent;
class PhysicsComponent;
class ScriptingComponent;
class RigidBodyComponent;
class WitchcraECS;
class WitchcraECSComponentLifecycleBridge;
class WitchcraECSEntityHierarchyBridge;
class WitchcraECSEntityHierarchyQueryBridge;
class WitchcraECSInspectorBridge;
class WitchcraECSPhysicsBridge;
class WitchcraECSRigidBodyBridge;
class WitchcraECSScriptingBridge;
class WitchcraECSTransformSyncBridge;
class SceneEntityBase;
struct AggregateGraphicObj;

template<typename T>
struct ECSComponentServiceTraits
{
	static constexpr const wchar_t* Name = nullptr;
};

template<>
struct ECSComponentServiceTraits<GeneralComponent>
{
	static constexpr const wchar_t* Name = L"GeneralComponent";
};

template<>
struct ECSComponentServiceTraits<CameraComponent>
{
	static constexpr const wchar_t* Name = L"CameraComponent";
};

template<>
struct ECSComponentServiceTraits<TransformComponent>
{
	static constexpr const wchar_t* Name = L"TransformComponent";
};

template<>
struct ECSComponentServiceTraits<MeshComponent>
{
	static constexpr const wchar_t* Name = L"MeshComponent";
};

template<>
struct ECSComponentServiceTraits<LightComponent>
{
	static constexpr const wchar_t* Name = L"LightComponent";
};

template<>
struct ECSComponentServiceTraits<PhysicsComponent>
{
	static constexpr const wchar_t* Name = L"PhysicsComponent";
};

template<>
struct ECSComponentServiceTraits<ScriptingComponent>
{
	static constexpr const wchar_t* Name = L"ScriptingComponent";
};

template<>
struct ECSComponentServiceTraits<RigidBodyComponent>
{
	static constexpr const wchar_t* Name = L"RigidbodyComponent";
};

// flecs entity type tags.
struct EntityUnknownTag {};
struct EntityCameraTag {};
struct EntityMeshTag {};
struct EntityLightTag {};

// flecs transform data.
// LocalTransform: editable local position / rotation / scale.
// WorldMatrix: authoritative runtime world matrix.
// WorldTransform: world-space transform decomposed from WorldMatrix.
struct EntityLocalTransform
{
	Transform value;
};

struct EntityWorldTransform
{
	Transform value;
};

struct EntityWorldMatrix
{
	DirectX::XMFLOAT4X4 value;
};

// 实体通用组件数据
struct EntityGeneralComponentData
{
	bool visible = true;
	std::uint32_t componentType = static_cast<std::uint32_t>(ComponentType::Co_Unk);
};

// 实体相机组件数据
struct EntityCameraComponentData
{
	float nearZ = 1.0f;
	float farZ = 1000.0f;
	float fovY = 0.25f;
	float viewportScale = 1.0f;
};

struct EntityLightComponentData
{
	std::uint32_t kind = 1;
	float type = 1.0f;
	DirectX::XMFLOAT3 color = DirectX::XMFLOAT3(0.42f, 0.42f, 0.42f);
	float power = 1.2f;
	bool castShadow = true;
};

// 实体变换组件数据
struct EntityPhysicsComponentData
{
	std::uint32_t boxColliderCount = 0;
	struct ColliderSnapshot
	{
		bool activeComponent = true;
		float staticFriction = PhysicsDefaultMaterial::StaticFriction;
		float dynamicFriction = PhysicsDefaultMaterial::DynamicFriction;
		float restitution = PhysicsDefaultMaterial::Restitution;
		DirectX::XMFLOAT3 center = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		DirectX::XMFLOAT3 size = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	};
	std::vector<ColliderSnapshot> colliders;
};

// 实体刚体组件数据
struct EntityRigidBodyComponentData
{
	float mass = 1.0f;
	float linearDamping = 0.0f;
	float angularDamping = 0.0f;
	bool useGravity = true;
	bool kinematic = false;
	bool linearLockX = false;
	bool linearLockY = false;
	bool linearLockZ = false;
	bool angularLockX = false;
	bool angularLockY = false;
	bool angularLockZ = false;
};

// 实体脚本组件数据
struct EntityScriptingComponentData
{
	struct ScriptSnapshot
	{
		std::wstring filePath;
		std::wstring fileName;
		bool activeComponent = true;
		bool error = false;
	};

	std::uint32_t scriptCount = 0;
	std::vector<std::wstring> scriptPaths;
	std::vector<ScriptSnapshot> scripts;
};

// 实体组件视图
struct EntityComponentView
{
	SceneEntityBase* entity = nullptr;
	GeneralComponent* generalComponent = nullptr;
	CameraComponent* cameraComponent = nullptr;
	TransformComponent* transformComponent = nullptr;
	MeshComponent* meshComponent = nullptr;
	LightComponent* lightComponent = nullptr;
	PhysicsComponent* physicsComponent = nullptr;
	ScriptingComponent* scriptingComponent = nullptr;
	RigidBodyComponent* rigidbodyComponent = nullptr;
	Transform editableLocalTransform{};
	std::wstring entityTypeLabel;
	bool isSkyEntity = false;
	bool hasInspectableComponents = false;
};

// 实体渲染视图
struct EntityRenderView
{
	SceneEntityBase* entity = nullptr;
	MeshComponent* meshComponent = nullptr;
	std::wstring renderItemName;
	std::wstring geometryName;
	std::wstring materialName;
	UINT renderLayerIndex = 0;
	bool visible = false;
	bool isSkyEntity = false;
};

class SceneEntityBase
{
public:
	SceneEntityBase()
	{
		childrenContainer.Init(L"EntityBase");
	}

	virtual ~SceneEntityBase() = default;

public:
	flecs::entity_t entity = 0;

	void SetTag(std::wstring tag);
	void SetStatic(bool arg);
	std::wstring GetName();
	std::wstring GetTag();
	bool IsStatic();
	const std::vector<SceneEntityBase*>& GetChildrenEntity() const;

private:
	friend class WitchcraECS;
	friend class WitchcraECSEntityHierarchyBridge;

	void SetName(std::wstring name);
	void AddChild(std::wstring name, SceneEntityBase* Entity);
	bool RemoveChild(SceneEntityBase* Entity);
	std::vector<SceneEntityBase*> ReleaseChildren();

protected:
	std::wstring nameEntity = L"基本实体";
	std::wstring tagEntity = L"空";
	bool staticEntity = false;

	ServicesContainer childrenContainer;
	std::vector<SceneEntityBase*> childrenEntity;
};

class WitchcraECS
{
public:
	WitchcraECS();
	~WitchcraECS();

	bool Init();

	SceneEntityBase* CreateBasicEntity(const std::wstring& name, SceneEntityBase* parent = nullptr, ComponentType componentType = ComponentType::Co_Unk);
	SceneEntityBase* CreateMeshEntity(const std::wstring& name, SceneEntityBase* parent = nullptr);
	SceneEntityBase* CreateCameraEntity(const std::wstring& name, SceneEntityBase* parent = nullptr);
	SceneEntityBase* CreateLightEntity(const std::wstring& name, SceneEntityBase* parent = nullptr);
	bool DestroyEntity(SceneEntityBase* entity, bool destroyChildren = true);
	bool RenameEntity(SceneEntityBase* entity, const std::wstring& newName);
	bool SetEntityTag(SceneEntityBase* entity, const std::wstring& newTag);
	bool SetEntityStatic(SceneEntityBase* entity, bool isStatic);
	GeneralComponent* AddGeneralComponent(SceneEntityBase* entity);
	TransformComponent* AddTransformComponent(SceneEntityBase* entity);
	MeshComponent* AddMeshComponent(SceneEntityBase* entity);
	CameraComponent* AddCameraComponent(SceneEntityBase* entity);
	LightComponent* AddLightComponent(SceneEntityBase* entity);
	PhysicsComponent* AddPhysicsComponent(SceneEntityBase* entity);
	ScriptingComponent* AddScriptingComponent(SceneEntityBase* entity);
	RigidBodyComponent* AddRigidbodyComponent(SceneEntityBase* entity);
	GeneralComponent* ReplaceGeneralComponent(SceneEntityBase* entity);
	TransformComponent* ReplaceTransformComponent(SceneEntityBase* entity);
	MeshComponent* ReplaceMeshComponent(SceneEntityBase* entity);
	CameraComponent* ReplaceCameraComponent(SceneEntityBase* entity);
	PhysicsComponent* ReplacePhysicsComponent(SceneEntityBase* entity);
	ScriptingComponent* ReplaceScriptingComponent(SceneEntityBase* entity);
	RigidBodyComponent* ReplaceRigidbodyComponent(SceneEntityBase* entity);
	bool RemoveGeneralComponent(SceneEntityBase* entity);
	bool RemoveTransformComponent(SceneEntityBase* entity);
	bool RemoveMeshComponent(SceneEntityBase* entity);
	bool RemoveCameraComponent(SceneEntityBase* entity);
	bool RemovePhysicsComponent(SceneEntityBase* entity);
	bool RemoveScriptingComponent(SceneEntityBase* entity);
	bool RemoveRigidbodyComponent(SceneEntityBase* entity);

	bool HasEntity(SceneEntityBase* entity) const;
	SceneEntityBase* GetParentEntity(SceneEntityBase* entity) const;
	bool IsRootEntity(SceneEntityBase* entity) const;
	bool IsEntityNameAvailable(const std::wstring& candidate, SceneEntityBase* parent = nullptr, SceneEntityBase* ignoreEntity = nullptr) const;
	std::wstring GetUniqueEntityName(const std::wstring& desiredName, SceneEntityBase* parent = nullptr) const;
	template<typename T>
	T* AddComponent(SceneEntityBase* entity)
	{
		if constexpr (std::is_same_v<T, GeneralComponent>)
			return AddGeneralComponent(entity);
		else if constexpr (std::is_same_v<T, TransformComponent>)
			return AddTransformComponent(entity);
		else if constexpr (std::is_same_v<T, MeshComponent>)
			return AddMeshComponent(entity);
		else if constexpr (std::is_same_v<T, CameraComponent>)
			return AddCameraComponent(entity);
		else if constexpr (std::is_same_v<T, PhysicsComponent>)
			return AddPhysicsComponent(entity);
		else if constexpr (std::is_same_v<T, ScriptingComponent>)
			return AddScriptingComponent(entity);
		else if constexpr (std::is_same_v<T, RigidBodyComponent>)
			return AddRigidbodyComponent(entity);
		else
			return nullptr;
	}
	template<typename T>
	T* ReplaceComponent(SceneEntityBase* entity)
	{
		if constexpr (std::is_same_v<T, GeneralComponent>)
			return ReplaceGeneralComponent(entity);
		else if constexpr (std::is_same_v<T, TransformComponent>)
			return ReplaceTransformComponent(entity);
		else if constexpr (std::is_same_v<T, MeshComponent>)
			return ReplaceMeshComponent(entity);
		else if constexpr (std::is_same_v<T, CameraComponent>)
			return ReplaceCameraComponent(entity);
		else if constexpr (std::is_same_v<T, PhysicsComponent>)
			return ReplacePhysicsComponent(entity);
		else if constexpr (std::is_same_v<T, ScriptingComponent>)
			return ReplaceScriptingComponent(entity);
		else if constexpr (std::is_same_v<T, RigidBodyComponent>)
			return ReplaceRigidbodyComponent(entity);
		else
			return nullptr;
	}
	template<typename T>
	bool RemoveComponent(SceneEntityBase* entity)
	{
		if constexpr (std::is_same_v<T, GeneralComponent>)
			return RemoveGeneralComponent(entity);
		else if constexpr (std::is_same_v<T, TransformComponent>)
			return RemoveTransformComponent(entity);
		else if constexpr (std::is_same_v<T, MeshComponent>)
			return RemoveMeshComponent(entity);
		else if constexpr (std::is_same_v<T, CameraComponent>)
			return RemoveCameraComponent(entity);
		else if constexpr (std::is_same_v<T, PhysicsComponent>)
			return RemovePhysicsComponent(entity);
		else if constexpr (std::is_same_v<T, ScriptingComponent>)
			return RemoveScriptingComponent(entity);
		else if constexpr (std::is_same_v<T, RigidBodyComponent>)
			return RemoveRigidbodyComponent(entity);
		else
			return false;
	}
	template<typename T>
	T* GetComponent(SceneEntityBase* entity) const
	{
		if (entity == nullptr)
			return nullptr;

		ServicesContainer& services = entity->childrenContainer;
		constexpr const wchar_t* serviceName = ECSComponentServiceTraits<T>::Name;
		if (serviceName == nullptr)
			return nullptr;

		return services.FindServiceAs<T>(serviceName);
	}
	template<typename T>
	bool HasComponent(SceneEntityBase* entity) const
	{
		return GetComponent<T>(entity) != nullptr;
	}
	UINT Size() const;
	std::wstring GetEntityTypeLabel(SceneEntityBase* entity) const;
	std::wstring GetEntityName(SceneEntityBase* entity) const;
	std::wstring GetEntityTag(SceneEntityBase* entity) const;

	// 面向 Inspector 的聚合读取接口。
	bool BuildEntityComponentView(SceneEntityBase* entity, EntityComponentView* outView) const;
	bool BuildSelectedEntityComponentView(EntityComponentView* outView) const;
	bool HasInspectableComponents(SceneEntityBase* entity) const;
	bool RenameSelectedEntity(const std::wstring& newName);
	bool SetSelectedEntityTag(const std::wstring& newTag);
	bool SetSelectedEntityStatic(bool isStatic);
	bool SetSelectedEntityVisible(bool visible);
	bool GetSelectedEntityCameraFov(float* outFov) const;
	bool SetSelectedEntityCameraFov(float fov);
	bool GetSelectedEntityCameraNear(float* outNearZ) const;
	bool SetSelectedEntityCameraNear(float nearZ);
	bool GetSelectedEntityCameraFar(float* outFarZ) const;
	bool SetSelectedEntityCameraFar(float farZ);
	bool GetSelectedEntityCameraScale(float* outScale) const;
	bool SetSelectedEntityCameraScale(float scale);
	bool RestoreSelectedEntityCameraScale();
	bool GetSelectedEntityRigidBodySnapshot(EntityRigidBodyComponentData* outSnapshot) const;
	bool SetSelectedEntityRigidBodySnapshot(const EntityRigidBodyComponentData& snapshot);
	bool GetSelectedEntityScriptingSnapshot(EntityScriptingComponentData* outSnapshot) const;
	bool GetSelectedEntityScriptSnapshot(size_t index, EntityScriptingComponentData::ScriptSnapshot* outSnapshot) const;
	bool SetSelectedEntityScriptActive(size_t index, bool active);
	bool RemoveSelectedEntityScript(size_t index);
	bool GetSelectedEntityPhysicsColliderSnapshot(size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot) const;
	bool SetSelectedEntityPhysicsColliderSnapshot(size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot);
	bool AddRigidbodyToSelectedEntity();
	bool AddBoxColliderToSelectedEntity();
	bool AddScriptToSelectedEntity(const std::wstring& scriptPath);

	// 面向渲染侧的场景遍历接口。
	bool BuildEntityRenderView(SceneEntityBase* entity, EntityRenderView* outView) const;
	const std::vector<SceneEntityBase*>& GetSceneRootEntities() const;
	UINT GetSceneRootEntityCount() const;
	const std::vector<SceneEntityBase*>& GetSceneChildren(SceneEntityBase* entity) const;

	// 面向 Hierarchy 的层级遍历接口。
	const std::vector<SceneEntityBase*>& GetHierarchyRootEntities() const;
	UINT GetHierarchyRootEntityCount() const;
	const std::vector<SceneEntityBase*>& GetHierarchyChildren(SceneEntityBase* entity) const;
	UINT GetHierarchyChildCount(SceneEntityBase* entity) const;
	bool HasEntityChildren(SceneEntityBase* entity) const;
	bool IsSkyEntity(SceneEntityBase* entity) const;
	bool IsEntityStatic(SceneEntityBase* entity) const;
	bool IsEntityVisible(SceneEntityBase* entity) const;
	bool SetEntityVisible(SceneEntityBase* entity, bool visible);
	ComponentType GetEntityGeneralComponentType(SceneEntityBase* entity) const;
	bool SetEntityGeneralComponentType(SceneEntityBase* entity, ComponentType componentType);
	bool GetEntityCameraFov(SceneEntityBase* entity, float* outFov) const;
	bool SetEntityCameraFov(SceneEntityBase* entity, float fov);
	bool GetEntityCameraNear(SceneEntityBase* entity, float* outNearZ) const;
	bool SetEntityCameraNear(SceneEntityBase* entity, float nearZ);
	bool GetEntityCameraFar(SceneEntityBase* entity, float* outFarZ) const;
	bool SetEntityCameraFar(SceneEntityBase* entity, float farZ);
	bool GetEntityCameraScale(SceneEntityBase* entity, float* outScale) const;
	bool SetEntityCameraScale(SceneEntityBase* entity, float scale);
	bool RestoreEntityCameraScale(SceneEntityBase* entity);
	bool GetEntityLightSnapshot(SceneEntityBase* entity, EntityLightComponentData* outSnapshot) const;
	bool SetEntityLightSnapshot(SceneEntityBase* entity, const EntityLightComponentData& snapshot);
	bool GetEntityRigidBodySnapshot(SceneEntityBase* entity, EntityRigidBodyComponentData* outSnapshot) const;
	bool SetEntityRigidBodySnapshot(SceneEntityBase* entity, const EntityRigidBodyComponentData& snapshot);
	bool GetEntityScriptingSnapshot(SceneEntityBase* entity, EntityScriptingComponentData* outSnapshot) const;
	bool GetEntityScriptSnapshot(SceneEntityBase* entity, size_t index, EntityScriptingComponentData::ScriptSnapshot* outSnapshot) const;
	bool SetEntityScriptActive(SceneEntityBase* entity, size_t index, bool active);
	bool RemoveEntityScript(SceneEntityBase* entity, size_t index);
	bool GetEntityPhysicsColliderSnapshot(SceneEntityBase* entity, size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot) const;
	bool SetEntityPhysicsColliderSnapshot(SceneEntityBase* entity, size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot);
	bool AddRigidbodyToEntity(SceneEntityBase* entity);
	bool AddBoxColliderToEntity(SceneEntityBase* entity);
	bool AddScriptToEntity(SceneEntityBase* entity, const std::wstring& scriptPath);
	bool SetCameraEntityEngine(SceneEntityBase* entity, Engine* engine);
	bool ConfigureMeshEntity(SceneEntityBase* entity,
		Engine* engine,
		const std::wstring& displayName,
		const std::wstring& fileName,
		const std::wstring& meshName,
		UINT renderLayerIndex,
		const std::wstring& defaultMaterialName);
	bool SetMeshEntityExternalGeometry(SceneEntityBase* entity, const std::wstring& geometryName, AggregateGraphicObj* aggregateGraphicObj);
	bool AppendMeshEntityVertices(SceneEntityBase* entity, const std::vector<Vertex>& vertices);
	bool AppendMeshEntityIndices(SceneEntityBase* entity, const std::vector<std::uint32_t>& indices);
	bool SetupMeshEntity(SceneEntityBase* entity, D3DWindow* dx);
	bool SetMeshEntityMaterial(SceneEntityBase* entity, const std::wstring& materialName);
	bool RebuildMeshComponentOnEntity(SceneEntityBase* entity);
	bool RebuildCameraComponentOnEntity(SceneEntityBase* entity);
	bool RebuildPhysicsComponentOnEntity(SceneEntityBase* entity);
	bool RebuildScriptingComponentOnEntity(SceneEntityBase* entity);
	bool RebuildRigidbodyComponentOnEntity(SceneEntityBase* entity);
	bool RemoveMeshComponentFromEntity(SceneEntityBase* entity);
	bool RemoveCameraComponentFromEntity(SceneEntityBase* entity);
	bool RemovePhysicsComponentFromEntity(SceneEntityBase* entity);
	bool RemoveScriptingComponentFromEntity(SceneEntityBase* entity);
	bool RemoveRigidbodyComponentFromEntity(SceneEntityBase* entity);

	bool GetEntityLocalTransform(SceneEntityBase* entity, Transform* outTransform) const;
	bool GetEntityWorldTransform(SceneEntityBase* entity, Transform* outTransform) const;
	bool GetEntityWorldMatrix(SceneEntityBase* entity, DirectX::XMFLOAT4X4* outMatrix) const;

	bool GetEntityEditableLocalTransform(SceneEntityBase* entity, Transform* outTransform) const;
	bool SetEntityEditableLocalTransform(SceneEntityBase* entity, const Transform& transform);

	bool GetEntityRenderTransform(SceneEntityBase* entity, Transform* outTransform) const;
	bool GetEntityRenderMatrix(SceneEntityBase* entity, DirectX::XMFLOAT4X4* outMatrix) const;
	void SyncTransformsToFlecs();

	bool CanReparentEntityInHierarchy(SceneEntityBase* entity, SceneEntityBase* newParent) const;
	bool ReparentEntityInHierarchy(SceneEntityBase* entity, SceneEntityBase* newParent);
	bool SelectEntityForHierarchy(SceneEntityBase* entity);
	void ClearHierarchySelection();
	bool DeleteEntityFromHierarchy(SceneEntityBase* entity, bool destroyChildren);
	bool HasSelectedEntity() const;
	SceneEntityBase* GetSelectedEntity() const;

	void Update(float delta_time);
	void Clear();
	void End();

private:
	friend class WitchcraECSComponentLifecycleBridge;
	friend class WitchcraECSEntityHierarchyBridge;
	friend class WitchcraECSEntityHierarchyQueryBridge;
	friend class WitchcraECSInspectorBridge;
	friend class WitchcraECSPhysicsBridge;
	friend class WitchcraECSRigidBodyBridge;
	friend class WitchcraECSScriptingBridge;
	friend class WitchcraECSTransformSyncBridge;

	void CreateEntity(std::wstring name, SceneEntityBase* Entity, SceneEntityBase* parent);
	SceneEntityBase* GetEntity(std::wstring name);
	SceneEntityBase* GetEntity(UINT index);
	const std::vector<SceneEntityBase*>& GetEntityChildren(SceneEntityBase* entity) const;
	UINT GetEntityChildCount(SceneEntityBase* entity) const;
	static DirectX::XMMATRIX TransformToMatrix(const Transform& transform);
	static DirectX::XMFLOAT4X4 IdentityMatrix4x4();
	static DirectX::XMFLOAT4X4 ComposeWorldMatrix(const Transform& localTransform, const DirectX::XMFLOAT4X4& parentWorldMatrix);
	static Transform DecomposeWorldTransform(const DirectX::XMFLOAT4X4& worldMatrix, const Transform& sourceTransform);
	static ServicesContainer& GetEntityServices(SceneEntityBase* entity);
	static EntityPhysicsComponentData BuildPhysicsComponentData(PhysicsComponent& component);
	static EntityRigidBodyComponentData BuildRigidBodyComponentData(RigidBodyComponent& component);
	void SyncGeneralComponentToFlecs(SceneEntityBase* entity);
	void SyncCameraComponentToFlecs(SceneEntityBase* entity);
	void SyncLightComponentToFlecs(SceneEntityBase* entity);
	void SyncPhysicsComponentToFlecs(SceneEntityBase* entity);
	void SyncRigidBodyComponentToFlecs(SceneEntityBase* entity);
	void SyncScriptingComponentToFlecs(SceneEntityBase* entity);
	void RemoveGeneralComponentDataFromFlecs(SceneEntityBase* entity);
	void RemoveCameraComponentDataFromFlecs(SceneEntityBase* entity);
	void RemoveLightComponentDataFromFlecs(SceneEntityBase* entity);
	void RemovePhysicsComponentDataFromFlecs(SceneEntityBase* entity);
	void RemoveRigidBodyComponentDataFromFlecs(SceneEntityBase* entity);
	void RemoveScriptingComponentDataFromFlecs(SceneEntityBase* entity);

	template<typename TComponent, typename Initializer>
	TComponent* AddManagedComponent(SceneEntityBase* entity, Initializer&& initializer)
	{
		if (entity == nullptr)
			return nullptr;

		ServicesContainer& services = GetEntityServices(entity);
		constexpr const wchar_t* serviceName = ECSComponentServiceTraits<TComponent>::Name;
		static_assert(ECSComponentServiceTraits<TComponent>::Name != nullptr,
			"TComponent 缺少 ECSComponentServiceTraits 特化。");

		if (TComponent* existingComponent = services.FindServiceAs<TComponent>(serviceName))
			return existingComponent;

		auto* component = new TComponent();
		initializer(component);
		if (!services.AddService(serviceName, component))
		{
			delete component;
			return services.FindServiceAs<TComponent>(serviceName);
		}

		RefreshEntityTypeTags(entity);
		return component;
	}

	template<typename TComponent, typename BeforeDestroy>
	bool RemoveManagedComponent(SceneEntityBase* entity, BeforeDestroy&& beforeDestroy)
	{
		if (entity == nullptr)
			return false;

		ServicesContainer& services = GetEntityServices(entity);
		constexpr const wchar_t* serviceName = ECSComponentServiceTraits<TComponent>::Name;
		static_assert(ECSComponentServiceTraits<TComponent>::Name != nullptr,
			"TComponent 缺少 ECSComponentServiceTraits 特化。");

		TComponent* component = services.FindServiceAs<TComponent>(serviceName);
		if (component == nullptr)
			return false;

		beforeDestroy(component);
		component->Destroy();
		delete component;
		services.RemoveService(serviceName);
		RefreshEntityTypeTags(entity);
		return true;
	}

	template<typename TComponent>
	TComponent* ReplaceManagedComponent(SceneEntityBase* entity)
	{
		RemoveComponent<TComponent>(entity);
		return AddComponent<TComponent>(entity);
	}

	void RegisterTransformSystems();
	void RegisterEntitySubtreeIndices(SceneEntityBase* entity, SceneEntityBase* parent);
	void UnregisterEntitySubtreeIndices(SceneEntityBase* entity);
	void AddEntityNameIndexEntry(SceneEntityBase* entity);
	void RemoveEntityNameIndexEntry(const std::wstring& name, SceneEntityBase* entity);
	void UpdateEntityParentIndexEntry(SceneEntityBase* entity, SceneEntityBase* parent);
	void RemoveEntityParentIndexEntry(SceneEntityBase* entity);
	void RemoveEntityIndexEntry(SceneEntityBase* entity);
	bool ContainsEntity(SceneEntityBase* root, SceneEntityBase* target) const;
	void DeleteEntityTree(SceneEntityBase* entity);
	void SyncTransformComponentCacheFromFlecs();
	void SyncTransformSubtree(SceneEntityBase* entity, const DirectX::XMFLOAT4X4& parentWorldMatrix);
	void InitializeEntityTransformState(SceneEntityBase* entity, const Transform& localTransform = Transform{});
	void MarkTransformDirty(SceneEntityBase* entity);
	void MarkAllTransformsDirty();
	SceneEntityBase* CreateEntityWithDefaultComponents(
		const std::wstring& name,
		SceneEntityBase* parent,
		ComponentType componentType,
		bool addMeshComponent,
		bool addCameraComponent);
	void RemoveRootEntityPointer(SceneEntityBase* entity);
	void FinalizeEntityHierarchyChange();
	void MoveChildrenUpOneLevel(SceneEntityBase* entity, SceneEntityBase* parent);
	void DestroyEntityComponents(SceneEntityBase* entity);
	void DestroyMeshComponentInstance(MeshComponent* component);
	void DestroyEntitySubtreeComponents(SceneEntityBase* entity);
	void DestroyEntitySubtree(SceneEntityBase* target, SceneEntityBase* parent);
	void RemoveEntityPreserveChildren(SceneEntityBase* target, SceneEntityBase* parent);

	void RefreshEntityTypeTags(SceneEntityBase* entity);
	void ClearEntityTypeTags(flecs::entity_t entityId);

private:
	flecs::world entityWorld;
	std::vector<SceneEntityBase*> entities;
	SceneEntityBase* selectedEntity = nullptr;
	ecs_entity_t mUnknownEntityTypeTagId = 0;
	ecs_entity_t mCameraEntityTypeTagId = 0;
	ecs_entity_t mMeshEntityTypeTagId = 0;
	ecs_entity_t mLightEntityTypeTagId = 0;
	ecs_entity_t mLocalTransformComponentId = 0;
	ecs_entity_t mWorldTransformComponentId = 0;
	ecs_entity_t mWorldMatrixComponentId = 0;
	ecs_entity_t mGeneralComponentDataId = 0;
	ecs_entity_t mCameraComponentDataId = 0;
	ecs_entity_t mLightComponentDataId = 0;
	ecs_entity_t mPhysicsComponentDataId = 0;
	ecs_entity_t mRigidBodyComponentDataId = 0;
	ecs_entity_t mScriptingComponentDataId = 0;
	std::unordered_map<std::wstring, std::vector<SceneEntityBase*>> mEntityNameIndex;
	std::unordered_map<SceneEntityBase*, SceneEntityBase*> mParentEntityIndex;
	bool mTransformsDirty = true;
	bool mTransformsDirtyFull = true;
	std::vector<SceneEntityBase*> mDirtyTransformRoots;
};
