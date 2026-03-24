#include "TransformComponent.h"
#include "GeneralComponent.h"
#include "RigidbodyComponent.h"
#include "PhysicsComponent.h"

namespace
{
	// 这里继续集中做一次非负缩放保护，避免旧路径写入非法值。
	float ClampNonNegativeScale(float value)
	{
		return value < 0.0f ? 0.0f : value;
	}
}

void TransformComponent::SetBoundingBox(DirectX::BoundingBox boundingBox)
{
	// 包围盒仍跟随本地缓存一起维护；
	// 后续若包围盒也迁移到 ECS/flecs，可再统一收口。
	localTransform.boundingBox = boundingBox;
}

DirectX::BoundingBox TransformComponent::GetBoundingBox()
{
	return globalTransform.boundingBox;
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
	localTransform = transform;
}

void TransformComponent::SyncGlobalTransformCache(const Transform& transform)
{
	globalTransform = transform;
}
