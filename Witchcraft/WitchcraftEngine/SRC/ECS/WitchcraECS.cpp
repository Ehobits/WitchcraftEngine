#include "WitchcraECS.h"
#include "WitchcraECSEntityHierarchyBridge.h"
#include "WitchcraECSEntityHierarchyQueryBridge.h"
#include "WitchcraECSComponentLifecycleBridge.h"
#include "WitchcraECSInspectorBridge.h"
#include "WitchcraECSPhysicsBridge.h"
#include "WitchcraECSRigidBodyBridge.h"
#include "WitchcraECSScriptingBridge.h"
#include "WitchcraECSTransformSyncBridge.h"
#include "Editor/Window/ConsoleWindow.h"
#include "Engine/Engine.h"
#include "D3DWindow/D3DWindow.h"
#include "String/SStringUtils.h"
#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/LightComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/SkeletonComponent.h"
#include "ECS/COMPONENT/AnimatorComponent.h"
#include "ECS/COMPONENT/SkinnedMeshComponent.h"
#include "ECS/COMPONENT/SkinningRuntimeComponent.h"
#include "ECS/COMPONENT/RenderDrawSetComponent.h"
#include "ECS/COMPONENT/BillboardComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
#include "ECS/COMPONENT/ScriptingComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"
#include "System/Animation/Assets/SkeletonAsset.h"
#include "System/WitchcraftFile/WSkeletonFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cwctype>
#include <functional>
#include <unordered_set>
#include <filesystem>

float WitchcraECS::MinColorChannel = 0.0f;
float WitchcraECS::MaxColorChannel = 1.0f;

void WitchcraECS::TagSkeletonHierarchyEntity(
	SceneEntityBase* entity,
	SceneEntityBase* ownerEntity,
	std::int32_t boneIndex,
	bool isRoot)
{
	if (entity == nullptr)
		return;

	entity->skeletonHierarchyOwnerEntity = ownerEntity;
	entity->skeletonHierarchyBoneIndex = boneIndex;
	entity->skeletonHierarchyRoot = isRoot;
}

bool WitchcraECS::BuildSkeletonBoneHierarchyRecursive(
	WitchcraECS& ecs,
	SceneEntityBase* ownerEntity,
	SceneEntityBase* parentEntity,
	const Witchcraft::Animation::SkeletonTopology& topology,
	std::int32_t boneIndex,
	bool isRootHierarchyNode)
{
	if (!topology.IsValidBoneIndex(boneIndex) || parentEntity == nullptr)
		return false;

	const Witchcraft::Animation::SkeletonBone& bone = topology.Bones[static_cast<size_t>(boneIndex)];
	const std::wstring displayName = bone.Name.empty()
		? (L"骨骼 " + std::to_wstring(boneIndex))
		: bone.Name;
	const std::wstring uniqueName = ecs.GetUniqueEntityName(displayName, parentEntity);
	SceneEntityBase* boneEntity = ecs.CreateBasicEntity(uniqueName, parentEntity, ComponentType::Co_Unk);
	if (boneEntity == nullptr)
		return false;

	TagSkeletonHierarchyEntity(boneEntity, ownerEntity, boneIndex, isRootHierarchyNode);
	ecs.RefreshEntityTypeTags(boneEntity);
	(void)ecs.SetEntityEditableLocalTransform(boneEntity, BuildTransformFromBoneLocalPose(bone.BindLocalPose));

	for (std::int32_t childBoneIndex = 0; childBoneIndex < static_cast<std::int32_t>(topology.Bones.size()); ++childBoneIndex)
	{
		if (topology.Bones[static_cast<size_t>(childBoneIndex)].ParentIndex != boneIndex)
			continue;
		(void)BuildSkeletonBoneHierarchyRecursive(ecs, ownerEntity, boneEntity, topology, childBoneIndex, false);
	}

	return true;
}

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

std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)> WitchcraECS::BuildDefaultSceneEntityTypeColors()
{
	std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)> colors{};
	colors[static_cast<size_t>(SceneEntityType::Sky)] = DirectX::XMFLOAT4(0.40f, 0.68f, 1.00f, 1.00f);
	colors[static_cast<size_t>(SceneEntityType::Ground)] = DirectX::XMFLOAT4(0.36f, 0.78f, 0.40f, 1.00f);
	colors[static_cast<size_t>(SceneEntityType::StaticScenery)] = DirectX::XMFLOAT4(0.70f, 0.70f, 0.70f, 1.00f);
	colors[static_cast<size_t>(SceneEntityType::DynamicScenery)] = DirectX::XMFLOAT4(0.98f, 0.62f, 0.22f, 1.00f);
	colors[static_cast<size_t>(SceneEntityType::Interactive)] = DirectX::XMFLOAT4(0.98f, 0.35f, 0.35f, 1.00f);
	return colors;
}

SceneEntityType WitchcraECS::SanitizeSceneEntityTypeValue(std::uint32_t rawValue)
{
	return ::SanitizeSceneEntityType(rawValue);
}

DirectX::XMFLOAT4 WitchcraECS::ClampColor(const DirectX::XMFLOAT4& color)
{
	return DirectX::XMFLOAT4(
		(std::max)(MinColorChannel, (std::min)(MaxColorChannel, color.x)),
		(std::max)(MinColorChannel, (std::min)(MaxColorChannel, color.y)),
		(std::max)(MinColorChannel, (std::min)(MaxColorChannel, color.z)),
		(std::max)(MinColorChannel, (std::min)(MaxColorChannel, color.w)));
}

void WitchcraECS::SetConsoleWindow(ConsoleWindow* consoleWindow)
{
	mConsoleWindow = consoleWindow;
}

