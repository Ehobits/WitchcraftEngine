#pragma once

#include "System/PhysicsSystem.h"
#include "Engine/EngineUtils.h"
#include "ServicesContainer/ServicesContainer.h"
#include "BaseComponent.h"

struct PhysicsComponent : public BaseComponent
{
public:
	void AddBoxCollider(ServicesContainer* ComponentServices);

	const std::vector<BoxColliderBuffer>& GetBoxColliders();

public:
	ComponentType GetComponentType() { return mComponentType; }

public:
	ComponentType mComponentType = ComponentType::Co_Physics;

	std::vector<BoxColliderBuffer> box_colliders;
};