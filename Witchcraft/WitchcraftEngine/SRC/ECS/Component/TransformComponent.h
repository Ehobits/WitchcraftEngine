#pragma once

#include "D3DWindow/D3DWindow.h"
#include "Engine/EngineUtils.h"
#include "HELPERS/Helpers.h"
#include "../ServicesContainer/ServicesContainer.h"
#include "BaseComponent.h"

struct TransformComponent : public BaseComponent
{
public:
	// 仅作缓存：
	// 1. localTransform 对应 flecs 中的 EntityLocalTransform 镜像
	// 2. globalTransform 对应 ECS 组合后的最终渲染变换镜像
	// 外部逻辑应优先通过 WitchcraECS 读写 Transform，而不是直接操作这里。
	Transform localTransform;
	Transform globalTransform;

public:
	// 兼容写接口：
	// 当前主要供 ECS 初始化/回退路径使用。
	// 正常编辑写入应走 WitchcraECS::SetEntityEditableLocalTransform。
	void SetBoundingBox(DirectX::BoundingBox boundingBox);
	DirectX::BoundingBox GetBoundingBox();
	void SetPosition3f(DirectX::XMFLOAT3 position);
	void SetPosition(float positionX, float positionY, float positionZ);
	void SetRotation3f(DirectX::XMFLOAT3 rotation);
	void SetRotation(float rotationX, float rotationY, float rotationZ);
	void SetScale3f(DirectX::XMFLOAT3 scale);
	void SetScale(float scaleX, float scaleY, float scaleZ);
	void SetTransform(Transform transform);

	// 最终渲染结果读取接口。
	// 这些值由 ECS 同步阶段回填，表示已组合父级后的结果。
	DirectX::XMFLOAT3    GetPosition();
	DirectX::XMFLOAT3    GetRotation();
	DirectX::XMFLOAT3    GetScale();
	Transform    GetTransform();

	// 局部变换缓存读取接口。
	// 仅用于兼容旧 UI/旧代码；新的外部读取应优先走
	// WitchcraECS::GetEntityEditableLocalTransform。
	DirectX::XMFLOAT3    GetLocalPosition();
	DirectX::XMFLOAT3    GetLocalRotation();
	DirectX::XMFLOAT3    GetLocalScale();
	Transform    GetLocalTransform();

	virtual ComponentType GetComponentType() { return mComponentType; }

private:
	// 旧父级矩阵缓存，目前未再承担主流程职责，先保留以兼容历史结构。
	DirectX::XMMATRIX parent = MathHelps::Identity;
	ComponentType mComponentType = ComponentType::Co_Transform;
};
