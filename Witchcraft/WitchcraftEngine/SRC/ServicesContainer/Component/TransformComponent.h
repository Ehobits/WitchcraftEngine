#pragma once

#include "D3DWindow/D3DWindow.h"
#include "Engine/EngineUtils.h"
#include "HELPERS/Helpers.h"
#include "ServicesContainer/ServicesContainer.h"
#include "BaseComponent.h"

struct TransformComponent : public BaseComponent
{
public:
	Transform localTransform; // 局部变换
	Transform globalTransform; // 全局变换

private:
	// 父级的矩阵（如果没有父级就一直用默认矩阵）
	DirectX::XMMATRIX parent = MathHelps::Identity;

public:
	void SetBoundingBox(DirectX::BoundingBox boundingBox);
	DirectX::BoundingBox GetBoundingBox();

public:
	void SetPosition3f(DirectX::XMFLOAT3 position);
	void SetPosition(float positionX, float positionY, float positionZ);
	void SetRotation3f(DirectX::XMFLOAT3 rotation);
	void SetRotation(float rotationX, float rotationY, float rotationZ);
	void SetScale3f(DirectX::XMFLOAT3 scale);
	void SetScale(float scaleX, float scaleY, float scaleZ);
	void SetTransform(Transform transform);

public:
	DirectX::XMFLOAT3    GetPosition();
	DirectX::XMFLOAT3    GetRotation();
	DirectX::XMFLOAT3    GetScale();
	Transform    GetTransform();

public:
	DirectX::XMFLOAT3    GetLocalPosition();
	DirectX::XMFLOAT3    GetLocalRotation();
	DirectX::XMFLOAT3    GetLocalScale();
	Transform    GetLocalTransform();
};