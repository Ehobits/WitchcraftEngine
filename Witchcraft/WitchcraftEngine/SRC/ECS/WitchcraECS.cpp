#include "WitchcraECS.h"
#include "WitchcraECSEntityHierarchyBridge.h"
#include "WitchcraECSEntityHierarchyQueryBridge.h"
#include "WitchcraECSComponentLifecycleBridge.h"
#include "WitchcraECSInspectorBridge.h"
#include "WitchcraECSPhysicsBridge.h"
#include "WitchcraECSRigidBodyBridge.h"
#include "WitchcraECSScriptingBridge.h"
#include "WitchcraECSTransformSyncBridge.h"
#include "Engine/Engine.h"
#include "D3DWindow/D3DWindow.h"
#include "String/SStringUtils.h"
#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/LightComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
#include "ECS/COMPONENT/ScriptingComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"

#include <algorithm>
#include <functional>

// 把编辑器使用的 Transform 转成运行时矩阵。
DirectX::XMMATRIX WitchcraECS::TransformToMatrix(const Transform& transform)
{
	return WitchcraECSTransformSyncBridge::TransformToMatrix(transform);
}

// 返回单位矩阵的拷贝，避免多处直接依赖外部静态对象。
DirectX::XMFLOAT4X4 WitchcraECS::IdentityMatrix4x4()
{
	return WitchcraECSTransformSyncBridge::IdentityMatrix4x4();
}

// 统一获取实体内部的服务容器。
ServicesContainer& WitchcraECS::GetEntityServices(SceneEntityBase* entity)
{
	return entity->childrenContainer;
}

EntityPhysicsComponentData WitchcraECS::BuildPhysicsComponentData(PhysicsComponent& component)
{
	return WitchcraECSPhysicsBridge::BuildComponentData(component);
}

EntityRigidBodyComponentData WitchcraECS::BuildRigidBodyComponentData(RigidBodyComponent& component)
{
	return WitchcraECSRigidBodyBridge::BuildComponentData(component);
}

void WitchcraECS::SyncGeneralComponentToFlecs(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || mGeneralComponentDataId == 0)
		return;

	GeneralComponent* component = GetComponent<GeneralComponent>(entity);
	if (component == nullptr)
	{
		RemoveGeneralComponentDataFromFlecs(entity);
		return;
	}

	component->BindEntity(this, entity);

	const EntityGeneralComponentData* currentGeneralData = static_cast<const EntityGeneralComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mGeneralComponentDataId));
	if (currentGeneralData == nullptr)
		entityWorld.entity(entity->entity).set<EntityGeneralComponentData>(EntityGeneralComponentData{});
}

void WitchcraECS::SyncCameraComponentToFlecs(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return;

	CameraComponent* component = GetComponent<CameraComponent>(entity);
	if (component == nullptr)
	{
		RemoveCameraComponentDataFromFlecs(entity);
		return;
	}

	component->BindEntity(this, entity);

	EntityCameraComponentData cameraData{};
	const EntityCameraComponentData* currentCameraData = static_cast<const EntityCameraComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mCameraComponentDataId));
	if (currentCameraData == nullptr)
		entityWorld.entity(entity->entity).set<EntityCameraComponentData>(cameraData);
}

void WitchcraECS::SyncLightComponentToFlecs(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || mLightComponentDataId == 0)
		return;

	LightComponent* component = GetComponent<LightComponent>(entity);
	if (component == nullptr)
	{
		RemoveLightComponentDataFromFlecs(entity);
		return;
	}

	component->BindEntity(this, entity);

	EntityLightComponentData lightData;
	if (!GetEntityLightSnapshot(entity, &lightData))
		entityWorld.entity(entity->entity).set<EntityLightComponentData>(EntityLightComponentData{});
}

void WitchcraECS::SyncPhysicsComponentToFlecs(SceneEntityBase* entity)
{
	WitchcraECSPhysicsBridge::SyncComponentToFlecs(*this, entity);
}

void WitchcraECS::SyncRigidBodyComponentToFlecs(SceneEntityBase* entity)
{
	WitchcraECSRigidBodyBridge::SyncComponentToFlecs(*this, entity);
}

void WitchcraECS::SyncScriptingComponentToFlecs(SceneEntityBase* entity)
{
	WitchcraECSScriptingBridge::SyncComponentToFlecs(*this, entity);
}

void WitchcraECS::RemoveGeneralComponentDataFromFlecs(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || mGeneralComponentDataId == 0)
		return;

	ecs_remove_id(entityWorld, entity->entity, mGeneralComponentDataId);
}

void WitchcraECS::RemoveCameraComponentDataFromFlecs(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return;

	ecs_remove_id(entityWorld, entity->entity, mCameraComponentDataId);
}

void WitchcraECS::RemoveLightComponentDataFromFlecs(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0 || mLightComponentDataId == 0)
		return;

	ecs_remove_id(entityWorld, entity->entity, mLightComponentDataId);
}

void WitchcraECS::RemovePhysicsComponentDataFromFlecs(SceneEntityBase* entity)
{
	WitchcraECSPhysicsBridge::RemoveComponentDataFromFlecs(*this, entity);
}

void WitchcraECS::RemoveRigidBodyComponentDataFromFlecs(SceneEntityBase* entity)
{
	WitchcraECSRigidBodyBridge::RemoveComponentDataFromFlecs(*this, entity);
}

void WitchcraECS::RemoveScriptingComponentDataFromFlecs(SceneEntityBase* entity)
{
	WitchcraECSScriptingBridge::RemoveComponentDataFromFlecs(*this, entity);
}

// 组合父子矩阵，得到最终 world matrix。
DirectX::XMFLOAT4X4 WitchcraECS::ComposeWorldMatrix(
	const Transform& localTransform,
	const DirectX::XMFLOAT4X4& parentWorldMatrix)
{
	return WitchcraECSTransformSyncBridge::ComposeWorldMatrix(localTransform, parentWorldMatrix);
}

Transform WitchcraECS::DecomposeWorldTransform(const DirectX::XMFLOAT4X4& worldMatrix, const Transform& sourceTransform)
{
	return WitchcraECSTransformSyncBridge::DecomposeWorldTransform(worldMatrix, sourceTransform);
}

// ---------------------------------------------------------------------------
// SceneEntityBase：这里只保留最基础的实体树与元数据存取。
// ---------------------------------------------------------------------------

void SceneEntityBase::SetName(std::wstring name)
{
	if (!name.compare(L""))
		return;

	childrenContainer.ReName(name);
	nameEntity = name;
}

void SceneEntityBase::SetTag(std::wstring tag)
{
	if (!tag.compare(L""))
		return;
	tagEntity = tag;
}

void SceneEntityBase::SetStatic(bool arg)
{
	staticEntity = arg;
}

void SceneEntityBase::AddChild(std::wstring name, SceneEntityBase* Entity)
{
	if (Entity == nullptr)
		return;

	childrenEntity.resize(childrenEntity.size() + 1);
	childrenEntity[childrenEntity.size() - 1] = Entity;
	childrenEntity[childrenEntity.size() - 1]->SetName(name);
}

