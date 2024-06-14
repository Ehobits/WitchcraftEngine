#include "RigidBodyComponent.h"
#include "ServicesContainer/COMPONENT/TransformComponent.h"
#include "ServicesContainer/COMPONENT/PhysicsComponent.h"

void RigidBodyComponent::UpdateActor(ServicesContainer* ComponentServices)
{
	//entt::entity entity = entt::to_entity(ComponentServices->registry, *this);

	//if (pxRigidBody)
	//{
	//	if (ComponentServices->HasComponent<TransformComponent>(entity))
	//	{
	//		physx::PxTransform pxTransform = pxRigidBody->getGlobalPose();
	//		auto& transformComponent = ComponentServices->GetComponent<TransformComponent>(entity);
	//		transformComponent.SetPosition3f(MathHelps::physics_to_vector3(pxTransform.p));
	//		transformComponent.SetRotation(MathHelps::physics_to_quat(pxTransform.q).x, MathHelps::physics_to_quat(pxTransform.q).y, MathHelps::physics_to_quat(pxTransform.q).z);
	//	}
	//}
}

void RigidBodyComponent::CreateActor(ServicesContainer* ComponentServices, PhysicsSystem* physicsSystem)
{
	//if (pxRigidBody) pxRigidBody->release();
	//entt::entity entity = entt::to_entity(ComponentServices->registry, *this);
	//if (ComponentServices->HasComponent<TransformComponent>(entity))
	{
		//auto& transformComponent = ComponentServices->GetComponent<TransformComponent>(entity);
		//physx::PxTransform pxTransform = physx::PxTransform(
		//	MathHelps::vector3_to_physics(transformComponent.GetPosition()),
		//	MathHelps::quat_to_physics(DirectX::XMFLOAT4(transformComponent.GetRotation().x, transformComponent.GetRotation().y, transformComponent.GetRotation().z, 1.0f)));
		//pxRigidBody = physicsSystem->GetPhysics()->createRigidDynamic(pxTransform);
		//if (!pxRigidBody) consoleWindow->AddWarningMessage(L"[RigidBody] -> Failed to create the RigidBody!");
		//physicsSystem->GetScene()->addActor(*pxRigidBody); /*<*>*/

		//if (ComponentServices->HasComponent<PhysicsComponent>(entity))
		{
			//auto& physicsComponent = ComponentServices->GetComponent<PhysicsComponent>(entity);
			//for (size_t i = 0; i < physicsComponent.GetBoxColliders().size(); i++)
				//if (physicsComponent.GetBoxColliders()[i].GetShape())
				//	pxRigidBody->attachShape(*physicsComponent.GetBoxColliders()[i].GetShape());
				//else
				//	consoleWindow->AddWarningMessage(L"[RigidBody] -> Failed to attach shape to the RigidBody!");
		}
	}
}

void RigidBodyComponent::SetMass(float value)
{
	if (value < 0.0f) return;
	//pxRigidBody->setMass(value);
}
//float RigidBodyComponent::GetMass()
//{
//	return pxRigidBody->getMass();
//}
//void RigidBodyComponent::SetLinearVelocity(DirectX::XMFLOAT3 value)
//{
//	pxRigidBody->setLinearVelocity(MathHelps::vector3_to_physics(value));
//}
//DirectX::XMFLOAT3 RigidBodyComponent::GetLinearVelocity()
//{
//	return MathHelps::physics_to_vector3(pxRigidBody->getLinearVelocity());
//}
void RigidBodyComponent::SetLinearDamping(float value)
{
	if (value < 0.0f) return;
	//pxRigidBody->setLinearDamping(value);
}
//float RigidBodyComponent::GetLinearDamping()
//{
//	return pxRigidBody->getLinearDamping();
//}
void RigidBodyComponent::SetAngularDamping(float value)
{
	if (value < 0.0f) return;
	//pxRigidBody->setAngularDamping(value);
}
//float RigidBodyComponent::GetAngularDamping()
//{
//	return pxRigidBody->getAngularDamping();
//}
//void RigidBodyComponent::SetAngularVelocity(DirectX::XMFLOAT3 value)
//{
//	pxRigidBody->setAngularVelocity(MathHelps::vector3_to_physics(value));
//}
//DirectX::XMFLOAT3 RigidBodyComponent::GetAngularVelocity()
//{
//	return MathHelps::physics_to_vector3(pxRigidBody->getAngularVelocity());
//}
void RigidBodyComponent::UseGravity(bool value)
{
	//pxRigidBody->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !value);
}
//bool RigidBodyComponent::HasUseGravity()
//{
//	return !pxRigidBody->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_GRAVITY);
//}
void RigidBodyComponent::SetKinematic(bool value)
{
	//pxRigidBody->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, value);
}
//bool RigidBodyComponent::IsKinematic()
//{
//	return pxRigidBody->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC);
//}
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
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_LINEAR_X, value);
}
//bool RigidBodyComponent::GetLinearLockX()
//{
//	return GetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_LINEAR_X);
//}
void RigidBodyComponent::SetLinearLockY(bool value)
{
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_LINEAR_Y, value);
}
//bool RigidBodyComponent::GetLinearLockY()
//{
//	return GetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_LINEAR_Y);
//}
void RigidBodyComponent::SetLinearLockZ(bool value)
{
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_LINEAR_Z, value);
}
//bool RigidBodyComponent::GetLinearLockZ()
//{
//	return GetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_LINEAR_Z);
//}
void RigidBodyComponent::SetAngularLockX(bool value)
{
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_ANGULAR_X, value);
}
//bool RigidBodyComponent::GetAngularLockX()
//{
//	return GetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_ANGULAR_X);
//}
void RigidBodyComponent::SetAngularLockY(bool value)
{
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_ANGULAR_Y, value);
}
//bool RigidBodyComponent::GetAngularLockY()
//{
//	return GetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_ANGULAR_Y);
//}
void RigidBodyComponent::SetAngularLockZ(bool value)
{
	//SetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_ANGULAR_Z, value);
}
//bool RigidBodyComponent::GetAngularLockZ()
//{
//	return GetLock(physx::PxRigidDynamicLockFlag::Enum::eLOCK_ANGULAR_Z);
//}
//void RigidBodyComponent::SetLock(physx::PxRigidDynamicLockFlag::Enum flag, bool value)
//{
//	return;
//}
//bool RigidBodyComponent::GetLock(physx::PxRigidDynamicLockFlag::Enum flag)
//{
//	return false;
//}
//void RigidBodyComponent::SetTransform(physx::PxTransform value)
//{
//	pxRigidBody->setGlobalPose(value);
//}
//physx::PxTransform RigidBodyComponent::GetTransform()
//{
//	return pxRigidBody->getGlobalPose();
//}
void RigidBodyComponent::ReleaseActor()
{
	//if (pxRigidBody) pxRigidBody->release();
}
//physx::PxRigidBody* RigidBodyComponent::GetRigidBody()
//{
//	return pxRigidBody;
//}

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