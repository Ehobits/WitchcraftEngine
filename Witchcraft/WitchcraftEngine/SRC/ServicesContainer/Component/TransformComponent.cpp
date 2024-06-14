#include "TransformComponent.h"
#include "GeneralComponent.h"
#include "RigidbodyComponent.h"
#include "PhysicsComponent.h"

void TransformComponent::SetBoundingBox(DirectX::BoundingBox boundingBox)
{
	localTransform.boundingBox = boundingBox;
}

DirectX::BoundingBox TransformComponent::GetBoundingBox()
{
	return globalTransform.boundingBox;
}

/* ------------------------------------------------------------ */

void TransformComponent::SetPosition3f(DirectX::XMFLOAT3 position)
{
	localTransform.position = position;
}

void TransformComponent::SetPosition(float positionX, float positionY, float positionZ)
{
	localTransform.position = DirectX::XMFLOAT3(positionX, positionY, positionZ);
}

void TransformComponent::SetRotation3f(DirectX::XMFLOAT3 rotation)
{
	localTransform.rotation = rotation;
}

void TransformComponent::SetRotation(float rotationX, float rotationY, float rotationZ)
{
	localTransform.rotation = DirectX::XMFLOAT3(rotationX, rotationY, rotationZ);
}

void TransformComponent::SetScale3f(DirectX::XMFLOAT3 scale)
{
	localTransform.scale = scale;
}

void TransformComponent::SetScale(float scaleX, float scaleY, float scaleZ)
{
	localTransform.scale = DirectX::XMFLOAT3(scaleX, scaleY, scaleZ);
}

void TransformComponent::SetTransform(Transform transform)
{
	localTransform = transform;
}

/* ------------------------------------------------------------ */

DirectX::XMFLOAT3 TransformComponent::GetPosition()
{
	return globalTransform.position;
}

DirectX::XMFLOAT3 TransformComponent::GetRotation()
{
	return globalTransform.rotation;
}

DirectX::XMFLOAT3 TransformComponent::GetScale()
{
	return globalTransform.scale;
}

Transform TransformComponent::GetTransform()
{
	return globalTransform;
}

/* ------------------------------------------------------------ */

DirectX::XMFLOAT3 TransformComponent::GetLocalPosition()
{
	return localTransform.position;
}

DirectX::XMFLOAT3 TransformComponent::GetLocalRotation()
{
	return localTransform.rotation;
}

DirectX::XMFLOAT3 TransformComponent::GetLocalScale()
{
	return localTransform.scale;
}

Transform TransformComponent::GetLocalTransform()
{
	return localTransform;
}