bool SceneEntityBase::RemoveChild(SceneEntityBase* Entity)
{
	for (auto it = childrenEntity.begin(); it != childrenEntity.end(); ++it)
	{
		if (*it == Entity)
		{
			childrenEntity.erase(it);
			return true;
		}
	}

	return false;
}

std::vector<SceneEntityBase*> SceneEntityBase::ReleaseChildren()
{
	std::vector<SceneEntityBase*> children;
	children.swap(childrenEntity);
	return children;
}

const std::vector<SceneEntityBase*>& SceneEntityBase::GetChildrenEntity() const
{
	return childrenEntity;
}

std::wstring SceneEntityBase::GetName()
{
	return nameEntity;
}

std::wstring SceneEntityBase::GetTag()
{
	return tagEntity;
}

bool SceneEntityBase::IsStatic()
{
	return staticEntity;
}


WitchcraECS::WitchcraECS()
{
}

WitchcraECS::~WitchcraECS()
{
}

// 初始化 flecs 组件/标签，并注册 ECS 内部需要的同步流程。
bool WitchcraECS::Init()
{
	mUnknownEntityTypeTagId = entityWorld.component<EntityUnknownTag>().id();
	mCameraEntityTypeTagId = entityWorld.component<EntityCameraTag>().id();
	mMeshEntityTypeTagId = entityWorld.component<EntityMeshTag>().id();
	mLightEntityTypeTagId = entityWorld.component<EntityLightTag>().id();
	mLocalTransformComponentId = entityWorld.component<EntityLocalTransform>().id();
	mWorldTransformComponentId = entityWorld.component<EntityWorldTransform>().id();
	mWorldMatrixComponentId = entityWorld.component<EntityWorldMatrix>().id();
	mGeneralComponentDataId = entityWorld.component<EntityGeneralComponentData>().id();
	mCameraComponentDataId = entityWorld.component<EntityCameraComponentData>().id();
	mLightComponentDataId = entityWorld.component<EntityLightComponentData>().id();
	mPhysicsComponentDataId = entityWorld.component<EntityPhysicsComponentData>().id();
	mRigidBodyComponentDataId = entityWorld.component<EntityRigidBodyComponentData>().id();
	mScriptingComponentDataId = entityWorld.component<EntityScriptingComponentData>().id();

	entityWorld.system<ProjectSceneSystem, const ProjectSceneSystem>()
		.each([](ProjectSceneSystem& scene, const ProjectSceneSystem& source)
		{
			scene = source;
		});

	// 这里不能对匿名 lambda 闭包类型调用 set，
	// 否则 flecs 会把空闭包类型误当成组件类型并在启动时断言。

	RegisterTransformSystems();
	return true;
}

void WitchcraECS::RegisterTransformSystems()
{
	// 这里先保留为注册入口。
	// 当前 world/local transform 仍采用按需同步，
	// 避免在 system 中递归整条父链造成重复计算。
}

// 把新挂接/导入的一棵实体子树登记到索引表中。
void WitchcraECS::RegisterEntitySubtreeIndices(SceneEntityBase* entity, SceneEntityBase* parent)
{
	WitchcraECSEntityHierarchyBridge::RegisterEntitySubtreeIndices(*this, entity, parent);
}

void WitchcraECS::UnregisterEntitySubtreeIndices(SceneEntityBase* entity)
{
	WitchcraECSEntityHierarchyBridge::UnregisterEntitySubtreeIndices(*this, entity);
}

void WitchcraECS::AddEntityNameIndexEntry(SceneEntityBase* entity)
{
	WitchcraECSEntityHierarchyBridge::AddEntityNameIndexEntry(*this, entity);
}

void WitchcraECS::RemoveEntityNameIndexEntry(const std::wstring& name, SceneEntityBase* entity)
{
	WitchcraECSEntityHierarchyBridge::RemoveEntityNameIndexEntry(*this, name, entity);
}

void WitchcraECS::UpdateEntityParentIndexEntry(SceneEntityBase* entity, SceneEntityBase* parent)
{
	WitchcraECSEntityHierarchyBridge::UpdateEntityParentIndexEntry(*this, entity, parent);
}

void WitchcraECS::RemoveEntityParentIndexEntry(SceneEntityBase* entity)
{
	WitchcraECSEntityHierarchyBridge::RemoveEntityParentIndexEntry(*this, entity);
}

void WitchcraECS::RemoveEntityIndexEntry(SceneEntityBase* entity)
{
	WitchcraECSEntityHierarchyBridge::RemoveEntityIndexEntry(*this, entity);
}

// 判断 target 是否位于 root 子树中。
bool WitchcraECS::ContainsEntity(SceneEntityBase* root, SceneEntityBase* target) const
{
	return WitchcraECSEntityHierarchyBridge::ContainsEntity(*this, root, target);
}

// 递归销毁实体树节点本身；组件资源应在外部先释放完成。
void WitchcraECS::DeleteEntityTree(SceneEntityBase* entity)
{
	WitchcraECSEntityHierarchyBridge::DeleteEntityTree(*this, entity);
}

// 标记整棵场景的变换缓存需要重算。
void WitchcraECS::MarkAllTransformsDirty()
{
	WitchcraECSTransformSyncBridge::MarkAllTransformsDirty(*this);
}

// 标记某个子树的变换缓存需要重算。
void WitchcraECS::MarkTransformDirty(SceneEntityBase* entity)
{
	WitchcraECSTransformSyncBridge::MarkTransformDirty(*this, entity);
}

// 真正执行实体树挂接，并初始化 flecs 侧状态。
void WitchcraECS::CreateEntity(std::wstring name, SceneEntityBase* Entity, SceneEntityBase* parent)
{
	WitchcraECSEntityHierarchyBridge::CreateEntity(*this, std::move(name), Entity, parent);
	SyncLightComponentToFlecs(Entity);
	SyncTransformsToFlecs();
}

// 创建只有基础组件的普通实体。
SceneEntityBase* WitchcraECS::CreateBasicEntity(const std::wstring& name, SceneEntityBase* parent, ComponentType componentType)
{
	return CreateEntityWithDefaultComponents(name, parent, componentType, false, false);
}

// 创建带 MeshComponent 的实体。
SceneEntityBase* WitchcraECS::CreateMeshEntity(const std::wstring& name, SceneEntityBase* parent)
{
	return CreateEntityWithDefaultComponents(name, parent, ComponentType::Co_Mesh, true, false);
}

// 创建带 CameraComponent 的实体。
SceneEntityBase* WitchcraECS::CreateCameraEntity(const std::wstring& name, SceneEntityBase* parent)
{
	return CreateEntityWithDefaultComponents(name, parent, ComponentType::Co_Camera, false, true);
}

