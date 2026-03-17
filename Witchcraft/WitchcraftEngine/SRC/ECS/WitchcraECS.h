#pragma once

#include "Engine/EngineUtils.h"
#include "D3DWindow/D3D12_framework.h"

#include "System/ProjectSceneSystem.h"
#include "ServicesContainer/ServicesContainer.h"
#include "Component/BaseComponent.h"
#include <flecs.h>

// 由 flecs 持有的实体类型标签。
// 当前阶段仍保留显式实体树，但实体类型信息改由 flecs 记录。
struct EntityUnknownTag {};
struct EntityCameraTag {};
struct EntityMeshTag {};
struct EntityLightTag {};

// 为后续 Transform 迁移准备的 flecs 数据组件。
// 当前仍以 TransformComponent 为编辑入口；这里先把同一份数据实时同步到 flecs。
struct EntityLocalTransform
{
	Transform value; // 实体自身的局部变换
};

struct EntityWorldTransform
{
	Transform value; // 父级累计后的世界变换；最终渲染需再与 LocalTransform 组合
};

// 场景实体基类。
class SceneEntityBase {
public:
	SceneEntityBase()
	{
		// 每个实体都自带一个组件容器；后续所有组件都挂在这里。
		childrenContainer.Init(L"EntityBase");
	}

	virtual ~SceneEntityBase() = default;

	// 基础元数据。
	void SetName(std::wstring name);
	void SetTag(std::wstring tag);
	void SetStatic(bool arg);

	// 组件 / 子实体管理。
	// AddChildComponent 失败时会回收传入组件，调用方只需关心返回值。
	bool AddChildComponent(std::wstring name, BaseComponent* Component);
	// AddChild 只维护显式实体树；flecs 的父子关系由 WitchcraECS::CreateEntity/DestroyEntity 同步。
	void AddChild(std::wstring name, SceneEntityBase* Entity);
	bool RemoveChild(SceneEntityBase* Entity);
	std::vector<SceneEntityBase*> ReleaseChildren();
	void Destroy();
	void DestroyChildren();
	ServicesContainer* GetChildrenContainer(); // 当前实体挂载的组件容器
	const std::vector<SceneEntityBase*>& GetChildrenEntity() const;

public:
	std::wstring GetName();
	std::wstring GetTag();
	bool IsStatic();

public:
	// 运行时 flecs 实体句柄。
	// 目前主要用于维护父子关系与生命周期同步，渲染仍走显式实体树。
	flecs::entity_t entity;

private:
	// 统一销毁容器里的组件；MeshComponent 会额外触发渲染资源清理。
	void DestroyAllContainer(ServicesContainer* ChildrenContainer);

protected:
	// 基础实体信息。
	std::wstring nameEntity = L"基本实体";
	std::wstring tagEntity = L"空";
	bool staticEntity = false;

	ServicesContainer childrenContainer; // 组件容器

	std::vector<SceneEntityBase*> childrenEntity; // 子实体
};

class WitchcraECS
{
public:
	WitchcraECS();
	~WitchcraECS();

	// 初始化 flecs 世界与当前阶段需要的最小系统。
	bool Init();

	// 创建 / 销毁实体。
	// 当前约定：如果 selectedEntity 非空，则新实体作为其子实体创建；否则创建为根实体。
	void CreateEntity(std::wstring name, SceneEntityBase* Entity);
	void DestroyEntity(std::wstring name, bool destroyChildren = true);

	// 查询接口。
	// 名字查询会在整棵实体树中递归搜索；索引查询仅针对根实体数组。
	SceneEntityBase* GetEntity(std::wstring name);
	SceneEntityBase* GetEntity(UINT index);
	const std::vector<SceneEntityBase*>& GetRootEntities() const;
	UINT Size();
	std::wstring GetEntityTypeLabel(SceneEntityBase* entity);

	// 底层 flecs 读取接口：
	// 直接读取 EntityLocalTransform / EntityWorldTransform。
	bool GetEntityLocalTransform(SceneEntityBase* entity, Transform* outTransform);
	bool GetEntityWorldTransform(SceneEntityBase* entity, Transform* outTransform);

	// 面向编辑器的局部变换入口：
	// 当前等价于读取/写入 flecs 的 EntityLocalTransform。
	bool GetEntityEditableLocalTransform(SceneEntityBase* entity, Transform* outTransform);
	bool SetEntityEditableLocalTransform(SceneEntityBase* entity, const Transform& transform);

	// 面向渲染侧的最终变换入口：
	// 会把父级 world transform 与本地 local transform 组合成最终结果。
	bool GetEntityRenderTransform(SceneEntityBase* entity, Transform* outTransform);
	// 将 flecs 中的 local/world 数据同步回 TransformComponent 缓存。
	void SyncTransformsToFlecs();

	// 编辑器当前选中实体。
	void SetSelectedEntity(SceneEntityBase* entity);
	SceneEntityBase* GetSelectedEntity();

	// 每帧更新 flecs 世界。
	void Update(float delta_time);

	// 清空整棵实体树，同时同步释放组件和 flecs 句柄。
	void Clear();

	void End();
private:
	// 根据当前组件情况，把实体类型同步为 flecs 标签。
	void RefreshEntityTypeTags(SceneEntityBase* entity);
	void ClearEntityTypeTags(flecs::entity_t entityId);
	void RefreshAllEntityTransformsToFlecs();
	void RefreshEntityTransformsToFlecsRecursive(SceneEntityBase* entity, const Transform& parentWorldTransform);

	flecs::world entityWorld;
	std::vector<SceneEntityBase*> entities; // 根实体列表

	SceneEntityBase* selectedEntity = nullptr; // 当前选中的实体
	ecs_entity_t mUnknownEntityTypeTagId = 0;
	ecs_entity_t mCameraEntityTypeTagId = 0;
	ecs_entity_t mMeshEntityTypeTagId = 0;
	ecs_entity_t mLightEntityTypeTagId = 0;
	ecs_entity_t mLocalTransformComponentId = 0;
	ecs_entity_t mWorldTransformComponentId = 0;
};
