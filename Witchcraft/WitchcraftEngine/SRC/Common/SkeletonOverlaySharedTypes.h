#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

struct SkeletonOverlayVertex
{
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct SkeletonOverlayRenderData
{
	bool Visible = false;
	bool XRay = true;
	std::vector<SkeletonOverlayVertex> Vertices;
	std::vector<std::uint32_t> Indices;
};