SceneEntityBase* WitchcraECS::CreateLightEntity(const std::wstring& name, SceneEntityBase* parent)
{
	SceneEntityBase* entity = CreateBasicEntity(name, parent, ComponentType::Co_Unk);
	if (entity == nullptr)
		return nullptr;

	AddLightComponent(entity);
	return entity;
}

// 为新实体补齐默认组件并挂到 ECS 中。
SceneEntityBase* WitchcraECS::CreateEntityWithDefaultComponents(
	const std::wstring& name,
	SceneEntityBase* parent,
	ComponentType componentType,
	bool addMeshComponent,
	bool addCameraComponent)
{
	SceneEntityBase* entity = new SceneEntityBase();
	entity->SetName(name);

	(void)AddGeneralComponent(entity);

	AddTransformComponent(entity);
	if (addMeshComponent)
		AddMeshComponent(entity);
	if (addCameraComponent)
		AddCameraComponent(entity);
	CreateEntity(name, entity, parent);
	SetEntityGeneralComponentType(entity, componentType);
	return entity;
}

// 在 flecs 中初始化 local/world transform 的基础数据。
void WitchcraECS::InitializeEntityTransformState(SceneEntityBase* entity, const Transform& localTransform)
{
	WitchcraECSTransformSyncBridge::InitializeEntityTransformState(*this, entity, localTransform);
}

// 对外统一的实体删除入口。
bool WitchcraECS::DestroyEntity(SceneEntityBase* target, bool destroyChildren)
{
	return WitchcraECSEntityHierarchyBridge::DestroyEntity(*this, target, destroyChildren);
}

// 从根实体列表中移除一个实体指针。
void WitchcraECS::RemoveRootEntityPointer(SceneEntityBase* entity)
{
	WitchcraECSEntityHierarchyBridge::RemoveRootEntityPointer(*this, entity);
}

// 层级变化后统一刷新索引和 transform dirty 状态。
void WitchcraECS::FinalizeEntityHierarchyChange()
{
	WitchcraECSEntityHierarchyBridge::FinalizeEntityHierarchyChange(*this);
}

// 删除父实体但保留子实体时，把子实体提升到上一层。
void WitchcraECS::MoveChildrenUpOneLevel(SceneEntityBase* entity, SceneEntityBase* parent)
{
	WitchcraECSEntityHierarchyBridge::MoveChildrenUpOneLevel(*this, entity, parent);
}

// 销毁一个实体拥有的全部组件对象。
void WitchcraECS::DestroyEntityComponents(SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	ServicesContainer& services = GetEntityServices(entity);
	for (const auto& servicePair : services.GetAllServices())
	{
		BaseComponent* component = static_cast<BaseComponent*>(servicePair.second);
		if (component == nullptr)
			continue;

		if (servicePair.first == ECSComponentServiceTraits<MeshComponent>::Name)
			DestroyMeshComponentInstance(static_cast<MeshComponent*>(component));
		else
			component->Destroy();

		delete component;
	}
	services.RemoveAll();
}

// MeshComponent 析构前需要先释放其持有的运行时 D3D 资源。
void WitchcraECS::DestroyMeshComponentInstance(MeshComponent* component)
{
	if (component == nullptr)
		return;

	component->ReleaseRuntimeResources();
	component->ClearCache();
	static_cast<BaseComponent*>(component)->Destroy();
}

// 递归销毁一棵子树上的全部组件。
void WitchcraECS::DestroyEntitySubtreeComponents(SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	for (SceneEntityBase* child : GetEntityChildren(entity))
		DestroyEntitySubtreeComponents(child);

	DestroyEntityComponents(entity);
}

// 完整销毁一个实体子树。
void WitchcraECS::DestroyEntitySubtree(SceneEntityBase* target, SceneEntityBase* parent)
{
	WitchcraECSEntityHierarchyBridge::DestroyEntitySubtree(*this, target, parent);
}

// 删除当前实体，但保留并上移它的子实体。
void WitchcraECS::RemoveEntityPreserveChildren(SceneEntityBase* target, SceneEntityBase* parent)
{
	WitchcraECSEntityHierarchyBridge::RemoveEntityPreserveChildren(*this, target, parent);
}

// 改名时按同级作用域检查重名。
bool WitchcraECS::RenameEntity(SceneEntityBase* entity, const std::wstring& newName)
{
	return WitchcraECSEntityHierarchyBridge::RenameEntity(*this, entity, newName);
}

bool WitchcraECS::SetEntityTag(SceneEntityBase* entity, const std::wstring& newTag)
{
	if (entity == nullptr || newTag.empty())
		return false;

	entity->SetTag(newTag);
	return true;
}

// 设置实体的静态标记；当前主要供编辑器读写。
bool WitchcraECS::SetEntityStatic(SceneEntityBase* entity, bool isStatic)
{
	if (entity == nullptr)
		return false;

	entity->SetStatic(isStatic);
	return true;
}

// ---------------------------------------------------------------------------
// 组件的创建 / 替换 / 移除。
// ---------------------------------------------------------------------------

GeneralComponent* WitchcraECS::AddGeneralComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddGeneralComponent(*this, entity);
}

// 添加 TransformComponent 时，顺手把 flecs 中已有的变换缓存同步进去。
TransformComponent* WitchcraECS::AddTransformComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddTransformComponent(*this, entity);
}

MeshComponent* WitchcraECS::AddMeshComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddMeshComponent(*this, entity);
}

CameraComponent* WitchcraECS::AddCameraComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddCameraComponent(*this, entity);
}

LightComponent* WitchcraECS::AddLightComponent(SceneEntityBase* entity)
{
	LightComponent* lightComponent = AddManagedComponent<LightComponent>(entity,
		[](LightComponent* newComponent)
		{
			newComponent->SetName(L"LightComponent");
		});

	if (lightComponent != nullptr)
	{
		lightComponent->BindEntity(this, entity);
		SyncLightComponentToFlecs(entity);
	}

	return lightComponent;
}

PhysicsComponent* WitchcraECS::AddPhysicsComponent(SceneEntityBase* entity)
{
	return WitchcraECSPhysicsBridge::AddComponent(*this, entity);
}

ScriptingComponent* WitchcraECS::AddScriptingComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddScriptingComponent(*this, entity);
}

RigidBodyComponent* WitchcraECS::AddRigidbodyComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddRigidbodyComponent(*this, entity);
}

GeneralComponent* WitchcraECS::ReplaceGeneralComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceGeneralComponent(*this, entity);
}

TransformComponent* WitchcraECS::ReplaceTransformComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceTransformComponent(*this, entity);
}

// MeshComponent 替换时，需要额外恢复旧网格的 CPU/GPU 状态。
MeshComponent* WitchcraECS::ReplaceMeshComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceMeshComponent(*this, entity);
}

CameraComponent* WitchcraECS::ReplaceCameraComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceCameraComponent(*this, entity);
}

PhysicsComponent* WitchcraECS::ReplacePhysicsComponent(SceneEntityBase* entity)
{
	return WitchcraECSPhysicsBridge::ReplaceComponent(*this, entity);
}