void WitchcraECS::LogDebugMessage(const wchar_t* format, ...) const
{
	if (mConsoleWindow == nullptr || format == nullptr || format[0] == L'\0')
		return;

	wchar_t buffer[2048] = {};
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
	va_end(args);
	mConsoleWindow->AddDebugMessage(L"%s", buffer);
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

SceneEntityType WitchcraECS::DetermineDefaultEntitySceneType(SceneEntityBase* entity) const
{
	if (IsSkyEntity(entity))
		return SceneEntityType::Sky;

	MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	if (meshComponent != nullptr)
	{
		std::wstring fileName = meshComponent->GetFileName();
		std::transform(fileName.begin(), fileName.end(), fileName.begin(),
			[](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
		if (fileName.find(L"plane.obj") != std::wstring::npos)
			return SceneEntityType::Ground;
		return SceneEntityType::StaticScenery;
	}

	return SceneEntityType::Interactive;
}

bool WitchcraECS::ApplySceneTypeVertexColorToMeshEntity(SceneEntityBase* entity)
{
	if (entity == nullptr)
		return false;

	MeshComponent* meshComponent = GetComponent<MeshComponent>(entity);
	if (meshComponent == nullptr)
		return false;

	SceneEntityType sceneType = SceneEntityType::StaticScenery;
	(void)GetEntitySceneType(entity, &sceneType);
	const DirectX::XMFLOAT4 color = GetEntitySceneTypeVertexColor(sceneType);
	LogDebugMessage(
		L"[SceneType][ApplyColor] entity=%s ptr=%p type=%s mesh=%s color=(%.3f,%.3f,%.3f,%.3f) vtx=%u idx=%u",
		entity->GetName().c_str(),
		entity,
		SceneEntityTypeToKey(sceneType),
		meshComponent->GetMeshName().c_str(),
		color.x, color.y, color.z, color.w,
		meshComponent->GetVertexCount(),
		meshComponent->GetIndexCount());
	D3DWindow* dx = meshComponent->GetEngine() != nullptr ? meshComponent->GetEngine()->GetD3DWindow() : nullptr;
	if (!meshComponent->OwnsGeometry())
	{
		return dx != nullptr && dx->SetGeometryVertexColor(meshComponent->GetGeometryName(), color);
	}

	if (!meshComponent->SetAllVertexColor(color))
	{
		LogDebugMessage(
			L"[SceneType][ApplyColor] skip SetAllVertexColor failed (likely no CPU vertices) entity=%s mesh=%s",
			entity->GetName().c_str(),
			meshComponent->GetMeshName().c_str());
		return false;
	}

	TransformComponent* transformComponent = GetComponent<TransformComponent>(entity);
	if (dx == nullptr || transformComponent == nullptr)
		return true;

	meshComponent->SetupMesh(transformComponent, dx, meshComponent->GetIndexCount(), meshComponent->GetVertexCount());
	LogDebugMessage(
		L"[SceneType][ApplyColor] setup done entity=%s mesh=%s geo=%s",
		entity->GetName().c_str(),
		meshComponent->GetMeshName().c_str(),
		meshComponent->GetGeometryName().c_str());
	return true;
}

void WitchcraECS::ApplySceneTypeVertexColorToSubtree(SceneEntityBase* rootEntity)
{
	if (rootEntity == nullptr)
		return;

	(void)ApplySceneTypeVertexColorToMeshEntity(rootEntity);
	for (SceneEntityBase* childEntity : GetEntityChildren(rootEntity))
		ApplySceneTypeVertexColorToSubtree(childEntity);
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
	mSkeletonEntityTypeTagId = entityWorld.component<EntitySkeletonTag>().id();
	mLocalTransformComponentId = entityWorld.component<EntityLocalTransform>().id();
	mWorldTransformComponentId = entityWorld.component<EntityWorldTransform>().id();
	mWorldMatrixComponentId = entityWorld.component<EntityWorldMatrix>().id();
	mGeneralComponentDataId = entityWorld.component<EntityGeneralComponentData>().id();
	mSceneTypeDataId = entityWorld.component<EntitySceneTypeData>().id();
	mCameraComponentDataId = entityWorld.component<EntityCameraComponentData>().id();
	mLightComponentDataId = entityWorld.component<EntityLightComponentData>().id();
	mPhysicsComponentDataId = entityWorld.component<EntityPhysicsComponentData>().id();
	mRigidBodyComponentDataId = entityWorld.component<EntityRigidBodyComponentData>().id();
	mScriptingComponentDataId = entityWorld.component<EntityScriptingComponentData>().id();
	mSceneEntityTypeVertexColors = BuildDefaultSceneEntityTypeColors();

	entityWorld.system<ProjectSceneSystem, const ProjectSceneSystem>()
		.each([](ProjectSceneSystem& scene, const ProjectSceneSystem& source)
			{
				scene = source;
			});

	// 这里不能对匿名 lambda 闭包类型调用 set，
	// 否则 flecs 会把空闭包类型误当成组件类型并在启动时断言。

	RegisterTransformSystems();
	EnsureEnvironmentEntity();
	EnsureDefaultAmbientLightEntity();
	return true;
}

const wchar_t* WitchcraECS::GetEnvironmentEntityName()
{
	return L"环境根节点";
}

SceneEntityBase* WitchcraECS::GetEnvironmentEntity() const
{
	return HasEntity(mEnvironmentEntity) ? mEnvironmentEntity : nullptr;
}

bool WitchcraECS::IsEnvironmentEntity(SceneEntityBase* entity) const
{
	return entity != nullptr && entity == GetEnvironmentEntity();
}

bool WitchcraECS::IsAmbientLightEntity(SceneEntityBase* entity) const
{
	EntityLightComponentData lightData{};
	if (!GetEntityLightSnapshot(entity, &lightData))
		return false;

	return static_cast<LightKind>(lightData.kind) == LightKind::Ambient;
}

SceneEntityBase* WitchcraECS::EnsureEnvironmentEntity()
{
	SceneEntityBase* environmentEntity = GetEnvironmentEntity();
	if (environmentEntity != nullptr)
	{
		auto it = std::find(entities.begin(), entities.end(), environmentEntity);
		if (it != entities.end() && it != entities.begin())
		{
			entities.erase(it);
			entities.insert(entities.begin(), environmentEntity);
		}
		return environmentEntity;
	}

	for (SceneEntityBase* rootEntity : entities)
	{
		if (rootEntity == nullptr)
			continue;

		if (GetEntityName(rootEntity) == GetEnvironmentEntityName())
		{
			mEnvironmentEntity = rootEntity;
			auto it = std::find(entities.begin(), entities.end(), mEnvironmentEntity);
			if (it != entities.end() && it != entities.begin())
			{
				entities.erase(it);
				entities.insert(entities.begin(), mEnvironmentEntity);
			}
			return mEnvironmentEntity;
		}
	}

	mEnvironmentEntity = CreateBasicEntity(GetEnvironmentEntityName(), nullptr, ComponentType::Co_Unk);
	auto it = std::find(entities.begin(), entities.end(), mEnvironmentEntity);
	if (it != entities.end() && it != entities.begin())
	{
		entities.erase(it);
		entities.insert(entities.begin(), mEnvironmentEntity);
	}
	return mEnvironmentEntity;
}

SceneEntityBase* WitchcraECS::EnsureDefaultAmbientLightEntity()
{
	SceneEntityBase* environmentEntity = EnsureEnvironmentEntity();
	if (environmentEntity == nullptr)
		return nullptr;

	const std::vector<SceneEntityBase*>& children = GetHierarchyChildren(environmentEntity);
	for (SceneEntityBase* child : children)
	{
		if (IsAmbientLightEntity(child))
			return child;
	}

	const std::wstring ambientName = GetUniqueEntityName(L"环境光", environmentEntity);
	SceneEntityBase* ambientEntity = CreateLightEntity(ambientName, environmentEntity);
	if (ambientEntity == nullptr)
		return nullptr;

	SetEntityLightSnapshot(ambientEntity, BuildDefaultAmbientLightSnapshot());
	SetEntityEditableLocalTransform(ambientEntity, Transform{});
	return ambientEntity;
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

SceneEntityBase* WitchcraECS::CreateSkeletonEntity(const std::wstring& name, SceneEntityBase* parent)
{
	SceneEntityBase* entity = CreateBasicEntity(name, parent, ComponentType::Co_Unk);
	if (entity == nullptr)
		return nullptr;

	entity->dedicatedSkeletonEntity = true;
	(void)EnsureSkeletonData(entity);
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
	SetEntitySceneType(entity, DetermineDefaultEntitySceneType(entity), false);
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
	if (IsEnvironmentEntity(target))
		return false;
	if (IsAmbientLightEntity(target))
		return false;

	return WitchcraECSEntityHierarchyBridge::DestroyEntity(*this, target, destroyChildren);
}

SceneEntityBase* WitchcraECS::DuplicateSelectedEntity(D3DWindow* dx, Engine* engine)
{
	SceneEntityBase* source = GetSelectedEntity();
	return DuplicateEntityHierarchy(source, dx, engine);
}

SceneEntityBase* WitchcraECS::DuplicateEntityHierarchy(SceneEntityBase* source, D3DWindow* dx, Engine* engine)
{
	if (source == nullptr || !HasEntity(source))
		return nullptr;
	if (IsEnvironmentEntity(source))
		return nullptr;

	SceneEntityBase* parent = GetParentEntity(source);
	SceneEntityBase* duplicateRoot = DuplicateEntityInternal(source, parent, dx, engine);
	if (duplicateRoot == nullptr)
		return nullptr;

	const std::vector<SceneEntityBase*>& sourceChildren = GetSceneChildren(source);
	for (SceneEntityBase* child : sourceChildren)
		DuplicateEntitySubtreeRecursive(child, duplicateRoot, dx, engine);

	SelectEntityForHierarchy(duplicateRoot);
	return duplicateRoot;
}

SceneEntityBase* WitchcraECS::DuplicateEntityInternal(SceneEntityBase* source, SceneEntityBase* parentOverride, D3DWindow* dx, Engine* engine)
{
	if (source == nullptr || !HasEntity(source))
		return nullptr;
	if (IsEnvironmentEntity(source))
		return nullptr;

	SceneEntityBase* parent = parentOverride;
	if (parent == nullptr && !IsRootEntity(source))
		parent = GetParentEntity(source);

	const std::wstring duplicatedName = GetUniqueEntityName(GetEntityName(source), parent);
	MeshComponent* sourceMesh = GetComponent<MeshComponent>(source);
	CameraComponent* sourceCamera = GetComponent<CameraComponent>(source);
	LightComponent* sourceLight = GetComponent<LightComponent>(source);
	BillboardComponent* sourceBillboard = GetComponent<BillboardComponent>(source);
	PhysicsComponent* sourcePhysics = GetComponent<PhysicsComponent>(source);

	Engine* resolvedEngine = engine;
	if (resolvedEngine == nullptr)
	{
		if (sourceMesh != nullptr)
			resolvedEngine = sourceMesh->GetEngine();
		if (resolvedEngine == nullptr && sourceCamera != nullptr)
			resolvedEngine = sourceCamera->GetEngine();
	}

	D3DWindow* resolvedDx = dx;
	if (resolvedDx == nullptr && resolvedEngine != nullptr)
		resolvedDx = resolvedEngine->GetD3DWindow();

	SceneEntityBase* duplicate = nullptr;

	if (sourceMesh != nullptr)
		duplicate = CreateMeshEntity(duplicatedName, parent);
	else if (sourceCamera != nullptr)
		duplicate = CreateCameraEntity(duplicatedName, parent);
	else if (sourceLight != nullptr)
		duplicate = CreateLightEntity(duplicatedName, parent);
	else
		duplicate = CreateBasicEntity(duplicatedName, parent, GetEntityGeneralComponentType(source));

	if (duplicate == nullptr)
		return nullptr;

	SetEntityTag(duplicate, GetEntityTag(source));
	SetEntityStatic(duplicate, IsEntityStatic(source));
	SetEntityVisible(duplicate, IsEntitySelfVisible(source));

	Transform editableLocalTransform{};
	if (GetEntityEditableLocalTransform(source, &editableLocalTransform))
		SetEntityEditableLocalTransform(duplicate, editableLocalTransform);

	SceneEntityType sceneType = SceneEntityType::StaticScenery;
	if (GetEntitySceneType(source, &sceneType))
		SetEntitySceneType(duplicate, sceneType);
	duplicate->dedicatedSkeletonEntity = source->dedicatedSkeletonEntity;

	if (IsSkeletonHierarchyEntity(source))
	{
		SceneEntityBase* duplicateOwnerEntity = nullptr;
		if (source->skeletonHierarchyRoot)
		{
			duplicateOwnerEntity = parent;
		}
		else if (parent != nullptr)
		{
			if (parent->skeletonHierarchyOwnerEntity != nullptr)
				duplicateOwnerEntity = parent->skeletonHierarchyOwnerEntity;
			else if (IsSkeletonHierarchyEntity(parent))
				duplicateOwnerEntity = parent;
		}

		if (duplicateOwnerEntity != nullptr)
		{
			TagSkeletonHierarchyEntity(
				duplicate,
				duplicateOwnerEntity,
				source->skeletonHierarchyBoneIndex,
				source->skeletonHierarchyRoot);
			RefreshEntityTypeTags(duplicate);
		}
	}

	if (HasSkeletonData(source))
	{
		if (Witchcraft::Animation::SkeletonData* duplicateSkeletonData = EnsureSkeletonData(duplicate))
		{
			*duplicateSkeletonData = *GetSkeletonData(source);
			(void)SyncSkeletonDataToComponent(duplicate);
		}
	}
	else if (GetComponent<SkeletonComponent>(source) != nullptr)
	{
		(void)SyncSkeletonDataFromComponent(source);
		if (Witchcraft::Animation::SkeletonData* sourceSkeletonData = GetSkeletonData(source))
		{
			if (Witchcraft::Animation::SkeletonData* duplicateSkeletonData = EnsureSkeletonData(duplicate))
			{
				*duplicateSkeletonData = *sourceSkeletonData;
				(void)SyncSkeletonDataToComponent(duplicate);
			}
		}
	}

	if (sourceMesh != nullptr)
	{
		MeshComponent* duplicateMesh = GetComponent<MeshComponent>(duplicate);
		if (duplicateMesh != nullptr)
		{
			duplicateMesh->CopySettingsFrom(*sourceMesh);
			// render item 名称在 D3DWindow::AllRitems 中是全局 key，
			// 不能只按“同级唯一”处理；否则复制带子树/多选复制时，
			// 不同父节点下的同名 mesh 仍可能互相覆盖，表现为层级里有实体但主画面只剩一个渲染项目。
			const std::wstring uniqueRenderItemName = BuildUniqueRenderItemName(resolvedDx, duplicatedName);
			duplicateMesh->SetMeshName(uniqueRenderItemName);
			if (duplicateMesh->OwnsGeometry())
				duplicateMesh->SetGeometryName(BuildUniqueGeometryName(resolvedDx, uniqueRenderItemName + L" Geo"));
			duplicateMesh->CopyCpuGeometryFrom(*sourceMesh);
			duplicateMesh->SetEngine(resolvedEngine);

			if (duplicateMesh->OwnsGeometry())
			{
				if (!duplicateMesh->GetIndices().empty() && !duplicateMesh->GetVertices().empty() && resolvedDx != nullptr)
					duplicateMesh->SetupMesh(GetComponent<TransformComponent>(duplicate), resolvedDx, duplicateMesh->GetIndexCount(), duplicateMesh->GetVertexCount());
			}
			else if (resolvedDx != nullptr)
			{
				AggregateGraphicObj* aggregateGraphicObj = resolvedDx->GetAggregateGraphicObj(duplicateMesh->GetGeometryName());
				if (aggregateGraphicObj != nullptr)
					SetMeshEntityExternalGeometry(duplicate, duplicateMesh->GetGeometryName(), aggregateGraphicObj);
			}

			SetMeshEntityMaterial(duplicate, sourceMesh->GetMaterialName());
		}
	}

	if (sourceCamera != nullptr)
	{
		CameraComponent* duplicateCamera = GetComponent<CameraComponent>(duplicate);
		if (duplicateCamera != nullptr)
		{
			duplicateCamera->CopySettingsFrom(*sourceCamera);
			duplicateCamera->SetEngine(resolvedEngine);
		}
	}

	EntityLightComponentData lightSnapshot{};
	if (GetEntityLightSnapshot(source, &lightSnapshot))
		SetEntityLightSnapshot(duplicate, lightSnapshot);

	if (sourceBillboard != nullptr)
	{
		BillboardComponent* duplicateBillboard = AddBillboardComponent(duplicate);
		if (duplicateBillboard != nullptr)
		{
			const BillboardData data = sourceBillboard->BuildData();
			duplicateBillboard->SetMode(data.Mode);
			duplicateBillboard->SetFacingMode(data.FacingMode);
			duplicateBillboard->SetSize(data.Width, data.Height);
			duplicateBillboard->SetScreenSize(data.ScreenSize);
			duplicateBillboard->SetOffset(data.Offset);
			duplicateBillboard->SetColor(data.Color);
			duplicateBillboard->SetMaterialName(sourceBillboard->GetMaterialName());
		}
	}

	EntityRigidBodyComponentData rigidBodySnapshot{};
	if (GetEntityRigidBodySnapshot(source, &rigidBodySnapshot))
		SetEntityRigidBodySnapshot(duplicate, rigidBodySnapshot);

	if (sourcePhysics != nullptr)
	{
		const std::vector<PhysicsBoxColliderSnapshot>& colliderSnapshots = sourcePhysics->GetColliderSnapshots();
		for (size_t colliderIndex = 0; colliderIndex < colliderSnapshots.size(); ++colliderIndex)
		{
			const PhysicsBoxColliderSnapshot& collider = colliderSnapshots[colliderIndex];
			if (collider.colliderType == static_cast<std::uint32_t>(PhysicsColliderType::Plane))
			{
				if (!AddPlaneColliderToEntity(duplicate))
					continue;
			}
			else
			{
				if (!AddBoxColliderToEntity(duplicate))
					continue;
			}

			PhysicsComponent* duplicatePhysics = GetComponent<PhysicsComponent>(duplicate);
			if (duplicatePhysics == nullptr)
				continue;

			const size_t duplicatedColliderIndex = duplicatePhysics->GetColliderSnapshots().empty()
				? 0
				: duplicatePhysics->GetColliderSnapshots().size() - 1;

			EntityPhysicsComponentData::ColliderSnapshot colliderSnapshot{};
			colliderSnapshot.colliderType = collider.colliderType;
			colliderSnapshot.activeComponent = collider.activeComponent;
			colliderSnapshot.staticFriction = collider.staticFriction;
			colliderSnapshot.dynamicFriction = collider.dynamicFriction;
			colliderSnapshot.restitution = collider.restitution;
			colliderSnapshot.center = collider.center;
			colliderSnapshot.size = collider.size;
			SetEntityPhysicsColliderSnapshot(duplicate, duplicatedColliderIndex, colliderSnapshot);
		}
	}

	EntityScriptingComponentData scriptingSnapshot{};
	if (GetEntityScriptingSnapshot(source, &scriptingSnapshot))
	{
		for (const EntityScriptingComponentData::ScriptSnapshot& script : scriptingSnapshot.scripts)
		{
			if (!AddScriptToEntity(duplicate, script.filePath))
				continue;

			EntityScriptingComponentData duplicateScriptingSnapshot{};
			if (GetEntityScriptingSnapshot(duplicate, &duplicateScriptingSnapshot) && !duplicateScriptingSnapshot.scripts.empty())
			{
				const size_t scriptIndex = duplicateScriptingSnapshot.scripts.size() - 1;
				SetEntityScriptActive(duplicate, scriptIndex, script.activeComponent);
			}
		}
	}

	if (resolvedDx != nullptr)
		resolvedDx->AddRenderItemsFromEntity(duplicate, this);

	return duplicate;
}

void WitchcraECS::DuplicateEntitySubtreeRecursive(SceneEntityBase* source, SceneEntityBase* duplicateParent, D3DWindow* dx, Engine* engine)
{
	if (source == nullptr)
		return;

	SceneEntityBase* duplicate = DuplicateEntityInternal(source, duplicateParent, dx, engine);
	if (duplicate == nullptr)
		return;

	const std::vector<SceneEntityBase*>& sourceChildren = GetSceneChildren(source);
	for (SceneEntityBase* child : sourceChildren)
		DuplicateEntitySubtreeRecursive(child, duplicate, dx, engine);
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

	component->ReleaseResources();
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
	if (IsEnvironmentEntity(entity))
		return false;

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

SkeletonComponent* WitchcraECS::AddSkeletonComponent(SceneEntityBase* entity)
{
	SkeletonComponent* component = WitchcraECSComponentLifecycleBridge::AddSkeletonComponent(*this, entity);
	if (component != nullptr)
	{
		if (HasSkeletonData(entity))
			(void)SyncSkeletonDataToComponent(entity);
		else
			(void)SyncSkeletonDataFromComponent(entity);
	}

	return component;
}

AnimatorComponent* WitchcraECS::AddAnimatorComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddAnimatorComponent(*this, entity);
}

SkinnedMeshComponent* WitchcraECS::AddSkinnedMeshComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddSkinnedMeshComponent(*this, entity);
}

SkinningRuntimeComponent* WitchcraECS::AddSkinningRuntimeComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddSkinningRuntimeComponent(*this, entity);
}

RenderDrawSetComponent* WitchcraECS::AddRenderDrawSetComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddRenderDrawSetComponent(*this, entity);
}

BillboardComponent* WitchcraECS::AddBillboardComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::AddBillboardComponent(*this, entity);
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
	if (HasPlaneColliderOnEntity(entity))
		return nullptr;

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

SkeletonComponent* WitchcraECS::ReplaceSkeletonComponent(SceneEntityBase* entity)
{
	SkeletonComponent* component = WitchcraECSComponentLifecycleBridge::ReplaceSkeletonComponent(*this, entity);
	if (component != nullptr)
	{
		if (HasSkeletonData(entity))
			(void)SyncSkeletonDataToComponent(entity);
		else
			(void)SyncSkeletonDataFromComponent(entity);
	}

	return component;
}

AnimatorComponent* WitchcraECS::ReplaceAnimatorComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceAnimatorComponent(*this, entity);
}

