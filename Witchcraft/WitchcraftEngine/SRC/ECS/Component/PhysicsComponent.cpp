#include "PhysicsComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"

#include <cmath>

void PhysicsComponent::AddCollider(TransformComponent* transformComponent, PhysicsColliderType colliderType)
{
	// 1) 构造运行时碰撞器参数。
	BoxColliderBuffer boxColliderBuffer;
	{
		boxColliderBuffer.colliderType = colliderType;
		// 平面碰撞器使用更大的默认尺寸，便于直接作为“地面”使用。
		if (colliderType == PhysicsColliderType::Plane)
			boxColliderBuffer.size = DirectX::XMFLOAT3(10.0f, 0.0f, 10.0f);
		boxColliderBuffer.CreateMaterial();
		boxColliderBuffer.CreateShape(transformComponent);
	}
	box_colliders.push_back(boxColliderBuffer);

	// 2) 同步生成一份可编辑快照（Inspector/序列化路径使用）。
	PhysicsBoxColliderSnapshot snapshot;
	snapshot.colliderType = static_cast<std::uint32_t>(boxColliderBuffer.colliderType);
	snapshot.activeComponent = boxColliderBuffer.activeComponent;
	snapshot.staticFriction = boxColliderBuffer.staticFriction;
	snapshot.dynamicFriction = boxColliderBuffer.dynamicFriction;
	snapshot.restitution = boxColliderBuffer.restitution;
	snapshot.center = boxColliderBuffer.center;
	snapshot.size = boxColliderBuffer.size;
	mColliderSnapshots.push_back(snapshot);
	NotifyDataChanged();
}

void PhysicsComponent::AddBoxCollider(TransformComponent* transformComponent)
{
	AddCollider(transformComponent, PhysicsColliderType::Box);
}

void PhysicsComponent::AddPlaneCollider(TransformComponent* transformComponent)
{
	AddCollider(transformComponent, PhysicsColliderType::Plane);
}

const std::vector<BoxColliderBuffer>& PhysicsComponent::GetBoxColliders() const
{
	return box_colliders;
}

const std::vector<PhysicsBoxColliderSnapshot>& PhysicsComponent::GetColliderSnapshots() const
{
	return mColliderSnapshots;
}

const PhysicsBoxColliderSnapshot* PhysicsComponent::GetColliderSnapshot(size_t index) const
{
	if (index >= mColliderSnapshots.size())
		return nullptr;

	return &mColliderSnapshots[index];
}

size_t PhysicsComponent::GetBoxColliderCount() const
{
	return box_colliders.size();
}

bool PhysicsComponent::HasPlaneCollider() const
{
	for (const PhysicsBoxColliderSnapshot& snapshot : mColliderSnapshots)
	{
		if (snapshot.activeComponent &&
			snapshot.colliderType == static_cast<std::uint32_t>(PhysicsColliderType::Plane))
			return true;
	}

	return false;
}

bool PhysicsComponent::RemoveCollider(size_t index)
{
	if (index >= box_colliders.size() || index >= mColliderSnapshots.size())
		return false;

	box_colliders.erase(box_colliders.begin() + static_cast<std::vector<BoxColliderBuffer>::difference_type>(index));
	mColliderSnapshots.erase(mColliderSnapshots.begin() + static_cast<std::vector<PhysicsBoxColliderSnapshot>::difference_type>(index));
	NotifyDataChanged();
	return true;
}

bool PhysicsComponent::SetColliderSnapshot(size_t index, const PhysicsBoxColliderSnapshot& snapshot)
{
	if (index >= box_colliders.size() || index >= mColliderSnapshots.size())
		return false;

	const auto isFiniteFloat = [](float value)
	{
		return std::isfinite(value);
	};
	const auto isFiniteFloat3 = [&](const DirectX::XMFLOAT3& value)
	{
		return isFiniteFloat(value.x) && isFiniteFloat(value.y) && isFiniteFloat(value.z);
	};

	// 简单参数校验：物理材质参数不接受负值。
	if (!isFiniteFloat(snapshot.staticFriction) ||
		!isFiniteFloat(snapshot.dynamicFriction) ||
		!isFiniteFloat(snapshot.restitution) ||
		!isFiniteFloat3(snapshot.center) ||
		!isFiniteFloat3(snapshot.size) ||
		snapshot.staticFriction < 0.0f ||
		snapshot.dynamicFriction < 0.0f ||
		snapshot.restitution < 0.0f)
	{
		return false;
	}

	BoxColliderBuffer& colliderBuffer = box_colliders[index];
	// 仅接受 Box / Plane 两种类型；非法值回退为 Box。
	const PhysicsColliderType colliderType = snapshot.colliderType == static_cast<std::uint32_t>(PhysicsColliderType::Plane)
		? PhysicsColliderType::Plane
		: PhysicsColliderType::Box;
	colliderBuffer.colliderType = colliderType;
	colliderBuffer.activeComponent = snapshot.activeComponent;
	colliderBuffer.SetStaticFriction(snapshot.staticFriction);
	colliderBuffer.SetDynamicFriction(snapshot.dynamicFriction);
	colliderBuffer.SetRestitution(snapshot.restitution);
	colliderBuffer.SetCenter(snapshot.center);
	colliderBuffer.SetSize(snapshot.size);

	mColliderSnapshots[index] = snapshot;
	mColliderSnapshots[index].colliderType = static_cast<std::uint32_t>(colliderType);
	NotifyDataChanged();
	return true;
}

void PhysicsComponent::SetSyncCallback(std::function<void()> callback)
{
	mSyncCallback = std::move(callback);
}

void PhysicsComponent::NotifyDataChanged()
{
	// 统一变更出口，便于外部集中处理“重建刚体/刷新碰撞体”。
	if (mSyncCallback)
		mSyncCallback();
}