ScriptingComponent* WitchcraECS::ReplaceScriptingComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceScriptingComponent(*this, entity);
}

RigidBodyComponent* WitchcraECS::ReplaceRigidbodyComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceRigidbodyComponent(*this, entity);
}

bool WitchcraECS::RemoveGeneralComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveGeneralComponent(*this, entity);
}

bool WitchcraECS::RemoveTransformComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveTransformComponent(*this, entity);
}

// MeshComponent 移除时要先释放运行时资源，再删除组件对象。
bool WitchcraECS::RemoveMeshComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveMeshComponent(*this, entity);
}

bool WitchcraECS::RemoveCameraComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveCameraComponent(*this, entity);
}

bool WitchcraECS::RemovePhysicsComponent(SceneEntityBase* entity)
{
	return WitchcraECSPhysicsBridge::RemoveComponent(*this, entity);
}

bool WitchcraECS::RemoveScriptingComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveScriptingComponent(*this, entity);
}

bool WitchcraECS::RemoveRigidbodyComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveRigidbodyComponent(*this, entity);
}

// ---------------------------------------------------------------------------
// 实体查询与编辑器语义接口。
// ---------------------------------------------------------------------------

SceneEntityBase* WitchcraECS::GetEntity(std::wstring name)
{
	auto it = mEntityNameIndex.find(name);
	if (it == mEntityNameIndex.end() || it->second.empty())
		return nullptr;

	return it->second.front();
}

SceneEntityBase* WitchcraECS::GetEntity(UINT index)
{
	if (index >= entities.size())
		return nullptr;

	return entities[index];
}

bool WitchcraECS::HasEntity(SceneEntityBase* entity) const
{
	if (entity == nullptr)
		return false;

	return mParentEntityIndex.find(entity) != mParentEntityIndex.end();
}

SceneEntityBase* WitchcraECS::GetParentEntity(SceneEntityBase* entity) const
{
	return WitchcraECSEntityHierarchyQueryBridge::GetParentEntity(*this, entity);
}

bool WitchcraECS::IsRootEntity(SceneEntityBase* entity) const
{
	return WitchcraECSEntityHierarchyQueryBridge::IsRootEntity(*this, entity);
}

bool WitchcraECS::IsEntityNameAvailable(const std::wstring& candidate, SceneEntityBase* parent, SceneEntityBase* ignoreEntity) const
{
	return WitchcraECSEntityHierarchyQueryBridge::IsEntityNameAvailable(*this, candidate, parent, ignoreEntity);
}

std::wstring WitchcraECS::GetUniqueEntityName(const std::wstring& desiredName, SceneEntityBase* parent) const
{
	return WitchcraECSEntityHierarchyQueryBridge::GetUniqueEntityName(*this, desiredName, parent);
}

UINT WitchcraECS::Size() const
{
	return static_cast<UINT>(entities.size());
}

// 统一返回编辑器使用的实体类型文本。
std::wstring WitchcraECS::GetEntityTypeLabel(SceneEntityBase* entity) const
{
	return WitchcraECSInspectorBridge::GetEntityTypeLabel(*this, entity);
}

std::wstring WitchcraECS::GetEntityName(SceneEntityBase* entity) const
{
	return entity != nullptr ? entity->GetName() : L"";
}

std::wstring WitchcraECS::GetEntityTag(SceneEntityBase* entity) const
{
	return entity != nullptr ? entity->GetTag() : L"";
}

// 把 Inspector 需要的一组组件视图数据集中打包出来。
bool WitchcraECS::BuildEntityComponentView(SceneEntityBase* entity, EntityComponentView* outView) const
{
	return WitchcraECSInspectorBridge::BuildEntityComponentView(*this, entity, outView);
}

bool WitchcraECS::BuildSelectedEntityComponentView(EntityComponentView* outView) const
{
	return WitchcraECSInspectorBridge::BuildSelectedEntityComponentView(*this, outView);
}

// 判断一个实体是否至少拥有一个可在 Inspector 中展示的组件。
bool WitchcraECS::HasInspectableComponents(SceneEntityBase* entity) const
{
	return WitchcraECSInspectorBridge::HasInspectableComponents(*this, entity);
}

bool WitchcraECS::RenameSelectedEntity(const std::wstring& newName)
{
	return WitchcraECSInspectorBridge::RenameSelectedEntity(*this, newName);
}

bool WitchcraECS::SetSelectedEntityTag(const std::wstring& newTag)
{
	return WitchcraECSInspectorBridge::SetSelectedEntityTag(*this, newTag);
}

bool WitchcraECS::SetSelectedEntityStatic(bool isStatic)
{
	return WitchcraECSInspectorBridge::SetSelectedEntityStatic(*this, isStatic);
}

bool WitchcraECS::SetSelectedEntityVisible(bool visible)
{
	return WitchcraECSInspectorBridge::SetSelectedEntityVisible(*this, visible);
}

bool WitchcraECS::GetSelectedEntityCameraFov(float* outFov) const
{
	return WitchcraECSInspectorBridge::GetSelectedEntityCameraFov(*this, outFov);
}

bool WitchcraECS::SetSelectedEntityCameraFov(float fov)
{
	return WitchcraECSInspectorBridge::SetSelectedEntityCameraFov(*this, fov);
}

bool WitchcraECS::GetSelectedEntityCameraNear(float* outNearZ) const
{
	return WitchcraECSInspectorBridge::GetSelectedEntityCameraNear(*this, outNearZ);
}

bool WitchcraECS::SetSelectedEntityCameraNear(float nearZ)
{
	return WitchcraECSInspectorBridge::SetSelectedEntityCameraNear(*this, nearZ);
}

bool WitchcraECS::GetSelectedEntityCameraFar(float* outFarZ) const
{
	return WitchcraECSInspectorBridge::GetSelectedEntityCameraFar(*this, outFarZ);
}

bool WitchcraECS::SetSelectedEntityCameraFar(float farZ)
{
	return WitchcraECSInspectorBridge::SetSelectedEntityCameraFar(*this, farZ);
}

bool WitchcraECS::GetSelectedEntityCameraScale(float* outScale) const
{
	return WitchcraECSInspectorBridge::GetSelectedEntityCameraScale(*this, outScale);
}

bool WitchcraECS::SetSelectedEntityCameraScale(float scale)
{
	return WitchcraECSInspectorBridge::SetSelectedEntityCameraScale(*this, scale);
}

bool WitchcraECS::RestoreSelectedEntityCameraScale()
{
	return WitchcraECSInspectorBridge::RestoreSelectedEntityCameraScale(*this);
}

bool WitchcraECS::GetSelectedEntityRigidBodySnapshot(EntityRigidBodyComponentData* outSnapshot) const
{
	return WitchcraECSInspectorBridge::GetSelectedEntityRigidBodySnapshot(*this, outSnapshot);
}

