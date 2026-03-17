#include "WitchcraECS.h"
#include "String/SStringUtils.h"
#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"

#include <vector>

namespace
{
	Transform CombineTransforms(const Transform& parentTransform, const Transform& localTransform)
	{
		Transform combined = localTransform;
		combined.position.x += parentTransform.position.x;
		combined.position.y += parentTransform.position.y;
		combined.position.z += parentTransform.position.z;
		combined.rotation.x += parentTransform.rotation.x;
		combined.rotation.y += parentTransform.rotation.y;
		combined.rotation.z += parentTransform.rotation.z;
		combined.scale.x *= parentTransform.scale.x;
		combined.scale.y *= parentTransform.scale.y;
		combined.scale.z *= parentTransform.scale.z;
		return combined;
	}

	// 递归判断 target 是否位于 root 这棵子树中。
	// 删除实体时用于判断当前选中实体是否也需要一并清空。
	bool ContainsEntity(SceneEntityBase* root, SceneEntityBase* target)
	{
		if (root == nullptr || target == nullptr)
			return false;
		if (root == target)
			return true;

		for (SceneEntityBase* child : root->GetChildrenEntity())
		{
			if (ContainsEntity(child, target))
				return true;
		}

		return false;
	}

	// 在实体树中查找 target 的直接父实体。
	SceneEntityBase* FindParentRecursive(SceneEntityBase* parent, SceneEntityBase* target)
	{
		if (parent == nullptr || target == nullptr)
			return nullptr;

		for (SceneEntityBase* child : parent->GetChildrenEntity())
		{
			if (child == target)
				return parent;

			if (SceneEntityBase* found = FindParentRecursive(child, target))
				return found;
		}

		return nullptr;
	}

	// 按名称递归搜索实体。
	SceneEntityBase* FindEntityByNameRecursive(SceneEntityBase* root, const std::wstring& name)
	{
		if (root == nullptr)
			return nullptr;
		if (root->GetName() == name)
			return root;

		for (SceneEntityBase* child : root->GetChildrenEntity())
		{
			if (SceneEntityBase* found = FindEntityByNameRecursive(child, name))
				return found;
		}

		return nullptr;
	}

	// 递归销毁一整棵实体树对应的 flecs 节点与 C++ 实体对象。
	void DeleteEntityTree(flecs::world& world, SceneEntityBase* entity)
	{
		if (entity == nullptr)
			return;

		for (SceneEntityBase* child : entity->GetChildrenEntity())
		{
			DeleteEntityTree(world, child);
		}

		if (entity->entity != 0)
			ecs_delete(world, entity->entity);

		delete entity;
	}

	// 删除父节点但保留子节点时，将子节点整体上提一层。
	void MoveChildrenUpOneLevel(
		flecs::world& world,
		std::vector<SceneEntityBase*>& rootEntities,
		SceneEntityBase* entity,
		SceneEntityBase* parent)
	{
		if (entity == nullptr)
			return;

		std::vector<SceneEntityBase*> children = entity->ReleaseChildren();
		for (SceneEntityBase* child : children)
		{
			if (child == nullptr)
				continue;

			if (child->entity != 0 && entity->entity != 0)
				ecs_remove_pair(world, child->entity, EcsChildOf, entity->entity);

			if (parent != nullptr)
			{
				parent->AddChild(child->GetName(), child);
				if (child->entity != 0 && parent->entity != 0)
					ecs_add_pair(world, child->entity, EcsChildOf, parent->entity);
			}
			else
			{
				rootEntities.push_back(child);
			}
		}
	}
}

void SceneEntityBase::SetName(std::wstring name)
{
	if (!name.compare(L""))
		return;

	childrenContainer.ReName(name);
	nameEntity = name;

	// GeneralComponent 中也缓存了一份可编辑名称；
	// 这里顺手同步，避免层级窗口与 Inspector 显示不一致。
	if (auto* generalComponent = childrenContainer.FindServiceAs<GeneralComponent>(L"GeneralComponent"))
		generalComponent->SetName(name);
}

