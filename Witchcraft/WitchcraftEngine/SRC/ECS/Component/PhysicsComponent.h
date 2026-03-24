#pragma once

#include "System/PhysicsSystem.h"
#include "Engine/EngineUtils.h"
#include "BaseComponent.h"

#include <functional>

class TransformComponent;

struct PhysicsBoxColliderSnapshot
{
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
	void AddBoxCollider(TransformComponent* transformComponent);
	const std::vector<BoxColliderBuffer>& GetBoxColliders() const;
	const std::vector<PhysicsBoxColliderSnapshot>& GetColliderSnapshots() const;
	const PhysicsBoxColliderSnapshot* GetColliderSnapshot(size_t index) const;
	size_t GetBoxColliderCount() const;
	bool SetColliderSnapshot(size_t index, const PhysicsBoxColliderSnapshot& snapshot);
	void SetSyncCallback(std::function<void()> callback);

public:
	virtual ComponentType GetComponentType() { return mComponentType; }

private:
	void NotifyDataChanged();

private:
	ComponentType mComponentType = ComponentType::Co_Physics;
	std::vector<BoxColliderBuffer> box_colliders;
	std::vector<PhysicsBoxColliderSnapshot> mColliderSnapshots;
	std::function<void()> mSyncCallback;
};
