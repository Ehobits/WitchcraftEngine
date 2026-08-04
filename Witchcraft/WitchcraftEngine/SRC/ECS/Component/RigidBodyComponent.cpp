#include "RigidBodyComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"

#include <cmath>

void RigidBodyComponent::UpdateActor(ServicesContainer* ComponentServices)
{
}

void RigidBodyComponent::CreateActor(ServicesContainer* ComponentServices, PhysicsSystem* physicsSystem)
{
	(void)ComponentServices;
	(void)physicsSystem;
}

void RigidBodyComponent::SetMass(float value)
{
	if (!std::isfinite(value) || value < 0.0f) return;
	mMass = value;
	NotifyDataChanged();
}

float RigidBodyComponent::GetMass() const
{
	return mMass;
}

void RigidBodyComponent::SetLinearDamping(float value)
{
	if (!std::isfinite(value) || value < 0.0f) return;
	mLinearDamping = value;
	NotifyDataChanged();
}

float RigidBodyComponent::GetLinearDamping() const
{
	return mLinearDamping;
}

void RigidBodyComponent::SetAngularDamping(float value)
{
	if (!std::isfinite(value) || value < 0.0f) return;
	mAngularDamping = value;
	NotifyDataChanged();
}

float RigidBodyComponent::GetAngularDamping() const
{
	return mAngularDamping;
}

void RigidBodyComponent::UseGravity(bool value)
{
	mUseGravity = value;
	NotifyDataChanged();
}

bool RigidBodyComponent::HasUseGravity() const
{
	return mUseGravity;
}

void RigidBodyComponent::SetKinematic(bool value)
{
	mKinematic = value;
	NotifyDataChanged();
}

bool RigidBodyComponent::IsKinematic() const
{
	return mKinematic;
}

void RigidBodyComponent::AddForce(DirectX::XMFLOAT3 value)
{
	(void)value;
}

void RigidBodyComponent::AddTorque(DirectX::XMFLOAT3 value)
{
	(void)value;
}

void RigidBodyComponent::ClearForce()
{
}

void RigidBodyComponent::ClearTorque()
{
}

void RigidBodyComponent::SetLinearLockX(bool value)
{
	mLinearLockX = value;
	NotifyDataChanged();
}

bool RigidBodyComponent::GetLinearLockX() const
{
	return mLinearLockX;
}

void RigidBodyComponent::SetLinearLockY(bool value)
{
	mLinearLockY = value;
	NotifyDataChanged();
}

bool RigidBodyComponent::GetLinearLockY() const
{
	return mLinearLockY;
}

void RigidBodyComponent::SetLinearLockZ(bool value)
{
	mLinearLockZ = value;
	NotifyDataChanged();
}

bool RigidBodyComponent::GetLinearLockZ() const
{
	return mLinearLockZ;
}

void RigidBodyComponent::SetAngularLockX(bool value)
{
	mAngularLockX = value;
	NotifyDataChanged();
}

bool RigidBodyComponent::GetAngularLockX() const
{
	return mAngularLockX;
}

void RigidBodyComponent::SetAngularLockY(bool value)
{
	mAngularLockY = value;
	NotifyDataChanged();
}

bool RigidBodyComponent::GetAngularLockY() const
{
	return mAngularLockY;
}

void RigidBodyComponent::SetAngularLockZ(bool value)
{
	mAngularLockZ = value;
	NotifyDataChanged();
}

bool RigidBodyComponent::GetAngularLockZ() const
{
	return mAngularLockZ;
}

void RigidBodyComponent::ReleaseActor()
{
}

void RigidBodyComponent::SetPosition(DirectX::XMFLOAT3 xyz)
{
	(void)xyz;
}

void RigidBodyComponent::SetRotation(DirectX::XMFLOAT4 quat)
{
	(void)quat;
}

void RigidBodyComponent::SetSyncCallback(std::function<void()> callback)
{
	mSyncCallback = std::move(callback);
}

void RigidBodyComponent::NotifyDataChanged()
{
	if (mSyncCallback)
		mSyncCallback();
}