SkinnedMeshComponent* WitchcraECS::ReplaceSkinnedMeshComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceSkinnedMeshComponent(*this, entity);
}

SkinningRuntimeComponent* WitchcraECS::ReplaceSkinningRuntimeComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceSkinningRuntimeComponent(*this, entity);
}

RenderDrawSetComponent* WitchcraECS::ReplaceRenderDrawSetComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceRenderDrawSetComponent(*this, entity);
}

BillboardComponent* WitchcraECS::ReplaceBillboardComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::ReplaceBillboardComponent(*this, entity);
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

bool WitchcraECS::RemoveSkeletonComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveSkeletonComponent(*this, entity);
}

bool WitchcraECS::HasSkeletonData(SceneEntityBase* entity) const
{
	return entity != nullptr &&
		(entity->skeletonData != nullptr || GetComponent<SkeletonComponent>(entity) != nullptr);
}

Witchcraft::Animation::SkeletonData* WitchcraECS::GetSkeletonData(SceneEntityBase* entity)
{
	if (entity == nullptr)
		return nullptr;

	if (entity->skeletonData == nullptr)
	{
		if (SkeletonComponent* component = GetComponent<SkeletonComponent>(entity))
		{
			Witchcraft::Animation::SkeletonData* data = EnsureSkeletonData(entity);
			CopySkeletonDataFromComponent(*component, data);
		}
	}
	else if (entity->skeletonData->Topology.Bones.empty())
	{
		if (SkeletonComponent* component = GetComponent<SkeletonComponent>(entity))
		{
			if (!component->GetTopology().Bones.empty() || !component->GetSkeletonAssetPath().empty())
				CopySkeletonDataFromComponent(*component, entity->skeletonData.get());
		}
	}

	return entity->skeletonData.get();
}