void SceneEntityBase::SetTag(std::wstring tag)
{
	if (!tag.compare(L""))
		return;
	tagEntity = tag;

	if (auto* generalComponent = childrenContainer.FindServiceAs<GeneralComponent>(L"GeneralComponent"))
		generalComponent->SetTag(tag);
}

void SceneEntityBase::SetStatic(bool arg)
{
	staticEntity = arg;

	if (auto* generalComponent = childrenContainer.FindServiceAs<GeneralComponent>(L"GeneralComponent"))
		generalComponent->SetStatic(arg);
}

bool SceneEntityBase::AddChildComponent(std::wstring name, BaseComponent* Component)
{
	if (Component == nullptr)
		return false;

	// ServicesContainer 负责唯一组件约束；失败时当前函数兜底回收组件。
	if (!childrenContainer.AddService(name, Component))
	{
		delete Component;
		return false;
	}

	return true;
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

void SceneEntityBase::Destroy()
{
	// 先递归销毁子实体上的组件，再清理当前实体自己的组件容器。
	for (UINT i = 0; i < childrenEntity.size(); i++)
		childrenEntity[i]->Destroy();
	DestroyAllContainer(&childrenContainer);
}

void SceneEntityBase::DestroyChildren()
{
	DestroyAllContainer(&childrenContainer);
}

ServicesContainer* SceneEntityBase::GetChildrenContainer()
{
	return &childrenContainer;
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

void SceneEntityBase::DestroyAllContainer(ServicesContainer* ChildrenContainer)
{
	if (ChildrenContainer == nullptr)
		return;

	for (const auto& servicePair : ChildrenContainer->GetAllServices())
	{
		BaseComponent* component = static_cast<BaseComponent*>(servicePair.second);
		if (component == nullptr)
			continue;

		if (servicePair.first == L"MeshComponent")
		{
			static_cast<MeshComponent*>(component)->Destroy();
		}
		else
		{
			component->Destroy();
		}

		delete component;
	}

	ChildrenContainer->RemoveAll();
}

WitchcraECS::WitchcraECS()
{
}

WitchcraECS::~WitchcraECS()
{
}

bool WitchcraECS::Init()
{	
	// 当前 flecs 世界只保留最小初始化；
	// 真正的编辑器/渲染数据仍以 SceneEntityBase 实体树为主。
	mUnknownEntityTypeTagId = entityWorld.component<EntityUnknownTag>().id();
	mCameraEntityTypeTagId = entityWorld.component<EntityCameraTag>().id();
	mMeshEntityTypeTagId = entityWorld.component<EntityMeshTag>().id();
	mLightEntityTypeTagId = entityWorld.component<EntityLightTag>().id();
	mLocalTransformComponentId = entityWorld.component<EntityLocalTransform>().id();
	mWorldTransformComponentId = entityWorld.component<EntityWorldTransform>().id();

	entityWorld.system<ProjectSceneSystem, const ProjectSceneSystem>()
		.each([](ProjectSceneSystem& Scene, const ProjectSceneSystem& ps) {
		Scene = ps;
			});

	auto e = entityWorld.entity()
		.set([](ProjectSceneSystem& Scene, ProjectSceneSystem& v) {
		//p = { 10, 20 };
		//ps = { 1, 2 };
			});

	return true;
}

void WitchcraECS::CreateEntity(std::wstring name, SceneEntityBase* Entity)
{
	if (Entity == nullptr)
		return;

	// 如果当前有选中实体，则将新实体作为子实体挂到其下；
	// 否则作为根实体加入 entities。
	if (selectedEntity)
	{
		selectedEntity->AddChild(name, Entity);
		Entity->SetName(name);
		Entity->entity = ecs_new_w_pair(entityWorld, EcsChildOf, selectedEntity->entity);
		RefreshEntityTypeTags(Entity);
	}
	else
	{
		entities.resize(entities.size() + 1);
		entities[entities.size() - 1] = Entity;
		entities[entities.size() - 1]->SetName(name);
		ecs_entity_desc_t desc = {0};
		desc.id = 0;
		//desc.name = SString::WstringToString(name).c_str();
		entities[entities.size() - 1]->entity = ecs_entity_init(entityWorld, &desc);
		RefreshEntityTypeTags(entities[entities.size() - 1]);
	}

	// 创建时先为 flecs 放入默认 local/world transform。
	// 真实初始值应由外部通过 ECS 的 Transform 写入口显式设置。
	entityWorld.entity(Entity->entity).set<EntityLocalTransform>({ Transform{} });
	entityWorld.entity(Entity->entity).set<EntityWorldTransform>({ Transform{} });
	RefreshAllEntityTransformsToFlecs();
}

void WitchcraECS::DestroyEntity(std::wstring name, bool destroyChildren)
{
	// 先尝试从根实体表中查找。
	for (auto it = entities.begin(); it != entities.end(); ++it)
	{
		if ((*it)->GetName() == name)
		{
			SceneEntityBase* target = *it;
			if (destroyChildren)
			{
				if (ContainsEntity(target, selectedEntity))
					selectedEntity = nullptr;

				target->Destroy();
				DeleteEntityTree(entityWorld, target);
			}
			else
			{
				if (selectedEntity == target)
					selectedEntity = nullptr;

				entities.erase(it);
				MoveChildrenUpOneLevel(entityWorld, entities, target, nullptr);
				target->DestroyChildren();
				if (target->entity != 0)
					ecs_delete(entityWorld, target->entity);
				delete target;
				return;
			}

			entities.erase(it);
			return;
		}
	}

	// 如果不是根实体，再递归到各根实体子树中查找。
	for (SceneEntityBase* entity : entities)
	{
		SceneEntityBase* target = FindEntityByNameRecursive(entity, name);
		if (target == nullptr || target == entity)
			continue;

		SceneEntityBase* parent = FindParentRecursive(entity, target);
		if (parent == nullptr)
			continue;

		if (destroyChildren)
		{
			if (ContainsEntity(target, selectedEntity))
				selectedEntity = nullptr;

			target->Destroy();
			DeleteEntityTree(entityWorld, target);
			parent->RemoveChild(target);
			return;
		}

		if (selectedEntity == target)
			selectedEntity = nullptr;

		MoveChildrenUpOneLevel(entityWorld, entities, target, parent);
		target->DestroyChildren();
		if (target->entity != 0)
			ecs_delete(entityWorld, target->entity);
		parent->RemoveChild(target);
		delete target;
		return;
	}
}

SceneEntityBase* WitchcraECS::GetEntity(std::wstring name)
{
	// 按名称在整棵实体树中搜索，而不只查根节点。
	for (SceneEntityBase* rootEntity : entities)
	{
		if (SceneEntityBase* found = FindEntityByNameRecursive(rootEntity, name))
			return found;
	}

	return nullptr;
}

SceneEntityBase* WitchcraECS::GetEntity(UINT index)
{
	if (index >= entities.size())
		return nullptr;

	return entities[index];
}

const std::vector<SceneEntityBase*>& WitchcraECS::GetRootEntities() const
{
	return entities;
}

UINT WitchcraECS::Size()
{
	return static_cast<UINT>(entities.size());
}

std::wstring WitchcraECS::GetEntityTypeLabel(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0)
		return L"未知";

	if (mMeshEntityTypeTagId != 0 && ecs_has_id(entityWorld, entity->entity, mMeshEntityTypeTagId))
		return L"网格";

	if (mCameraEntityTypeTagId != 0 && ecs_has_id(entityWorld, entity->entity, mCameraEntityTypeTagId))
		return L"相机";

	if (mLightEntityTypeTagId != 0 && ecs_has_id(entityWorld, entity->entity, mLightEntityTypeTagId))
		return L"灯光";

	if (mUnknownEntityTypeTagId != 0 && ecs_has_id(entityWorld, entity->entity, mUnknownEntityTypeTagId))
		return L"空实体";

	return L"未知";
}

bool WitchcraECS::GetEntityLocalTransform(SceneEntityBase* entity, Transform* outTransform)
{
	if (entity == nullptr || entity->entity == 0 || outTransform == nullptr || mLocalTransformComponentId == 0)
		return false;

	const EntityLocalTransform* transformData = static_cast<const EntityLocalTransform*>(
		ecs_get_id(entityWorld, entity->entity, mLocalTransformComponentId));
	if (transformData == nullptr)
		return false;

	*outTransform = transformData->value;
	return true;
}

bool WitchcraECS::GetEntityWorldTransform(SceneEntityBase* entity, Transform* outTransform)
{
	if (entity == nullptr || entity->entity == 0 || outTransform == nullptr || mWorldTransformComponentId == 0)
		return false;

	const EntityWorldTransform* transformData = static_cast<const EntityWorldTransform*>(
		ecs_get_id(entityWorld, entity->entity, mWorldTransformComponentId));
	if (transformData == nullptr)
		return false;

	*outTransform = transformData->value;
	return true;
}

bool WitchcraECS::GetEntityEditableLocalTransform(SceneEntityBase* entity, Transform* outTransform)
{
	if (entity == nullptr || outTransform == nullptr)
		return false;

	// local transform 现在由 flecs 持有，外部读取统一走这里。
	return GetEntityLocalTransform(entity, outTransform);
}

bool WitchcraECS::SetEntityEditableLocalTransform(SceneEntityBase* entity, const Transform& transform)
{
	if (entity == nullptr || entity->entity == 0 || mLocalTransformComponentId == 0)
		return false;

	Transform sanitizedTransform = transform;
	sanitizedTransform.scale.x = sanitizedTransform.scale.x < 0.0f ? 0.0f : sanitizedTransform.scale.x;
	sanitizedTransform.scale.y = sanitizedTransform.scale.y < 0.0f ? 0.0f : sanitizedTransform.scale.y;
	sanitizedTransform.scale.z = sanitizedTransform.scale.z < 0.0f ? 0.0f : sanitizedTransform.scale.z;

	// local transform 的真实写入口切到 flecs；
	// TransformComponent 只做同步缓存，方便旧 UI/旧代码逐步迁移。
	entityWorld.entity(entity->entity).set<EntityLocalTransform>({ sanitizedTransform });

	if (ServicesContainer* services = entity->GetChildrenContainer())
	{
		if (TransformComponent* transformComponent = services->FindServiceAs<TransformComponent>(L"TransformComponent"))
			transformComponent->localTransform = sanitizedTransform;
	}

	RefreshAllEntityTransformsToFlecs();
	return true;
}

bool WitchcraECS::GetEntityRenderTransform(SceneEntityBase* entity, Transform* outTransform)
{
	if (entity == nullptr || outTransform == nullptr)
		return false;

	Transform localTransform{};
	Transform worldTransform{};
	const bool hasLocalTransform = GetEntityEditableLocalTransform(entity, &localTransform);
	const bool hasWorldTransform = GetEntityWorldTransform(entity, &worldTransform);
	if (hasLocalTransform && hasWorldTransform)
	{
		*outTransform = CombineTransforms(worldTransform, localTransform);
		return true;
	}

	if (ServicesContainer* services = entity->GetChildrenContainer())
	{
		if (TransformComponent* transformComponent = services->FindServiceAs<TransformComponent>(L"TransformComponent"))
		{
			*outTransform = transformComponent->GetTransform();
			return true;
		}
	}

	return false;
}

void WitchcraECS::SyncTransformsToFlecs()
{
	RefreshAllEntityTransformsToFlecs();
}

void WitchcraECS::SetSelectedEntity(SceneEntityBase* entity)
{
	// 当前允许外部直接设置选中目标；
	// 后续若需要更严格的校验，可在这里确认 entity 是否仍属于当前 ECS。
	selectedEntity = entity;
}

SceneEntityBase* WitchcraECS::GetSelectedEntity()
{
	return selectedEntity;
}

void WitchcraECS::Update(float delta_time)
{
	// flecs 侧按需推进；SceneEntityBase 的编辑器逻辑仍由外层显式驱动。
	entityWorld.progress(delta_time);
	RefreshAllEntityTransformsToFlecs();
	for (auto& entity : entities)
	{
		// 在这里可以添加更新逻辑
		//entities.
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

		entity->Destroy();
		DeleteEntityTree(entityWorld, entity);
	}

	entities.clear();
}

void WitchcraECS::End()
{
	Clear();
	entityWorld.quit();
}

void WitchcraECS::RefreshEntityTypeTags(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->entity == 0)
		return;

	ClearEntityTypeTags(entity->entity);

	ServicesContainer* services = entity->GetChildrenContainer();
	if (services == nullptr)
	{
		ecs_add_id(entityWorld, entity->entity, mUnknownEntityTypeTagId);
		return;
	}

	if (services->FindServiceAs<CameraComponent>(L"CameraComponent") != nullptr)
	{
		ecs_add_id(entityWorld, entity->entity, mCameraEntityTypeTagId);
		return;
	}

	if (services->FindServiceAs<MeshComponent>(L"MeshComponent") != nullptr)
	{
		ecs_add_id(entityWorld, entity->entity, mMeshEntityTypeTagId);
		return;
	}

	// 目前 LightComponent 还没有接入；无相机/网格组件时，先标记为 Unknown。
	ecs_add_id(entityWorld, entity->entity, mUnknownEntityTypeTagId);
}

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

