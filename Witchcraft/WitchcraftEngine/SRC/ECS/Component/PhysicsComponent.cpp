#include "PhysicsComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"

void PhysicsComponent::AddBoxCollider(TransformComponent* transformComponent)
{
	BoxColliderBuffer boxColliderBuffer;
	{
		boxColliderBuffer.CreateMaterial();
		boxColliderBuffer.CreateShape(transformComponent);
	}
	box_colliders.push_back(boxColliderBuffer);

	PhysicsBoxColliderSnapshot snapshot;
	snapshot.activeComponent = boxColliderBuffer.activeComponent;
	snapshot.staticFriction = boxColliderBuffer.staticFriction;
	snapshot.dynamicFriction = boxColliderBuffer.dynamicFriction;
	snapshot.restitution = boxColliderBuffer.restitution;
	snapshot.center = boxColliderBuffer.center;
	snapshot.size = boxColliderBuffer.size;
	mColliderSnapshots.push_back(snapshot);
	NotifyDataChanged();
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

bool PhysicsComponent::SetColliderSnapshot(size_t index, const PhysicsBoxColliderSnapshot& snapshot)
{
	if (index >= box_colliders.size() || index >= mColliderSnapshots.size())
		return false;

	if (snapshot.staticFriction < 0.0f || snapshot.dynamicFriction < 0.0f || snapshot.restitution < 0.0f)
		return false;

	BoxColliderBuffer& colliderBuffer = box_colliders[index];
	colliderBuffer.activeComponent = snapshot.activeComponent;
	colliderBuffer.SetStaticFriction(snapshot.staticFriction);
	colliderBuffer.SetDynamicFriction(snapshot.dynamicFriction);
	colliderBuffer.SetRestitution(snapshot.restitution);
	colliderBuffer.SetCenter(snapshot.center);
	colliderBuffer.SetSize(snapshot.size);

	mColliderSnapshots[index] = snapshot;
	NotifyDataChanged();
	return true;
}

void PhysicsComponent::SetSyncCallback(std::function<void()> callback)
{
	mSyncCallback = std::move(callback);
}

void PhysicsComponent::NotifyDataChanged()
{
	if (mSyncCallback)
		mSyncCallback();
}