const Witchcraft::Animation::SkeletonData* WitchcraECS::GetSkeletonData(SceneEntityBase* entity) const
{
	return const_cast<WitchcraECS*>(this)->GetSkeletonData(entity);
}

Witchcraft::Animation::SkeletonData* WitchcraECS::EnsureSkeletonData(SceneEntityBase* entity)
{
	if (entity == nullptr)
		return nullptr;

	if (entity->skeletonData == nullptr)
	{
		entity->skeletonData = std::make_unique<Witchcraft::Animation::SkeletonData>();
		RefreshEntityTypeTags(entity);
	}

	return entity->skeletonData.get();
}

void WitchcraECS::RemoveSkeletonData(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->skeletonData == nullptr)
		return;

	entity->skeletonData.reset();
	DeleteSkeletonHierarchyForEntity(entity);
	RefreshEntityTypeTags(entity);
}

void WitchcraECS::DeleteSkeletonHierarchyForEntity(SceneEntityBase* ownerEntity)
{
	if (ownerEntity == nullptr || !HasEntity(ownerEntity))
		return;

	std::vector<SceneEntityBase*> existingRoots;
	for (SceneEntityBase* childEntity : GetHierarchyChildren(ownerEntity))
	{
		if (childEntity != nullptr &&
			childEntity->skeletonHierarchyRoot &&
			childEntity->skeletonHierarchyOwnerEntity == ownerEntity)
		{
			existingRoots.push_back(childEntity);
		}
	}

	for (SceneEntityBase* rootEntity : existingRoots)
		(void)DeleteEntityFromHierarchy(rootEntity, true);
}

bool WitchcraECS::SyncSkeletonDataFromComponent(SceneEntityBase* entity)
{
	if (entity == nullptr)
		return false;

	SkeletonComponent* component = GetComponent<SkeletonComponent>(entity);
	if (component == nullptr)
		return false;

	Witchcraft::Animation::SkeletonData* data = EnsureSkeletonData(entity);
	CopySkeletonDataFromComponent(*component, data);
	return true;
}

bool WitchcraECS::SyncSkeletonDataToComponent(SceneEntityBase* entity)
{
	if (entity == nullptr || entity->skeletonData == nullptr)
		return false;

	SkeletonComponent* component = GetComponent<SkeletonComponent>(entity);
	if (component == nullptr)
		return false;

	CopySkeletonDataToComponent(*entity->skeletonData, component);
	return true;
}

bool WitchcraECS::SetEntitySkeletonAssetPath(SceneEntityBase* entity, const std::wstring& assetPath)
{
	Witchcraft::Animation::SkeletonData* data = EnsureSkeletonData(entity);
	if (data == nullptr)
		return false;

	const bool assetChanged = (data->SkeletonAssetPath != assetPath);
	if (assetChanged)
	{
		data->Topology = {};
		data->BoneNames.clear();
		data->LocalPose.clear();
		data->LocalMatrixPose.clear();
		data->GlobalPose.clear();
	}

	data->SkeletonAssetPath = assetPath;
	if (assetPath.empty())
	{
		data->Dirty = false;
		(void)SyncSkeletonDataToComponent(entity);
		(void)RebuildSkeletonHierarchyForEntity(entity);
		return true;
	}

	if (!assetChanged && !data->Topology.Bones.empty())
	{
		(void)SyncSkeletonDataToComponent(entity);
		(void)RebuildSkeletonHierarchyForEntity(entity);
		return true;
	}

	Witchcraft::Animation::SkeletonTopology topology;
	if (!TryLoadSkeletonTopologyFromAsset(assetPath, &topology))
	{
		data->Dirty = true;
		(void)SyncSkeletonDataToComponent(entity);
		(void)RebuildSkeletonHierarchyForEntity(entity);
		return false;
	}

	Witchcraft::Animation::PopulateSkeletonDataFromTopology(topology, data);
	data->SkeletonAssetPath = assetPath;
	(void)SyncSkeletonDataToComponent(entity);
	(void)RebuildSkeletonHierarchyForEntity(entity);
	return true;
}