bool WitchcraECS::SetSelectedEntityRigidBodySnapshot(const EntityRigidBodyComponentData& snapshot)
{
	return WitchcraECSInspectorBridge::SetSelectedEntityRigidBodySnapshot(*this, snapshot);
}

bool WitchcraECS::GetSelectedEntityScriptingSnapshot(EntityScriptingComponentData* outSnapshot) const
{
	return WitchcraECSInspectorBridge::GetSelectedEntityScriptingSnapshot(*this, outSnapshot);
}

bool WitchcraECS::GetSelectedEntityScriptSnapshot(size_t index, EntityScriptingComponentData::ScriptSnapshot* outSnapshot) const
{
	return WitchcraECSInspectorBridge::GetSelectedEntityScriptSnapshot(*this, index, outSnapshot);
}

bool WitchcraECS::SetSelectedEntityScriptActive(size_t index, bool active)
{
	return WitchcraECSInspectorBridge::SetSelectedEntityScriptActive(*this, index, active);
}

bool WitchcraECS::RemoveSelectedEntityScript(size_t index)
{
	return WitchcraECSInspectorBridge::RemoveSelectedEntityScript(*this, index);
}

bool WitchcraECS::GetSelectedEntityPhysicsColliderSnapshot(size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot) const
{
	return WitchcraECSPhysicsBridge::GetSelectedEntityColliderSnapshot(*this, index, outSnapshot);
}

bool WitchcraECS::SetSelectedEntityPhysicsColliderSnapshot(size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot)
{
	return WitchcraECSPhysicsBridge::SetSelectedEntityColliderSnapshot(*this, index, snapshot);
}

bool WitchcraECS::AddRigidbodyToSelectedEntity()
{
	return WitchcraECSInspectorBridge::AddRigidbodyToSelectedEntity(*this);
}

bool WitchcraECS::AddBoxColliderToSelectedEntity()
{
	return WitchcraECSPhysicsBridge::AddBoxColliderToSelectedEntity(*this);
}

bool WitchcraECS::AddScriptToSelectedEntity(const std::wstring& scriptPath)
{
	return WitchcraECSInspectorBridge::AddScriptToSelectedEntity(*this, scriptPath);
}

bool WitchcraECS::BuildEntityRenderView(SceneEntityBase* entity, EntityRenderView* outView) const
{
	if (outView == nullptr)
		return false;

	*outView = EntityRenderView{};
	if (entity == nullptr || !HasEntity(entity))
		return false;

	MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	if (meshComponent == nullptr)
		return false;

	outView->entity = entity;
	outView->meshComponent = meshComponent;
	outView->renderItemName = meshComponent->GetMeshName();
	if (outView->renderItemName.empty())
		outView->renderItemName = GetEntityName(entity);

	outView->geometryName = meshComponent->GetGeometryName();
	outView->materialName = meshComponent->GetDefaultMaterialName();
	outView->renderLayerIndex = meshComponent->GetRenderLayerIndex();
	outView->visible = IsEntityVisible(entity);
	outView->isSkyEntity = outView->renderLayerIndex == 天空渲染项目;
	return true;
}

const std::vector<SceneEntityBase*>& WitchcraECS::GetSceneRootEntities() const
{
	return entities;
}

UINT WitchcraECS::GetSceneRootEntityCount() const
{
	return static_cast<UINT>(entities.size());
}

const std::vector<SceneEntityBase*>& WitchcraECS::GetSceneChildren(SceneEntityBase* entity) const
{
	return GetEntityChildren(entity);
}

const std::vector<SceneEntityBase*>& WitchcraECS::GetHierarchyRootEntities() const
{
	return entities;
}

UINT WitchcraECS::GetHierarchyRootEntityCount() const
{
	return static_cast<UINT>(entities.size());
}

const std::vector<SceneEntityBase*>& WitchcraECS::GetHierarchyChildren(SceneEntityBase* entity) const
{
	return GetEntityChildren(entity);
}

UINT WitchcraECS::GetHierarchyChildCount(SceneEntityBase* entity) const
{
	return GetEntityChildCount(entity);
}

const std::vector<SceneEntityBase*>& WitchcraECS::GetEntityChildren(SceneEntityBase* entity) const
{
	return WitchcraECSEntityHierarchyQueryBridge::GetEntityChildren(*this, entity);
}

UINT WitchcraECS::GetEntityChildCount(SceneEntityBase* entity) const
{
	return WitchcraECSEntityHierarchyQueryBridge::GetEntityChildCount(*this, entity);
}

bool WitchcraECS::HasEntityChildren(SceneEntityBase* entity) const
{
	return !GetEntityChildren(entity).empty();
}

bool WitchcraECS::IsSkyEntity(SceneEntityBase* entity) const
{
	const MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	return meshComponent != nullptr && meshComponent->GetRenderLayerIndex() == 天空渲染项目;
}

bool WitchcraECS::IsEntityStatic(SceneEntityBase* entity) const
{
	return entity != nullptr && entity->IsStatic();
}

bool WitchcraECS::IsEntityVisible(SceneEntityBase* entity) const
{
	if (entity == nullptr || entity->entity == 0 || mGeneralComponentDataId == 0)
		return true;

	const EntityGeneralComponentData* generalData = static_cast<const EntityGeneralComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mGeneralComponentDataId));
	return generalData == nullptr || generalData->visible;
}

bool WitchcraECS::SetEntityVisible(SceneEntityBase* entity, bool visible)
{
	if (entity == nullptr || entity->entity == 0 || mGeneralComponentDataId == 0)
		return false;

	EntityGeneralComponentData generalData{};
	const EntityGeneralComponentData* currentGeneralData = static_cast<const EntityGeneralComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mGeneralComponentDataId));
	if (currentGeneralData != nullptr)
		generalData = *currentGeneralData;

	generalData.visible = visible;
	entityWorld.entity(entity->entity).set<EntityGeneralComponentData>(generalData);
	return true;
}

ComponentType WitchcraECS::GetEntityGeneralComponentType(SceneEntityBase* entity) const
{
	if (entity == nullptr || entity->entity == 0 || mGeneralComponentDataId == 0)
		return ComponentType::Co_Unk;

	const EntityGeneralComponentData* generalData = static_cast<const EntityGeneralComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mGeneralComponentDataId));
	if (generalData == nullptr)
		return ComponentType::Co_Unk;

	return static_cast<ComponentType>(generalData->componentType);
}

bool WitchcraECS::SetEntityGeneralComponentType(SceneEntityBase* entity, ComponentType componentType)
{
	if (entity == nullptr || entity->entity == 0 || mGeneralComponentDataId == 0)
		return false;

	EntityGeneralComponentData generalData{};
	const EntityGeneralComponentData* currentGeneralData = static_cast<const EntityGeneralComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mGeneralComponentDataId));
	if (currentGeneralData != nullptr)
		generalData = *currentGeneralData;

	generalData.componentType = static_cast<std::uint32_t>(componentType);
	entityWorld.entity(entity->entity).set<EntityGeneralComponentData>(generalData);
	return true;
}

