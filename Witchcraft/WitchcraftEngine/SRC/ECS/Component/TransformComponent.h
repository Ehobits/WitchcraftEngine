#pragma once

#include "D3DWindow/D3DWindow.h"
#include "Common/TransformSharedTypes.h"
#include "Engine/EngineUtils.h"
#include "HELPERS/Helpers.h"
#include "BaseComponent.h"

class TransformComponent : public BaseComponent
{
public:
	// 兼容旧写入口：
	// 正常编辑应优先通过 WitchcraECS 写入 flecs 中的 local/world transform。
	void SetBoundingBox(DirectX::BoundingBox boundingBox);
	DirectX::BoundingBox GetBoundingBox();
	void SetPosition3f(DirectX::XMFLOAT3 position);
	void SetPosition(float positionX, float positionY, float positionZ);
	void SetRotation3f(DirectX::XMFLOAT3 rotation);
	void SetRotation(float rotationX, float rotationY, float rotationZ);
	void SetScale3f(DirectX::XMFLOAT3 scale);
	void SetScale(float scaleX, float scaleY, float scaleZ);
	void SetTransform(Transform transform);

	// 这里做一次非负缩放保护，避免旧路径写入非法值。
	float ClampNonNegativeScale(float value);

	// 读取 ECS 已组合后的最终渲染变换缓存。
	DirectX::XMFLOAT3 GetPosition();
	DirectX::XMFLOAT3 GetRotation();
	DirectX::XMFLOAT3 GetScale();
	Transform GetTransform();

	// 读取 local transform 缓存，仅用于兼容旧代码。
	DirectX::XMFLOAT3 GetLocalPosition();
	DirectX::XMFLOAT3 GetLocalRotation();
	DirectX::XMFLOAT3 GetLocalScale();
	Transform GetLocalTransform();

	// 仅供 ECS 同步阶段回填缓存。
	void SyncLocalTransformCache(const Transform& transform);
	void SyncGlobalTransformCache(const Transform& transform);

	virtual ComponentType GetComponentType() { return mComponentType; }

private:
	Transform localTransform;
	Transform globalTransform;

	// 旧父级矩阵缓存，当前仅保留作兼容字段。
	DirectX::XMMATRIX parent = MathHelps::Identity;
	ComponentType mComponentType = ComponentType::Co_Transform;
};