bool WitchcraECS::EnsureEntitySkeletonRuntime(
	SceneEntityBase* entity,
	const std::wstring& assetPath,
	const Witchcraft::Animation::SkeletonTopology* fallbackInlineTopology,
	bool addSkeletonComponent,
	bool addAnimatorComponent,
	bool addSkinningRuntimeComponent,
	bool rebuildEditorHierarchy)
{
	if (entity == nullptr || !HasEntity(entity))
		return false;

	const bool skeletonAssetLoaded = SetEntitySkeletonAssetPath(entity, assetPath);
	Witchcraft::Animation::SkeletonData* skeletonData = EnsureSkeletonData(entity);
	if (skeletonData == nullptr)
		return false;

	if (!skeletonAssetLoaded &&
		fallbackInlineTopology != nullptr &&
		!fallbackInlineTopology->Bones.empty())
	{
		Witchcraft::Animation::PopulateSkeletonDataFromTopology(*fallbackInlineTopology, skeletonData);
		skeletonData->SkeletonAssetPath.clear();
	}

	skeletonData->Dirty = false;

	if (addSkeletonComponent)
		(void)AddComponent<SkeletonComponent>(entity);
	if (addAnimatorComponent)
		(void)AddComponent<AnimatorComponent>(entity);
	if (addSkinningRuntimeComponent)
		(void)AddComponent<SkinningRuntimeComponent>(entity);

	(void)SyncSkeletonDataToComponent(entity);

	if (rebuildEditorHierarchy)
		(void)RebuildSkeletonHierarchyForEntity(entity);
	else
		DeleteSkeletonHierarchyForEntity(entity);

	return skeletonAssetLoaded ||
		(fallbackInlineTopology != nullptr && !fallbackInlineTopology->Bones.empty()) ||
		!skeletonData->Topology.Bones.empty();
}

bool WitchcraECS::IsSkeletonHierarchyEntity(SceneEntityBase* entity) const
{
	return entity != nullptr &&
		entity->skeletonHierarchyOwnerEntity != nullptr &&
		HasEntity(entity->skeletonHierarchyOwnerEntity) &&
		entity->skeletonHierarchyBoneIndex >= -1;
}

bool WitchcraECS::TryGetSkeletonHierarchyBinding(
	SceneEntityBase* entity,
	SceneEntityBase** outOwnerEntity,
	std::int32_t* outBoneIndex,
	bool* outIsRoot) const
{
	if (!IsSkeletonHierarchyEntity(entity))
		return false;

	if (outOwnerEntity != nullptr)
		*outOwnerEntity = entity->skeletonHierarchyOwnerEntity;
	if (outBoneIndex != nullptr)
		*outBoneIndex = entity->skeletonHierarchyBoneIndex;
	if (outIsRoot != nullptr)
		*outIsRoot = entity->skeletonHierarchyRoot;
	return true;
}

SceneEntityBase* WitchcraECS::FindSkeletonHierarchyRoot(SceneEntityBase* ownerEntity) const
{
	if (ownerEntity == nullptr || !HasEntity(ownerEntity))
		return nullptr;

	for (SceneEntityBase* childEntity : GetHierarchyChildren(ownerEntity))
	{
		if (childEntity == nullptr)
			continue;
		if (childEntity->skeletonHierarchyRoot &&
			childEntity->skeletonHierarchyOwnerEntity == ownerEntity)
		{
			return childEntity;
		}
	}

	return nullptr;
}

bool WitchcraECS::RebuildSkeletonHierarchyForEntity(SceneEntityBase* ownerEntity)
{
	if (ownerEntity == nullptr || !HasEntity(ownerEntity))
		return false;

	std::vector<SceneEntityBase*> existingRoots;
	for (SceneEntityBase* childEntity : GetHierarchyChildren(ownerEntity))
	{
		if (childEntity != nullptr &&
			childEntity->skeletonHierarchyRoot &&
			childEntity->skeletonHierarchyOwnerEntity == ownerEntity)
		{
			existingRoots.push_back(childEntity);
		}
	}

SceneEntityBase* existingRoot = existingRoots.empty() ? nullptr : existingRoots.front();
	std::wstring preservedRootName;
	if (existingRoot != nullptr)
		preservedRootName = GetEntityName(existingRoot);
	DeleteSkeletonHierarchyForEntity(ownerEntity);

	const Witchcraft::Animation::SkeletonData* skeletonData = GetSkeletonData(ownerEntity);
	if (skeletonData == nullptr)
		return false;

	const bool useOwnerAsHierarchyContainer = ownerEntity->dedicatedSkeletonEntity;
	SceneEntityBase* hierarchyRoot = ownerEntity;
	if (!useOwnerAsHierarchyContainer)
	{
		const std::wstring rootName =
			existingRoot != nullptr
			? preservedRootName
			: GetUniqueEntityName(L"骨骼", ownerEntity);
		hierarchyRoot = CreateBasicEntity(rootName, ownerEntity, ComponentType::Co_Unk);
		if (hierarchyRoot == nullptr)
			return false;

		TagSkeletonHierarchyEntity(hierarchyRoot, ownerEntity, -1, true);
		RefreshEntityTypeTags(hierarchyRoot);
	}

	if (skeletonData->Topology.Bones.empty())
		return true;

	const Witchcraft::Animation::SkeletonTopology& topology = skeletonData->Topology;
	bool builtAnyBone = false;
	if (topology.IsValidBoneIndex(topology.RootBoneIndex))
	{
		builtAnyBone = BuildSkeletonBoneHierarchyRecursive(
			*this,
			ownerEntity,
			hierarchyRoot,
			topology,
			topology.RootBoneIndex,
			useOwnerAsHierarchyContainer);
	}

	for (std::int32_t boneIndex = 0; boneIndex < static_cast<std::int32_t>(topology.Bones.size()); ++boneIndex)
	{
		if (boneIndex == topology.RootBoneIndex)
			continue;

		const Witchcraft::Animation::SkeletonBone& bone = topology.Bones[static_cast<size_t>(boneIndex)];
		if (bone.ParentIndex >= 0 && topology.IsValidBoneIndex(bone.ParentIndex))
			continue;

		builtAnyBone = BuildSkeletonBoneHierarchyRecursive(
			*this,
			ownerEntity,
			hierarchyRoot,
			topology,
			boneIndex,
			useOwnerAsHierarchyContainer) || builtAnyBone;
	}

	if (!builtAnyBone)
		return false;

	return builtAnyBone;
}

bool WitchcraECS::RemoveAnimatorComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveAnimatorComponent(*this, entity);
}

bool WitchcraECS::RemoveSkinnedMeshComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveSkinnedMeshComponent(*this, entity);
}

bool WitchcraECS::RemoveSkinningRuntimeComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveSkinningRuntimeComponent(*this, entity);
}

bool WitchcraECS::RemoveRenderDrawSetComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveRenderDrawSetComponent(*this, entity);
}