// 先把相机基础参数的读写统一收口到 ECS 语义层，后续 Inspector 可逐步切到这里。
bool WitchcraECS::GetEntityCameraFov(SceneEntityBase* entity, float* outFov) const
{
	if (entity == nullptr || entity->entity == 0 || outFov == nullptr || mCameraComponentDataId == 0)
		return false;

	const EntityCameraComponentData* cameraData = static_cast<const EntityCameraComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mCameraComponentDataId));
	if (cameraData == nullptr)
		return false;

	*outFov = cameraData->fovY;
	return true;
}

bool WitchcraECS::SetEntityCameraFov(SceneEntityBase* entity, float fov)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return false;

	if (fov < 0.1f || fov > 1.0f)
		return false;

	EntityCameraComponentData cameraData{};
	if (!GetEntityCameraFov(entity, &cameraData.fovY) ||
		!GetEntityCameraNear(entity, &cameraData.nearZ) ||
		!GetEntityCameraFar(entity, &cameraData.farZ) ||
		!GetEntityCameraScale(entity, &cameraData.viewportScale))
	{
		cameraData = EntityCameraComponentData{};
	}

	cameraData.fovY = fov;
	entityWorld.entity(entity->entity).set<EntityCameraComponentData>(cameraData);
	return true;
}

bool WitchcraECS::GetEntityCameraNear(SceneEntityBase* entity, float* outNearZ) const
{
	if (entity == nullptr || entity->entity == 0 || outNearZ == nullptr || mCameraComponentDataId == 0)
		return false;

	const EntityCameraComponentData* cameraData = static_cast<const EntityCameraComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mCameraComponentDataId));
	if (cameraData == nullptr)
		return false;

	*outNearZ = cameraData->nearZ;
	return true;
}

bool WitchcraECS::SetEntityCameraNear(SceneEntityBase* entity, float nearZ)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return false;

	EntityCameraComponentData cameraData{};
	if (!GetEntityCameraFov(entity, &cameraData.fovY) ||
		!GetEntityCameraNear(entity, &cameraData.nearZ) ||
		!GetEntityCameraFar(entity, &cameraData.farZ) ||
		!GetEntityCameraScale(entity, &cameraData.viewportScale))
	{
		cameraData = EntityCameraComponentData{};
	}

	if (nearZ <= 0.0f || nearZ > cameraData.farZ)
		return false;

	cameraData.nearZ = nearZ;
	entityWorld.entity(entity->entity).set<EntityCameraComponentData>(cameraData);
	return true;
}

bool WitchcraECS::GetEntityCameraFar(SceneEntityBase* entity, float* outFarZ) const
{
	if (entity == nullptr || entity->entity == 0 || outFarZ == nullptr || mCameraComponentDataId == 0)
		return false;

	const EntityCameraComponentData* cameraData = static_cast<const EntityCameraComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mCameraComponentDataId));
	if (cameraData == nullptr)
		return false;

	*outFarZ = cameraData->farZ;
	return true;
}

bool WitchcraECS::SetEntityCameraFar(SceneEntityBase* entity, float farZ)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return false;

	EntityCameraComponentData cameraData{};
	if (!GetEntityCameraFov(entity, &cameraData.fovY) ||
		!GetEntityCameraNear(entity, &cameraData.nearZ) ||
		!GetEntityCameraFar(entity, &cameraData.farZ) ||
		!GetEntityCameraScale(entity, &cameraData.viewportScale))
	{
		cameraData = EntityCameraComponentData{};
	}

	cameraData.farZ = farZ < cameraData.nearZ ? (cameraData.nearZ + 0.01f) : farZ;
	entityWorld.entity(entity->entity).set<EntityCameraComponentData>(cameraData);
	return true;
}

bool WitchcraECS::GetEntityCameraScale(SceneEntityBase* entity, float* outScale) const
{
	if (entity == nullptr || entity->entity == 0 || outScale == nullptr || mCameraComponentDataId == 0)
		return false;

	const EntityCameraComponentData* cameraData = static_cast<const EntityCameraComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mCameraComponentDataId));
	if (cameraData == nullptr)
		return false;

	*outScale = cameraData->viewportScale;
	return true;
}

bool WitchcraECS::SetEntityCameraScale(SceneEntityBase* entity, float scale)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return false;

	if (scale < 0.0f)
		return false;

	EntityCameraComponentData cameraData{};
	if (!GetEntityCameraFov(entity, &cameraData.fovY) ||
		!GetEntityCameraNear(entity, &cameraData.nearZ) ||
		!GetEntityCameraFar(entity, &cameraData.farZ) ||
		!GetEntityCameraScale(entity, &cameraData.viewportScale))
	{
		cameraData = EntityCameraComponentData{};
	}

	cameraData.viewportScale = scale;
	entityWorld.entity(entity->entity).set<EntityCameraComponentData>(cameraData);
	return true;
}

bool WitchcraECS::RestoreEntityCameraScale(SceneEntityBase* entity)
{
	CameraComponent* cameraComponent = GetComponent<CameraComponent>(entity);
	if (cameraComponent == nullptr)
		return false;

	cameraComponent->RestoreScale();
	return true;
}

bool WitchcraECS::GetEntityLightSnapshot(SceneEntityBase* entity, EntityLightComponentData* outSnapshot) const
{
	if (entity == nullptr || entity->entity == 0 || outSnapshot == nullptr || mLightComponentDataId == 0)
		return false;

	const EntityLightComponentData* lightData = static_cast<const EntityLightComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mLightComponentDataId));
	if (lightData == nullptr)
		return false;

	*outSnapshot = *lightData;
	return true;
}

bool WitchcraECS::SetEntityLightSnapshot(SceneEntityBase* entity, const EntityLightComponentData& snapshot)
{
	if (entity == nullptr || entity->entity == 0 || mLightComponentDataId == 0)
		return false;

	LightComponent* lightComponent = GetComponent<LightComponent>(entity);
	if (lightComponent == nullptr)
		lightComponent = AddLightComponent(entity);
	if (lightComponent == nullptr)
		return false;

	lightComponent->BindEntity(this, entity);

	EntityLightComponentData sanitizedSnapshot = snapshot;
	const LightKind lightKind = static_cast<LightKind>(sanitizedSnapshot.kind);
	sanitizedSnapshot.castShadow = lightKind != LightKind::Ambient && sanitizedSnapshot.castShadow;
	entityWorld.entity(entity->entity).set<EntityLightComponentData>(sanitizedSnapshot);
	return true;
}

bool WitchcraECS::GetEntityRigidBodySnapshot(SceneEntityBase* entity, EntityRigidBodyComponentData* outSnapshot) const
{
	return WitchcraECSRigidBodyBridge::GetEntitySnapshot(*this, entity, outSnapshot);
}

