#include "PhysicsComponent.h"
#include "ServicesContainer/COMPONENT/RigidbodyComponent.h"

void PhysicsComponent::AddBoxCollider(ServicesContainer* ComponentServices)
{
	BoxColliderBuffer boxColliderBuffer;
	{
		boxColliderBuffer.CreateMaterial();
		boxColliderBuffer.CreateShape(ComponentServices);

		//if (ComponentServices->registry.any_of<RigidBodyComponent>(entity))
		{
			//auto& rigidBodyComponent = ComponentServices->registry.get<RigidBodyComponent>(entity);
			//if (rigidBodyComponent.GetRigidBody())
			//	rigidBodyComponent.GetRigidBody()->attachShape(*boxColliderBuffer.GetShape());
		}
	}
	box_colliders.push_back(boxColliderBuffer);
}

const std::vector<BoxColliderBuffer>& PhysicsComponent::GetBoxColliders()
{
	return box_colliders;
}