bool WitchcraECS::RemoveBillboardComponent(SceneEntityBase* entity)
{
	return WitchcraECSComponentLifecycleBridge::RemoveBillboardComponent(*this, entity);
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

	if (mParentEntityIndex.find(entity) != mParentEntityIndex.end())
		return true;

	for (SceneEntityBase* rootEntity : entities)
	{
		if (ContainsEntity(rootEntity, entity))
			return true;
	}

	return false;
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

bool WitchcraECS::GetSelectedEntitySceneType(SceneEntityType* outType) const
{
	return GetEntitySceneType(selectedEntity, outType);
}

bool WitchcraECS::SetSelectedEntitySceneType(SceneEntityType type)
{
	return SetEntitySceneType(selectedEntity, type);
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
	return SetEntityPhysicsColliderSnapshot(selectedEntity, index, snapshot);
}

bool WitchcraECS::RemoveSelectedEntityPhysicsCollider(size_t index)
{
	return RemoveEntityPhysicsCollider(selectedEntity, index);
}

bool WitchcraECS::AddRigidbodyToSelectedEntity()
{
	return WitchcraECSInspectorBridge::AddRigidbodyToSelectedEntity(*this);
}

bool WitchcraECS::AddBoxColliderToSelectedEntity()
{
	return WitchcraECSPhysicsBridge::AddBoxColliderToSelectedEntity(*this);
}

bool WitchcraECS::AddPlaneColliderToSelectedEntity()
{
	return AddPlaneColliderToEntity(selectedEntity);
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
	SkinnedMeshComponent* skinnedMeshComponent = GetComponent<SkinnedMeshComponent>(entity);
	SkinningRuntimeComponent* skinningRuntimeComponent = GetComponent<SkinningRuntimeComponent>(entity);
	RenderDrawSetComponent* renderDrawSetComponent = GetComponent<RenderDrawSetComponent>(entity);
	if (skinnedMeshComponent != nullptr && skinningRuntimeComponent == nullptr)
	{
		for (SceneEntityBase* parentEntity = GetParentEntity(entity);
			parentEntity != nullptr && skinningRuntimeComponent == nullptr;
			parentEntity = GetParentEntity(parentEntity))
		{
			skinningRuntimeComponent = GetComponent<SkinningRuntimeComponent>(parentEntity);
		}
	}
	BillboardComponent* billboardComponent = GetComponent<BillboardComponent>(entity);
	if (meshComponent == nullptr && billboardComponent == nullptr && renderDrawSetComponent == nullptr)
		return false;

	outView->entity = entity;
	outView->meshComponent = meshComponent;
	outView->skinnedMeshComponent = skinnedMeshComponent;
	outView->skinningRuntimeComponent = skinningRuntimeComponent;
	outView->renderDrawSetComponent = renderDrawSetComponent;
	outView->billboardComponent = billboardComponent;
	if (meshComponent != nullptr)
		outView->renderItemName = meshComponent->GetMeshName();
	if (outView->renderItemName.empty())
		outView->renderItemName = GetEntityName(entity);

	if (meshComponent != nullptr)
	{
		outView->geometryName = meshComponent->GetGeometryName();
		outView->materialName = meshComponent->GetDefaultMaterialName();
		outView->renderLayerIndex = meshComponent->GetRenderLayerIndex();
	}
	else if (renderDrawSetComponent != nullptr && !renderDrawSetComponent->GetDraws().empty())
	{
		const RenderDrawSlice& firstDraw = renderDrawSetComponent->GetDraws().front();
		outView->geometryName = firstDraw.GeometryName;
		outView->materialName = firstDraw.MaterialName;
		outView->renderLayerIndex = firstDraw.RenderLayerIndex;
	}
	else
	{
		outView->materialName = billboardComponent->GetMaterialName();
		outView->renderLayerIndex = 透明物体渲染项目;
	}

	(void)GetEntitySceneType(entity, &outView->sceneEntityType);
	outView->visible = IsEntityVisible(entity);
	outView->isSkyEntity = outView->renderLayerIndex == 天空渲染项目;
	// 是否使用蒙皮顶点布局由组件归属决定，而不是由可选的资源路径决定。
	// .wmodel 的内嵌蒙皮几何可不生成单独 .wskin 文件，此时路径为空仍必须
	// 走 Skinned PSO 并上传骨骼 palette，否则会静默退回静态顶点着色器。
	outView->isSkinned =
		((meshComponent != nullptr && skinnedMeshComponent != nullptr) ||
			(renderDrawSetComponent != nullptr &&
				std::any_of(
					renderDrawSetComponent->GetDraws().begin(),
					renderDrawSetComponent->GetDraws().end(),
					[](const RenderDrawSlice& draw) { return draw.IsSkinned; })));
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

bool WitchcraECS::IsEntitySelfVisible(SceneEntityBase* entity) const
{
	if (entity == nullptr || entity->entity == 0 || mGeneralComponentDataId == 0)
		return true;

	const EntityGeneralComponentData* generalData = static_cast<const EntityGeneralComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mGeneralComponentDataId));
	return generalData == nullptr || generalData->visible;
}

bool WitchcraECS::IsEntityVisible(SceneEntityBase* entity) const
{
	if (!IsEntitySelfVisible(entity))
		return false;

	SceneEntityBase* parentEntity = GetParentEntity(entity);
	while (parentEntity != nullptr)
	{
		if (!IsEntitySelfVisible(parentEntity))
			return false;
		parentEntity = GetParentEntity(parentEntity);
	}

	return true;
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

bool WitchcraECS::GetEntitySceneType(SceneEntityBase* entity, SceneEntityType* outType) const
{
	if (outType == nullptr || entity == nullptr || entity->entity == 0 || mSceneTypeDataId == 0)
		return false;

	const EntitySceneTypeData* sceneTypeData = static_cast<const EntitySceneTypeData*>(
		ecs_get_id(entityWorld, entity->entity, mSceneTypeDataId));
	if (sceneTypeData == nullptr)
	{
		*outType = DetermineDefaultEntitySceneType(entity);
		return true;
	}

	*outType = SanitizeSceneEntityTypeValue(sceneTypeData->type);
	return true;
}

bool WitchcraECS::SetEntitySceneType(SceneEntityBase* entity, SceneEntityType type, bool applyVertexColor)
{
	if (entity == nullptr || entity->entity == 0 || mSceneTypeDataId == 0)
		return false;

	const SceneEntityType sanitizedType = SanitizeSceneEntityTypeValue(static_cast<std::uint32_t>(type));
	LogDebugMessage(
		L"[SceneType][Set] entity=%s ptr=%p type=%s apply=%d hasMesh=%d",
		entity->GetName().c_str(),
		entity,
		SceneEntityTypeToKey(sanitizedType),
		applyVertexColor ? 1 : 0,
		GetComponent<MeshComponent>(entity) != nullptr ? 1 : 0);
	EntitySceneTypeData sceneTypeData{};
	sceneTypeData.type = static_cast<std::uint32_t>(sanitizedType);
	entityWorld.entity(entity->entity).set<EntitySceneTypeData>(sceneTypeData);

	if (applyVertexColor)
		(void)ApplySceneTypeVertexColorToMeshEntity(entity);
	return true;
}

bool WitchcraECS::GetEntitySceneTypeVertexColor(SceneEntityType type, DirectX::XMFLOAT4* outColor) const
{
	if (outColor == nullptr)
		return false;

	*outColor = GetEntitySceneTypeVertexColor(type);
	return true;
}

DirectX::XMFLOAT4 WitchcraECS::GetEntitySceneTypeVertexColor(SceneEntityType type) const
{
	const SceneEntityType sanitizedType = SanitizeSceneEntityTypeValue(static_cast<std::uint32_t>(type));
	return mSceneEntityTypeVertexColors[static_cast<size_t>(sanitizedType)];
}

bool WitchcraECS::SetEntitySceneTypeVertexColor(SceneEntityType type, const DirectX::XMFLOAT4& color, bool applyToScene)
{
	const SceneEntityType sanitizedType = SanitizeSceneEntityTypeValue(static_cast<std::uint32_t>(type));
	mSceneEntityTypeVertexColors[static_cast<size_t>(sanitizedType)] = ClampColor(color);

	if (!applyToScene)
		return true;

	for (SceneEntityBase* rootEntity : entities)
		ApplySceneTypeVertexColorToSubtree(rootEntity);
	return true;
}

void WitchcraECS::ResetEntitySceneTypeVertexColorsToDefault(bool applyToScene)
{
	mSceneEntityTypeVertexColors = BuildDefaultSceneEntityTypeColors();
	if (!applyToScene)
		return;

	for (SceneEntityBase* rootEntity : entities)
		ApplySceneTypeVertexColorToSubtree(rootEntity);
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
	if (outFov == nullptr)
		return false;

	EntityCameraComponentData cameraData{};
	if (!GetEntityCameraSnapshot(entity, &cameraData))
		return false;

	*outFov = cameraData.fovY;
	return true;
}

bool WitchcraECS::SetEntityCameraFov(SceneEntityBase* entity, float fov)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return false;

	if (fov < 0.1f || fov > 1.0f)
		return false;

	EntityCameraComponentData cameraData{};
	GetEntityCameraSnapshot(entity, &cameraData);

	cameraData.fovY = fov;
	return SetEntityCameraSnapshot(entity, cameraData);
}

bool WitchcraECS::GetEntityCameraNear(SceneEntityBase* entity, float* outNearZ) const
{
	if (outNearZ == nullptr)
		return false;

	EntityCameraComponentData cameraData{};
	if (!GetEntityCameraSnapshot(entity, &cameraData))
		return false;

	*outNearZ = cameraData.nearZ;
	return true;
}

bool WitchcraECS::SetEntityCameraNear(SceneEntityBase* entity, float nearZ)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return false;

	EntityCameraComponentData cameraData{};
	GetEntityCameraSnapshot(entity, &cameraData);

	if (nearZ <= 0.0f || nearZ > cameraData.farZ)
		return false;

	cameraData.nearZ = nearZ;
	return SetEntityCameraSnapshot(entity, cameraData);
}

bool WitchcraECS::GetEntityCameraFar(SceneEntityBase* entity, float* outFarZ) const
{
	if (outFarZ == nullptr)
		return false;

	EntityCameraComponentData cameraData{};
	if (!GetEntityCameraSnapshot(entity, &cameraData))
		return false;

	*outFarZ = cameraData.farZ;
	return true;
}

bool WitchcraECS::SetEntityCameraFar(SceneEntityBase* entity, float farZ)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return false;

	EntityCameraComponentData cameraData{};
	GetEntityCameraSnapshot(entity, &cameraData);

	cameraData.farZ = farZ < cameraData.nearZ ? (cameraData.nearZ + 0.01f) : farZ;
	return SetEntityCameraSnapshot(entity, cameraData);
}