bool WitchcraECS::SetEntityRigidBodySnapshot(SceneEntityBase* entity, const EntityRigidBodyComponentData& snapshot)
{
	return WitchcraECSRigidBodyBridge::SetEntitySnapshot(*this, entity, snapshot);
}

bool WitchcraECS::GetEntityScriptingSnapshot(SceneEntityBase* entity, EntityScriptingComponentData* outSnapshot) const
{
	return WitchcraECSScriptingBridge::GetEntitySnapshot(*this, entity, outSnapshot);
}

bool WitchcraECS::GetEntityScriptSnapshot(SceneEntityBase* entity, size_t index, EntityScriptingComponentData::ScriptSnapshot* outSnapshot) const
{
	return WitchcraECSScriptingBridge::GetEntityScriptSnapshot(*this, entity, index, outSnapshot);
}

bool WitchcraECS::SetEntityScriptActive(SceneEntityBase* entity, size_t index, bool active)
{
	return WitchcraECSScriptingBridge::SetEntityScriptActive(*this, entity, index, active);
}

bool WitchcraECS::RemoveEntityScript(SceneEntityBase* entity, size_t index)
{
	return WitchcraECSScriptingBridge::RemoveEntityScript(*this, entity, index);
}

bool WitchcraECS::GetEntityPhysicsColliderSnapshot(SceneEntityBase* entity, size_t index, EntityPhysicsComponentData::ColliderSnapshot* outSnapshot) const
{
	return WitchcraECSPhysicsBridge::GetEntityColliderSnapshot(*this, entity, index, outSnapshot);
}

bool WitchcraECS::SetEntityPhysicsColliderSnapshot(SceneEntityBase* entity, size_t index, const EntityPhysicsComponentData::ColliderSnapshot& snapshot)
{
	return WitchcraECSPhysicsBridge::SetEntityColliderSnapshot(*this, entity, index, snapshot);
}

bool WitchcraECS::AddRigidbodyToEntity(SceneEntityBase* entity)
{
	return AddComponent<RigidBodyComponent>(entity) != nullptr;
}

// 通过 ECS 语义接口为实体添加一个盒体碰撞器。
bool WitchcraECS::AddBoxColliderToEntity(SceneEntityBase* entity)
{
	return WitchcraECSPhysicsBridge::AddBoxColliderToEntity(*this, entity);
}

// 通过 ECS 语义接口为实体追加脚本。
bool WitchcraECS::AddScriptToEntity(SceneEntityBase* entity, const std::wstring& scriptPath)
{
	return WitchcraECSScriptingBridge::AddEntityScript(*this, entity, scriptPath);
}

bool WitchcraECS::SetCameraEntityEngine(SceneEntityBase* entity, Engine* engine)
{
	CameraComponent* cameraComponent = GetComponent<CameraComponent>(entity);
	if (cameraComponent == nullptr)
		return false;

	cameraComponent->SetEngine(engine);
	return true;
}

bool WitchcraECS::ConfigureMeshEntity(
	SceneEntityBase* entity,
	Engine* engine,
	const std::wstring& displayName,
	const std::wstring& fileName,
	const std::wstring& meshName,
	UINT renderLayerIndex,
	const std::wstring& defaultMaterialName)
{
	MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	if (meshComponent == nullptr)
		return false;

	meshComponent->SetEngine(engine);
	meshComponent->SetName(displayName);
	if (!fileName.empty())
		meshComponent->SetFileName(fileName);
	meshComponent->SetMeshName(meshName.empty() ? displayName : meshName);
	meshComponent->SetRenderLayerIndex(renderLayerIndex);
	meshComponent->SetDefaultMaterialName(defaultMaterialName);
	return true;
}

bool WitchcraECS::SetMeshEntityExternalGeometry(SceneEntityBase* entity, const std::wstring& geometryName, AggregateGraphicObj* aggregateGraphicObj)
{
	MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	if (meshComponent == nullptr || aggregateGraphicObj == nullptr || geometryName.empty())
		return false;

	meshComponent->SetExternalRenderGeometry(geometryName, aggregateGraphicObj);
	return true;
}

bool WitchcraECS::AppendMeshEntityVertices(SceneEntityBase* entity, const std::vector<Vertex>& vertices)
{
	MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	if (meshComponent == nullptr)
		return false;

	for (const Vertex& vertex : vertices)
		meshComponent->AddVertices(vertex);

	return true;
}

bool WitchcraECS::AppendMeshEntityIndices(SceneEntityBase* entity, const std::vector<std::uint32_t>& indices)
{
	MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	if (meshComponent == nullptr)
		return false;

	for (std::uint32_t index : indices)
		meshComponent->AddIndices(static_cast<UINT>(index));

	return true;
}

bool WitchcraECS::SetupMeshEntity(SceneEntityBase* entity, D3DWindow* dx)
{
	MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	TransformComponent* transformComponent = GetComponent<TransformComponent>(entity);
	if (meshComponent == nullptr || transformComponent == nullptr || dx == nullptr)
		return false;

	meshComponent->SetupMesh(transformComponent, dx, meshComponent->GetIndexCount(), meshComponent->GetVertexCount());
	return true;
}

bool WitchcraECS::SetMeshEntityMaterial(SceneEntityBase* entity, const std::wstring& materialName)
{
	MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	if (meshComponent == nullptr || materialName.empty())
		return false;

	meshComponent->SetMaterial(materialName);
	return true;
}

bool WitchcraECS::RebuildMeshComponentOnEntity(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RebuildMeshComponentOnEntity(*this, entity);
}

bool WitchcraECS::RebuildCameraComponentOnEntity(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RebuildCameraComponentOnEntity(*this, entity);
}

bool WitchcraECS::RebuildPhysicsComponentOnEntity(SceneEntityBase* entity)
{
	return WitchcraECSPhysicsBridge::RebuildComponentOnEntity(*this, entity);
}

bool WitchcraECS::RebuildScriptingComponentOnEntity(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RebuildScriptingComponentOnEntity(*this, entity);
}

bool WitchcraECS::RebuildRigidbodyComponentOnEntity(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RebuildRigidbodyComponentOnEntity(*this, entity);
}

bool WitchcraECS::RemoveMeshComponentFromEntity(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveMeshComponentFromEntity(*this, entity);
}

bool WitchcraECS::RemoveCameraComponentFromEntity(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveCameraComponentFromEntity(*this, entity);
}

bool WitchcraECS::RemovePhysicsComponentFromEntity(SceneEntityBase* entity)
{
	return WitchcraECSPhysicsBridge::RemoveComponentFromEntity(*this, entity);
}

bool WitchcraECS::RemoveScriptingComponentFromEntity(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveScriptingComponentFromEntity(*this, entity);
}

bool WitchcraECS::RemoveRigidbodyComponentFromEntity(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveRigidbodyComponentFromEntity(*this, entity);
}

// ---------------------------------------------------------------------------
// Transform 读取 / 写入 / 同步。
// ---------------------------------------------------------------------------