void WitchcraECS::RefreshAllEntityTransformsToFlecs()
{
	const Transform identityTransform = Transform{};
	for (SceneEntityBase* entity : entities)
	{
		if (entity == nullptr)
			continue;

		RefreshEntityTransformsToFlecsRecursive(entity, identityTransform);
	}
}

void WitchcraECS::RefreshEntityTransformsToFlecsRecursive(SceneEntityBase* entity, const Transform& parentWorldTransform)
{
	if (entity == nullptr || entity->entity == 0)
		return;

	Transform localTransform = Transform{};
	if (mLocalTransformComponentId != 0)
	{
		const EntityLocalTransform* localTransformData = static_cast<const EntityLocalTransform*>(
			ecs_get_id(entityWorld, entity->entity, mLocalTransformComponentId));
		if (localTransformData != nullptr)
			localTransform = localTransformData->value;
	}

	// 当前约定：
	// 1. LocalTransform 始终保存“实体自身的本地变换”
	// 2. WorldTransform 保存“父级已经累计好的世界变换”
	// 这样根实体的 WorldTransform 为单位变换，子实体则拿到父实体的最终变换；
	// 真正交给渲染侧时，再由 World + Local 组合出最终矩阵。
	const Transform worldTransform = parentWorldTransform;
	const Transform combinedRenderTransform = CombineTransforms(worldTransform, localTransform);

	if (ServicesContainer* services = entity->GetChildrenContainer())
	{
		if (TransformComponent* transformComponent = services->FindServiceAs<TransformComponent>(L"TransformComponent"))
		{
			transformComponent->localTransform = localTransform;
			transformComponent->globalTransform = combinedRenderTransform;
		}
	}

	entityWorld.entity(entity->entity).set<EntityLocalTransform>({ localTransform });
	entityWorld.entity(entity->entity).set<EntityWorldTransform>({ worldTransform });

	for (SceneEntityBase* child : entity->GetChildrenEntity())
	{
		RefreshEntityTransformsToFlecsRecursive(child, combinedRenderTransform);
	}
}
