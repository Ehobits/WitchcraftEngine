#pragma once

#include "D3DWindow/D3DWindow.h"
#include "Engine/EngineUtils.h"
#include "ServicesContainer/ServicesContainer.h"
#include "BaseComponent.h"
#include "SYSTEM/PhysicsSystem.h"

struct RigidBodyComponent : public BaseComponent
{
public:
	void CreateActor(ServicesContainer* ComponentServices, PhysicsSystem* physicsSystem);
	void UpdateActor(ServicesContainer* ComponentServices);
	void ReleaseActor();

public:
	void SetMass(float value);
	//float GetMass();
	//void SetLinearVelocity(DirectX::XMFLOAT3 value);
	//DirectX::XMFLOAT3 GetLinearVelocity();
	//void SetAngularVelocity(DirectX::XMFLOAT3 value);
	//DirectX::XMFLOAT3 GetAngularVelocity();
	void SetLinearDamping(float value);
	//float GetLinearDamping();
	void SetAngularDamping(float value);
	//float GetAngularDamping();
	void UseGravity(bool value);
	//bool HasUseGravity();
	void SetKinematic(bool value);
	//bool IsKinematic();
	void AddForce(DirectX::XMFLOAT3 value);
	void AddTorque(DirectX::XMFLOAT3 value);
	void ClearForce();
	void ClearTorque();
	void SetLinearLockX(bool value);
	//bool GetLinearLockX();
	void SetLinearLockY(bool value);
	//bool GetLinearLockY();
	void SetLinearLockZ(bool value);
	//bool GetLinearLockZ();
	void SetAngularLockX(bool value);
	//bool GetAngularLockX();
	void SetAngularLockY(bool value);
	//bool GetAngularLockY();
	void SetAngularLockZ(bool value);
	//bool GetAngularLockZ();
	void SetPosition(DirectX::XMFLOAT3 xyz);
	void SetRotation(DirectX::XMFLOAT4 quat);

public:
	ComponentType GetComponentType() { return mComponentType; }

private:
	ComponentType mComponentType = ComponentType::Co_RigidBody;
};