#pragma once

#include "D3DWindow/D3DWindow.h"
#include "Engine/EngineUtils.h"
#include "BaseComponent.h"
#include "SYSTEM/PhysicsSystem.h"

#include <functional>

class ServicesContainer;

class RigidBodyComponent : public BaseComponent
{
public:
	void CreateActor(ServicesContainer* ComponentServices, PhysicsSystem* physicsSystem);
	void UpdateActor(ServicesContainer* ComponentServices);
	void ReleaseActor();

public:
	void SetMass(float value);
	float GetMass() const;
	void SetLinearDamping(float value);
	float GetLinearDamping() const;
	void SetAngularDamping(float value);
	float GetAngularDamping() const;
	void UseGravity(bool value);
	bool HasUseGravity() const;
	void SetKinematic(bool value);
	bool IsKinematic() const;
	void AddForce(DirectX::XMFLOAT3 value);
	void AddTorque(DirectX::XMFLOAT3 value);
	void ClearForce();
	void ClearTorque();
	void SetLinearLockX(bool value);
	bool GetLinearLockX() const;
	void SetLinearLockY(bool value);
	bool GetLinearLockY() const;
	void SetLinearLockZ(bool value);
	bool GetLinearLockZ() const;
	void SetAngularLockX(bool value);
	bool GetAngularLockX() const;
	void SetAngularLockY(bool value);
	bool GetAngularLockY() const;
	void SetAngularLockZ(bool value);
	bool GetAngularLockZ() const;
	void SetPosition(DirectX::XMFLOAT3 xyz);
	void SetRotation(DirectX::XMFLOAT4 quat);
	void SetSyncCallback(std::function<void()> callback);

public:
	virtual ComponentType GetComponentType() { return mComponentType; }

private:
	void NotifyDataChanged();

private:
	ComponentType mComponentType = ComponentType::Co_RigidBody;
	float mMass = 1.0f;
	float mLinearDamping = 0.0f;
	float mAngularDamping = 0.0f;
	bool mUseGravity = true;
	bool mKinematic = false;
	bool mLinearLockX = false;
	bool mLinearLockY = false;
	bool mLinearLockZ = false;
	bool mAngularLockX = false;
	bool mAngularLockY = false;
	bool mAngularLockZ = false;
	std::function<void()> mSyncCallback;
};