bool WitchcraECS::GetEntityCameraScale(SceneEntityBase* entity, float* outScale) const
{
	if (outScale == nullptr)
		return false;

	EntityCameraComponentData cameraData{};
	if (!GetEntityCameraSnapshot(entity, &cameraData))
		return false;

	*outScale = cameraData.viewportScale;
	return true;
}

bool WitchcraECS::SetEntityCameraScale(SceneEntityBase* entity, float scale)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return false;

	if (scale < 0.0f)
		return false;

	EntityCameraComponentData cameraData{};
	GetEntityCameraSnapshot(entity, &cameraData);

	cameraData.viewportScale = scale;
	return SetEntityCameraSnapshot(entity, cameraData);
}

bool WitchcraECS::RestoreEntityCameraScale(SceneEntityBase* entity)
{
	CameraComponent* cameraComponent = GetComponent<CameraComponent>(entity);
	if (cameraComponent == nullptr)
		return false;

	cameraComponent->RestoreScale();
	return true;
}

bool WitchcraECS::GetEntityCameraSnapshot(SceneEntityBase* entity, EntityCameraComponentData* outSnapshot) const
{
	if (entity == nullptr || entity->entity == 0 || outSnapshot == nullptr || mCameraComponentDataId == 0)
		return false;

	const EntityCameraComponentData* cameraData = static_cast<const EntityCameraComponentData*>(
		ecs_get_id(entityWorld, entity->entity, mCameraComponentDataId));
	if (cameraData == nullptr)
		return false;

	*outSnapshot = *cameraData;
	return true;
}

bool WitchcraECS::SetEntityCameraSnapshot(SceneEntityBase* entity, const EntityCameraComponentData& snapshot)
{
	if (entity == nullptr || entity->entity == 0 || mCameraComponentDataId == 0)
		return false;

	CameraComponent* cameraComponent = GetComponent<CameraComponent>(entity);
	if (cameraComponent == nullptr)
		cameraComponent = AddCameraComponent(entity);
	if (cameraComponent == nullptr)
		return false;

	cameraComponent->BindEntity(this, entity);

	EntityCameraComponentData sanitizedSnapshot = snapshot;
	sanitizedSnapshot.fovY = std::clamp(sanitizedSnapshot.fovY, 0.1f, 1.0f);
	sanitizedSnapshot.nearZ = (std::max)(sanitizedSnapshot.nearZ, 0.001f);
	sanitizedSnapshot.farZ = sanitizedSnapshot.farZ <= sanitizedSnapshot.nearZ
		? sanitizedSnapshot.nearZ + 0.01f
		: sanitizedSnapshot.farZ;
	sanitizedSnapshot.viewportScale = sanitizedSnapshot.viewportScale <= 0.0f
		? 1.0f
		: sanitizedSnapshot.viewportScale;

	entityWorld.entity(entity->entity).set<EntityCameraComponentData>(sanitizedSnapshot);
	return true;
}

bool WitchcraECS::TryBuildCameraRenderRequest(SceneEntityBase* entity, CameraRenderRequest* outRequest) const
{
	if (outRequest == nullptr)
		return false;

	*outRequest = CameraRenderRequest{};

	if (entity == nullptr ||
		entity->entity == 0 ||
		!IsEntityVisible(entity) ||
		GetComponent<CameraComponent>(entity) == nullptr)
	{
		return false;
	}

	EntityCameraComponentData cameraData{};
	if (!GetEntityCameraSnapshot(entity, &cameraData) ||
		!cameraData.renderEnabled ||
		!cameraData.renderToTextureEnabled ||
		cameraData.outputTargetId == 0)
	{
		return false;
	}

	Transform worldTransform{};
	if (!GetEntityWorldTransform(entity, &worldTransform))
		return false;
	DirectX::XMFLOAT4X4 worldMatrixData{};
	if (!GetEntityWorldMatrix(entity, &worldMatrixData))
		return false;

	EntityCameraComponentData sanitizedCameraData = cameraData;
	sanitizedCameraData.fovY = std::clamp(sanitizedCameraData.fovY, 0.1f, 1.0f);
	sanitizedCameraData.nearZ = (std::max)(sanitizedCameraData.nearZ, 0.001f);
	sanitizedCameraData.farZ = sanitizedCameraData.farZ <= sanitizedCameraData.nearZ
		? sanitizedCameraData.nearZ + 0.01f
		: sanitizedCameraData.farZ;
	sanitizedCameraData.viewportScale = sanitizedCameraData.viewportScale <= 0.0f
		? 1.0f
		: sanitizedCameraData.viewportScale;

	const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&worldTransform.position);
	const DirectX::XMMATRIX worldMatrix = DirectX::XMLoadFloat4x4(&worldMatrixData);
	const DirectX::XMVECTOR rawForward =
		DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), worldMatrix);
	const DirectX::XMVECTOR rawUp =
		DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), worldMatrix);
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(rawForward)) <= 1e-8f ||
		DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(rawUp)) <= 1e-8f)
	{
		return false;
	}
	const DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(rawForward);
	const DirectX::XMVECTOR up = DirectX::XMVector3Normalize(rawUp);

	const DirectX::XMMATRIX viewMatrix = DirectX::XMMatrixLookAtLH(eye, DirectX::XMVectorAdd(eye, forward), up);
	const DirectX::XMMATRIX projMatrix = DirectX::XMMatrixPerspectiveFovLH(
		sanitizedCameraData.fovY,
		sanitizedCameraData.viewportScale,
		sanitizedCameraData.nearZ,
		sanitizedCameraData.farZ);
	const DirectX::XMMATRIX viewProjMatrix = DirectX::XMMatrixMultiply(viewMatrix, projMatrix);

	CameraRenderRequest request;
	request.entity = entity;
	request.entityId = entity->entity;
	request.outputTargetId = sanitizedCameraData.outputTargetId;
	request.cameraData = sanitizedCameraData;
	request.worldTransform = worldTransform;
	request.positionWS = worldTransform.position;
	request.fovY = sanitizedCameraData.fovY;
	request.viewportScale = sanitizedCameraData.viewportScale;
	request.nearZ = sanitizedCameraData.nearZ;
	request.farZ = sanitizedCameraData.farZ;
	request.primary = sanitizedCameraData.primary;
	request.renderEnabled = sanitizedCameraData.renderEnabled;
	request.renderToTextureEnabled = sanitizedCameraData.renderToTextureEnabled;
	DirectX::XMStoreFloat3(&request.forwardWS, forward);
	DirectX::XMStoreFloat3(&request.upWS, up);
	DirectX::XMStoreFloat4x4(&request.view, viewMatrix);
	DirectX::XMStoreFloat4x4(&request.proj, projMatrix);
	DirectX::XMStoreFloat4x4(&request.viewProj, viewProjMatrix);

	*outRequest = request;
	return true;
}

std::vector<CameraRenderRequest> WitchcraECS::BuildCameraRenderRequests() const
{
	std::vector<CameraRenderRequest> requests;
	std::unordered_set<SceneEntityBase*> visitedEntities;

	const auto appendCameraRequest = [this, &requests](SceneEntityBase* entity)
	{
		CameraRenderRequest request;
		if (TryBuildCameraRenderRequest(entity, &request))
		{
			requests.push_back(request);
		}
	};

	std::function<void(SceneEntityBase*)> traverse;
	traverse = [&](SceneEntityBase* entity)
	{
		if (entity == nullptr)
			return;
		if (!visitedEntities.insert(entity).second)
			return;

		appendCameraRequest(entity);
		for (SceneEntityBase* childEntity : GetSceneChildren(entity))
			traverse(childEntity);
	};

	for (SceneEntityBase* rootEntity : GetSceneRootEntities())
		traverse(rootEntity);

	return requests;
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
	const bool supportsVolumetric = (lightKind == LightKind::Directional || lightKind == LightKind::Spot || lightKind == LightKind::Point);
	sanitizedSnapshot.castShadow =
		(lightKind == LightKind::Directional || lightKind == LightKind::Spot || lightKind == LightKind::Point) &&
		sanitizedSnapshot.castShadow;
	sanitizedSnapshot.enableVolumetric = supportsVolumetric && sanitizedSnapshot.enableVolumetric;
	sanitizedSnapshot.volumetricIntensity = supportsVolumetric
		? std::clamp(sanitizedSnapshot.volumetricIntensity, 0.0f, 8.0f)
		: 0.0f;
	sanitizedSnapshot.volumetricAttenuationDistance = supportsVolumetric
		? std::clamp(sanitizedSnapshot.volumetricAttenuationDistance, 0.1f, 500.0f)
		: 0.0f;
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
	const bool changed = WitchcraECSPhysicsBridge::SetEntityColliderSnapshot(*this, entity, index, snapshot);
	if (!changed)
		return false;

	if (snapshot.colliderType == static_cast<std::uint32_t>(PhysicsColliderType::Plane))
		RemoveRigidbodyComponent(entity);

	return true;
}

