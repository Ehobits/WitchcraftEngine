#include "TransformComponent.h"
#include "GeneralComponent.h"
#include "RigidbodyComponent.h"
#include "PhysicsComponent.h"

void TransformComponent::SetBoundingBox(DirectX::BoundingBox boundingBox)
{
	// BoundingBox 由 MeshComponent 根据 mesh 本地顶点生成，当前仍是 TransformComponent 的兼容缓存，
	// 不是 flecs Local/WorldTransform 的权威数据。这里同时写 local/global，避免渲染重建后
	// GetBoundingBox() 读到未同步的默认盒子。
	localTransform.boundingBox = boundingBox;
	globalTransform.boundingBox = boundingBox;
}

DirectX::BoundingBox TransformComponent::GetBoundingBox()
{
	// 当前渲染侧把该返回值作为“mesh 本地包围盒”，随后再用世界矩阵 Transform 到世界空间。
	return localTransform.boundingBox;
}

/* ------------------------------------------------------------ */

void TransformComponent::SetPosition3f(DirectX::XMFLOAT3 position)
{
	// 兼容写入口：仅更新本地缓存，不负责触发 ECS 同步。
	localTransform.position = position;
}

void TransformComponent::SetPosition(float positionX, float positionY, float positionZ)
{
	localTransform.position = DirectX::XMFLOAT3(positionX, positionY, positionZ);
}

void TransformComponent::SetRotation3f(DirectX::XMFLOAT3 rotation)
{
	// 兼容写入口：仅更新本地缓存，不负责触发 ECS 同步。
	localTransform.rotation = rotation;
}

void TransformComponent::SetRotation(float rotationX, float rotationY, float rotationZ)
{
	localTransform.rotation = DirectX::XMFLOAT3(rotationX, rotationY, rotationZ);
}

void TransformComponent::SetScale3f(DirectX::XMFLOAT3 scale)
{
	// 兼容写入口：仅更新本地缓存，缩放仍强制保持非负。
	localTransform.scale = DirectX::XMFLOAT3(
		ClampNonNegativeScale(scale.x),
		ClampNonNegativeScale(scale.y),
		ClampNonNegativeScale(scale.z));
}

void TransformComponent::SetScale(float scaleX, float scaleY, float scaleZ)
{
	localTransform.scale = DirectX::XMFLOAT3(
		ClampNonNegativeScale(scaleX),
		ClampNonNegativeScale(scaleY),
		ClampNonNegativeScale(scaleZ));
}

void TransformComponent::SetTransform(Transform transform)
{
	// 兼容写入口：外部正常写入应优先走 WitchcraECS。
	transform.scale.x = ClampNonNegativeScale(transform.scale.x);
	transform.scale.y = ClampNonNegativeScale(transform.scale.y);
	transform.scale.z = ClampNonNegativeScale(transform.scale.z);
	localTransform = transform;
}

float TransformComponent::ClampNonNegativeScale(float value)
{
	return value < 0.0f ? 0.0f : value;
}

/* ------------------------------------------------------------ */

DirectX::XMFLOAT3 TransformComponent::GetPosition()
{
	// 读取 ECS 已组合完成的最终渲染变换缓存。
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
	// 读取 ECS 已组合完成的最终渲染变换缓存。
	return globalTransform;
}

/* ------------------------------------------------------------ */

DirectX::XMFLOAT3 TransformComponent::GetLocalPosition()
{
	// 读取局部变换缓存；优先用于兼容旧代码。
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
	// 读取局部变换缓存；新的外部读取应优先走 WitchcraECS。
	return localTransform;
}

void TransformComponent::SyncLocalTransformCache(const Transform& transform)
{
	const DirectX::BoundingBox cachedBoundingBox = localTransform.boundingBox;
	localTransform = transform;
	localTransform.boundingBox = cachedBoundingBox;
}

void TransformComponent::SyncGlobalTransformCache(const Transform& transform)
{
	const DirectX::BoundingBox cachedBoundingBox = globalTransform.boundingBox;
	globalTransform = transform;
	globalTransform.boundingBox = cachedBoundingBox;
}
