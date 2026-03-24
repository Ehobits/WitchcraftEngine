#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>

struct Transform
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT3 rotation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT3 scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	DirectX::BoundingBox boundingBox = DirectX::BoundingBox(
		DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
		DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
};