bool WitchcraECS::GetEntityLocalTransform(SceneEntityBase* entity, Transform* outTransform) const
{
	return WitchcraECSTransformSyncBridge::GetEntityLocalTransform(*this, entity, outTransform);
}

bool WitchcraECS::GetEntityWorldTransform(SceneEntityBase* entity, Transform* outTransform) const
{
	return WitchcraECSTransformSyncBridge::GetEntityWorldTransform(*this, entity, outTransform);
}

bool WitchcraECS::GetEntityWorldMatrix(SceneEntityBase* entity, DirectX::XMFLOAT4X4* outMatrix) const
{
	return WitchcraECSTransformSyncBridge::GetEntityWorldMatrix(*this, entity, outMatrix);
}

bool WitchcraECS::GetEntityEditableLocalTransform(SceneEntityBase* entity, Transform* outTransform) const
{
	return WitchcraECSTransformSyncBridge::GetEntityEditableLocalTransform(*this, entity, outTransform);
}

bool WitchcraECS::SetEntityEditableLocalTransform(SceneEntityBase* entity, const Transform& transform)
{
	return WitchcraECSTransformSyncBridge::SetEntityEditableLocalTransform(*this, entity, transform);
}

bool WitchcraECS::GetEntityRenderTransform(SceneEntityBase* entity, Transform* outTransform) const
{
	return WitchcraECSTransformSyncBridge::GetEntityRenderTransform(*this, entity, outTransform);
}

bool WitchcraECS::GetEntityRenderMatrix(SceneEntityBase* entity, DirectX::XMFLOAT4X4* outMatrix) const
{
	return WitchcraECSTransformSyncBridge::GetEntityRenderMatrix(*this, entity, outMatrix);
}

// 当 transform 发生改动时，把 flecs 数据同步回旧缓存组件。
void WitchcraECS::SyncTransformsToFlecs()
{
	WitchcraECSTransformSyncBridge::SyncTransformsToFlecs(*this);
}

// ---------------------------------------------------------------------------
// 选择状态、运行时更新与清理。
// ---------------------------------------------------------------------------

// 判断层级重挂是否合法：不能挂到自己或自己的子级下面。
bool WitchcraECS::CanReparentEntityInHierarchy(SceneEntityBase* entity, SceneEntityBase* newParent) const
{
	return WitchcraECSEntityHierarchyBridge::CanReparentEntityInHierarchy(*this, entity, newParent);
}

// 把实体移动到新的父节点下；若目标层级重名，则自动生成同级唯一名称。
bool WitchcraECS::ReparentEntityInHierarchy(SceneEntityBase* entity, SceneEntityBase* newParent)
{
	const bool reparented = WitchcraECSEntityHierarchyBridge::ReparentEntityInHierarchy(*this, entity, newParent);
	if (reparented)
		SyncTransformsToFlecs();

	return reparented;
}

// 层级窗口选择实体时统一走这里，顺便做存在性校验。
bool WitchcraECS::SelectEntityForHierarchy(SceneEntityBase* entity)
{
	return WitchcraECSEntityHierarchyQueryBridge::SelectEntityForHierarchy(*this, entity);
}

// 清除层级窗口当前选择。
void WitchcraECS::ClearHierarchySelection()
{
	WitchcraECSEntityHierarchyQueryBridge::ClearHierarchySelection(*this);
}

// 层级窗口删除实体时统一走这里，明确表达“删子树”还是“保留子级”。
bool WitchcraECS::DeleteEntityFromHierarchy(SceneEntityBase* entity, bool destroyChildren)
{
	return WitchcraECSEntityHierarchyQueryBridge::DeleteEntityFromHierarchy(*this, entity, destroyChildren);
}

bool WitchcraECS::HasSelectedEntity() const
{
	return WitchcraECSEntityHierarchyQueryBridge::HasSelectedEntity(*this);
}

SceneEntityBase* WitchcraECS::GetSelectedEntity() const
{
	return WitchcraECSEntityHierarchyQueryBridge::GetSelectedEntity(*this);
}

void WitchcraECS::Update(float delta_time)
{
	entityWorld.progress(delta_time);
	SyncTransformsToFlecs();
	for (auto& entity : entities)
	{
		(void)entity;
	}
}

void WitchcraECS::Clear()
{
	selectedEntity = nullptr;

	// 逐个销毁根实体；DeleteEntityTree 会递归删除整棵子树。
	for (SceneEntityBase* entity : entities)
	{
		if (entity == nullptr)
			continue;

		DestroyEntitySubtreeComponents(entity);
		DeleteEntityTree(entity);
	}

	entities.clear();
	mEntityNameIndex.clear();
	mParentEntityIndex.clear();
	mTransformsDirty = false;
	mTransformsDirtyFull = false;
	mDirtyTransformRoots.clear();
}

void WitchcraECS::End()
{
	Clear();
	entityWorld.quit();
}

// 根据当前组件集合刷新 flecs 的实体类型标签。
void WitchcraECS::RefreshEntityTypeTags(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0)
		return;

	ClearEntityTypeTags(entity->entity);

	if (HasComponent<CameraComponent>(entity))
	{
		ecs_add_id(entityWorld, entity->entity, mCameraEntityTypeTagId);
		return;
	}

	if (HasComponent<MeshComponent>(entity))
	{
		ecs_add_id(entityWorld, entity->entity, mMeshEntityTypeTagId);
		return;
	}

	if (HasComponent<LightComponent>(entity))
	{
		ecs_add_id(entityWorld, entity->entity, mLightEntityTypeTagId);
		return;
	}

	// 无相机/网格/灯光组件时，先标记为 Unknown。
	ecs_add_id(entityWorld, entity->entity, mUnknownEntityTypeTagId);
}

// 移除实体身上的类型标签，随后会由 RefreshEntityTypeTags 重新写回。
void WitchcraECS::ClearEntityTypeTags(flecs::entity_t entityId)
{
	if (entityId == 0)
		return;

	if (mUnknownEntityTypeTagId != 0)
		ecs_remove_id(entityWorld, entityId, mUnknownEntityTypeTagId);
	if (mCameraEntityTypeTagId != 0)
		ecs_remove_id(entityWorld, entityId, mCameraEntityTypeTagId);
	if (mMeshEntityTypeTagId != 0)
		ecs_remove_id(entityWorld, entityId, mMeshEntityTypeTagId);
	if (mLightEntityTypeTagId != 0)
		ecs_remove_id(entityWorld, entityId, mLightEntityTypeTagId);
}

// 只同步脏子树，减少每帧重复计算。
void WitchcraECS::SyncTransformComponentCacheFromFlecs()
{
	WitchcraECSTransformSyncBridge::SyncTransformComponentCacheFromFlecs(*this);
}

// 递归计算 local/world transform 与 world matrix。
void WitchcraECS::SyncTransformSubtree(SceneEntityBase* entity, const DirectX::XMFLOAT4X4& parentWorldMatrix)
{
	WitchcraECSTransformSyncBridge::SyncTransformSubtree(*this, entity, parentWorldMatrix);
}
