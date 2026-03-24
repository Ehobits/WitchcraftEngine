#include "RigidBodyComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"

void RigidBodyComponent::UpdateActor(ServicesContainer* ComponentServices)
{
}

void RigidBodyComponent::CreateActor(ServicesContainer* ComponentServices, PhysicsSystem* physicsSystem)
{

}

void RigidBodyComponent::SetMass(float value)
{
	if (value < 0.0f) return;
	mMass = value;
	NotifyDataChanged();
	//pxRigidBody->setMass(value);
}

float RigidBodyComponent::GetMass() const
{
	return mMass;
}

void RigidBodyComponent::SetLinearDamping(float value)
{
	if (value < 0.0f) return;
	mLinearDamping = value;
	NotifyDataChanged();
	//pxRigidBody->setLinearDamping(value);
}

float RigidBodyComponent::GetLinearDamping() const
{
	return mLinearDamping;
}

void RigidBodyComponent::SetAngularDamping(float value)
{
	if (value < 0.0f) return;
	mAngularDamping = value;
	NotifyDataChanged();
	//pxRigidBody->setAngularDamping(value);
}

float RigidBodyComponent::GetAngularDamping() const
{
	return mAngularDamping;
}

void RigidBodyComponent::UseGravity(bool value)
{
	mUseGravity = value;
	NotifyDataChanged();
	//pxRigidBody->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !value);
}

bool RigidBodyComponent::HasUseGravity() const
{
	return mUseGravity;
}

void RigidBodyComponent::SetKinematic(bool value)
{
	mKinematic = value;
	NotifyDataChanged();
	//pxRigidBody->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, value);
}

bool RigidBodyComponent::IsKinematic() const
{
	return mKinematic;
}

void RigidBodyComponent::AddForce(DirectX::XMFLOAT3 value)
{
	//pxRigidBody->addForce(MathHelps::vector3_to_physics(value));
}

void RigidBodyComponent::AddTorque(DirectX::XMFLOAT3 value)
{
	//pxRigidBody->addTorque(MathHelps::vector3_to_physics(value));
}

void RigidBodyComponent::ClearForce()
{
	//pxRigidBody->clearForce();
}

void RigidBodyComponent::ClearTorque()
{
	//pxRigidBody->clearTorque();
}

void RigidBodyComponent::SetLinearLockX(bool value)
{
	mLinearLockX = value;
	NotifyDataChanged();
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_LINEAR_X, value);
}

bool RigidBodyComponent::GetLinearLockX() const
{
	return mLinearLockX;
}

void RigidBodyComponent::SetLinearLockY(bool value)
{
	mLinearLockY = value;
	NotifyDataChanged();
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_LINEAR_Y, value);
}

bool RigidBodyComponent::GetLinearLockY() const
{
	return mLinearLockY;
}

void RigidBodyComponent::SetLinearLockZ(bool value)
{
	mLinearLockZ = value;
	NotifyDataChanged();
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_LINEAR_Z, value);
}

bool RigidBodyComponent::GetLinearLockZ() const
{
	return mLinearLockZ;
}

void RigidBodyComponent::SetAngularLockX(bool value)
{
	mAngularLockX = value;
	NotifyDataChanged();
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_ANGULAR_X, value);
}

bool RigidBodyComponent::GetAngularLockX() const
{
	return mAngularLockX;
}

void RigidBodyComponent::SetAngularLockY(bool value)
{
	mAngularLockY = value;
	NotifyDataChanged();
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_ANGULAR_Y, value);
}

bool RigidBodyComponent::GetAngularLockY() const
{
	return mAngularLockY;
}

void RigidBodyComponent::SetAngularLockZ(bool value)
{
	mAngularLockZ = value;
	NotifyDataChanged();
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_ANGULAR_Z, value);
}

bool RigidBodyComponent::GetAngularLockZ() const
{
	return mAngularLockZ;
}

void RigidBodyComponent::ReleaseActor()
{
	//if (pxRigidBody) pxRigidBody->release();
}

void RigidBodyComponent::SetPosition(DirectX::XMFLOAT3 xyz)
{
	//physx::PxTransform tr = pxRigidBody->getGlobalPose();
	//tr.p.x = xyz.x;
	//tr.p.y = xyz.y;
	//tr.p.z = xyz.z;
	//pxRigidBody->setGlobalPose(tr);
}

void RigidBodyComponent::SetRotation(DirectX::XMFLOAT4 quat)
{
	//physx::PxTransform tr = pxRigidBody->getGlobalPose();
	//tr.q.x = quat.x;
	//tr.q.y = quat.y;
	//tr.q.z = quat.z;
	//tr.q.w = quat.w;
	//pxRigidBody->setGlobalPose(tr);
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
