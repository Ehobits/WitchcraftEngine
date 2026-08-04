#pragma once

#include "System/PhysicsSystem.h"
#include "Engine/EngineUtils.h"
#include "BaseComponent.h"

#include <functional>

class TransformComponent;

struct PhysicsBoxColliderSnapshot
{
	// 用于序列化/Inspector 编辑的轻量快照（不直接持有 Jolt 运行时对象）。
	std::uint32_t colliderType = static_cast<std::uint32_t>(PhysicsColliderType::Box);
	bool activeComponent = true;
	float staticFriction = PhysicsDefaultMaterial::StaticFriction;
	float dynamicFriction = PhysicsDefaultMaterial::DynamicFriction;
	float restitution = PhysicsDefaultMaterial::Restitution;
	DirectX::XMFLOAT3 center = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT3 size = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
};

class PhysicsComponent : public BaseComponent
{
public:
	// 统一入口：按类型新增碰撞器，并同步生成一份可编辑快照。
	void AddCollider(TransformComponent* transformComponent, PhysicsColliderType colliderType);
	void AddBoxCollider(TransformComponent* transformComponent);
	void AddPlaneCollider(TransformComponent* transformComponent);

	// 运行时碰撞器数据（供物理系统读取）。
	const std::vector<BoxColliderBuffer>& GetBoxColliders() const;
	// 编辑器快照数据（供 Inspector/UI 读取与编辑）。
	const std::vector<PhysicsBoxColliderSnapshot>& GetColliderSnapshots() const;
	const PhysicsBoxColliderSnapshot* GetColliderSnapshot(size_t index) const;
	size_t GetBoxColliderCount() const;
	bool HasPlaneCollider() const;
	bool RemoveCollider(size_t index);
	// 将 UI 快照写回运行时碰撞器参数；成功后会触发同步回调。
	bool SetColliderSnapshot(size_t index, const PhysicsBoxColliderSnapshot& snapshot);

	// 数据变更通知：由外部（通常是 ECS/PhysicsSystem）注册。
	void SetSyncCallback(std::function<void()> callback);

public:
	virtual ComponentType GetComponentType() { return mComponentType; }

private:
	// 当碰撞器数据被新增/修改时通知外部重建或刷新物理体。
	void NotifyDataChanged();

private:
	ComponentType mComponentType = ComponentType::Co_Physics;
	// 真正参与物理模拟的碰撞器参数集合。
	std::vector<BoxColliderBuffer> box_colliders;
	// 与 box_colliders 一一对应的编辑快照。
	std::vector<PhysicsBoxColliderSnapshot> mColliderSnapshots;
	std::function<void()> mSyncCallback;
};
