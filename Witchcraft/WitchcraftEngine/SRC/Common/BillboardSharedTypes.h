#pragma once

#include <cstdint>

#include <DirectXMath.h>

enum class BillboardMode : std::uint32_t
{
	WorldSize = 0,
	ScreenSize = 1
};

enum class BillboardFacingMode : std::uint32_t
{
	FaceCamera = 0,
	YAxisOnly = 1
};

struct BillboardData
{
	BillboardMode Mode = BillboardMode::WorldSize;
	BillboardFacingMode FacingMode = BillboardFacingMode::FaceCamera;
	float Width = 1.0f;
	float Height = 1.0f;
	float ScreenSize = 64.0f;
	DirectX::XMFLOAT3 Offset = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
};