bool WitchcraECS::RemoveEntityPhysicsCollider(SceneEntityBase* entity, size_t index)
{
	PhysicsComponent* physicsComponent = GetComponent<PhysicsComponent>(entity);
	if (physicsComponent == nullptr)
		return false;

	return physicsComponent->RemoveCollider(index);
}

bool WitchcraECS::AddRigidbodyToEntity(SceneEntityBase* entity)
{
	if (HasPlaneColliderOnEntity(entity))
		return false;

	return AddComponent<RigidBodyComponent>(entity) != nullptr;
}

// 通过 ECS 语义接口为实体添加一个盒体碰撞器。
bool WitchcraECS::AddBoxColliderToEntity(SceneEntityBase* entity)
{
	return WitchcraECSPhysicsBridge::AddBoxColliderToEntity(*this, entity);
}

bool WitchcraECS::AddPlaneColliderToEntity(SceneEntityBase* entity)
{
	if (!WitchcraECSPhysicsBridge::AddPlaneColliderToEntity(*this, entity))
		return false;

	RemoveRigidbodyComponent(entity);
	return true;
}

bool WitchcraECS::HasPlaneColliderOnEntity(SceneEntityBase* entity) const
{
	return WitchcraECSPhysicsBridge::HasPlaneCollider(*this, entity);
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

	D3DWindow* dx = meshComponent->GetEngine() != nullptr ? meshComponent->GetEngine()->GetD3DWindow() : nullptr;
	if (dx != nullptr && !meshComponent->GetMeshName().empty())
	{
		dx->RebindRenderItemGeometry(
			meshComponent->GetMeshName(),
			meshComponent->GetObjectCollection(),
			geometryName);
	}

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

bool WitchcraECS::SetEntityEditableLocalTransform(SceneEntityBase* entity, const Transform& transform, bool syncImmediately)
{
	return WitchcraECSTransformSyncBridge::SetEntityEditableLocalTransform(*this, entity, transform, syncImmediately);
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
bool WitchcraECS::SelectEntityForHierarchy(SceneEntityBase* entity, bool additive)
{
	return WitchcraECSEntityHierarchyQueryBridge::SelectEntityForHierarchy(*this, entity, additive);
}

bool WitchcraECS::IsEntitySelectedInHierarchy(SceneEntityBase* entity) const
{
	return WitchcraECSEntityHierarchyQueryBridge::IsEntitySelectedInHierarchy(*this, entity);
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

std::vector<SceneEntityBase*> WitchcraECS::GetHierarchySelectionSnapshot() const
{
	return WitchcraECSEntityHierarchyQueryBridge::GetHierarchySelectionSnapshot(*this);
}

std::vector<SceneEntityBase*> WitchcraECS::GetHierarchySelectionRootSnapshot() const
{
	return WitchcraECSEntityHierarchyQueryBridge::GetHierarchySelectionRootSnapshot(*this);
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
	mHierarchySelectedEntities.clear();
	mEnvironmentEntity = nullptr;

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

	if (HasSkeletonData(entity) || IsSkeletonHierarchyEntity(entity))
	{
		ecs_add_id(entityWorld, entity->entity, mSkeletonEntityTypeTagId);
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
	if (mSkeletonEntityTypeTagId != 0)
		ecs_remove_id(entityWorld, entityId, mSkeletonEntityTypeTagId);
}

std::wstring WitchcraECS::BuildUniqueRenderItemName(D3DWindow* dx, const std::wstring& desiredName)
{
	const std::wstring baseName = desiredName.empty() ? L"RenderItem" : desiredName;
	if (dx == nullptr || dx->GetRenderItem(baseName) == nullptr)
		return baseName;

	UINT suffix = 1;
	while (true)
	{
		const std::wstring candidate = baseName + L"_" + std::to_wstring(suffix);
		if (dx->GetRenderItem(candidate) == nullptr)
			return candidate;
		++suffix;
	}
}

std::wstring WitchcraECS::BuildUniqueGeometryName(D3DWindow* dx, const std::wstring& desiredName)
{
	const std::wstring baseName = desiredName.empty() ? L"Geometry" : desiredName;
	if (dx == nullptr || dx->GetAggregateGraphicObj(baseName) == nullptr)
		return baseName;

	UINT suffix = 1;
	while (true)
	{
		const std::wstring candidate = baseName + L"_" + std::to_wstring(suffix);
		if (dx->GetAggregateGraphicObj(candidate) == nullptr)
			return candidate;
		++suffix;
	}
}

EntityLightComponentData WitchcraECS::BuildDefaultAmbientLightSnapshot()
{
	EntityLightComponentData lightData{};
	lightData.kind = static_cast<std::uint32_t>(LightKind::Ambient);
	lightData.type = 0.0f;
	lightData.color = DirectX::XMFLOAT3(0.45f, 0.45f, 0.45f);
	lightData.power = 0.35f;
	lightData.castShadow = false;
	lightData.enableVolumetric = false;
	lightData.volumetricIntensity = 0.0f;
	lightData.volumetricAttenuationDistance = 0.0f;
	return lightData;
}

bool WitchcraECS::TryLoadSkeletonTopologyFromAsset(const std::wstring& assetPath, Witchcraft::Animation::SkeletonTopology* outTopology)
{
	if (outTopology == nullptr || assetPath.empty())
		return false;

	WSkeletonFileData fileData;
	std::filesystem::path path(assetPath);
	if (!path.is_absolute())
		path = std::filesystem::path(EngineUtils::GetProjectDirPath()) / path;
	if (!WSkeletonFile::LoadFromFile(path, &fileData))
		return false;

	Witchcraft::Animation::SkeletonAsset skeletonAsset;
	skeletonAsset.GetTopology() = fileData.Topology;
	skeletonAsset.RebuildCaches();
	*outTopology = skeletonAsset.GetTopology();
	return true;
}

void WitchcraECS::CopySkeletonDataFromComponent(const SkeletonComponent& component, Witchcraft::Animation::SkeletonData* outData)
{
	if (outData == nullptr)
		return;

	outData->SkeletonAssetPath = component.GetSkeletonAssetPath();
	outData->Topology = component.GetTopology();
	outData->BoneNames = component.GetBoneNames();
	outData->LocalPose = component.GetLocalPose();
	outData->LocalMatrixPose = component.GetLocalMatrixPose();
	outData->GlobalPose = component.GetGlobalPose();
	outData->Dirty = component.IsDirty();
}

void WitchcraECS::CopySkeletonDataToComponent(const Witchcraft::Animation::SkeletonData& data, SkeletonComponent* outComponent)
{
	if (outComponent == nullptr)
		return;

	outComponent->SetSkeletonAssetPath(data.SkeletonAssetPath);
	outComponent->SetTopology(data.Topology);
	outComponent->SetBoneNames(data.BoneNames);
	outComponent->SetLocalPose(data.LocalPose);
	outComponent->SetLocalMatrixPose(data.LocalMatrixPose);
	outComponent->SetGlobalPose(data.GlobalPose);
	outComponent->SetDirty(data.Dirty);
}

DirectX::XMFLOAT3 WitchcraECS::QuaternionToEulerDegrees(const DirectX::XMFLOAT4& rotation)
{
	const DirectX::XMVECTOR normalized = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&rotation));
	DirectX::XMFLOAT4 value{};
	DirectX::XMStoreFloat4(&value, normalized);

	const float pitch = std::atan2(
		2.0f * (value.w * value.x + value.y * value.z),
		1.0f - 2.0f * (value.x * value.x + value.y * value.y));
	const float yaw = std::asin((std::max)(-1.0f, (std::min)(1.0f, 2.0f * (value.w * value.y - value.z * value.x))));
	const float roll = std::atan2(
		2.0f * (value.w * value.z + value.x * value.y),
		1.0f - 2.0f * (value.y * value.y + value.z * value.z));

	return DirectX::XMFLOAT3(
		DirectX::XMConvertToDegrees(pitch),
		DirectX::XMConvertToDegrees(yaw),
		DirectX::XMConvertToDegrees(roll));
}

Transform WitchcraECS::BuildTransformFromBoneLocalPose(const Witchcraft::Animation::BoneLocalPose& pose)
{
	Transform transform{};
	transform.position = pose.Translation;
	transform.rotation = QuaternionToEulerDegrees(pose.Rotation);
	transform.scale = pose.Scale;
	return transform;
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
