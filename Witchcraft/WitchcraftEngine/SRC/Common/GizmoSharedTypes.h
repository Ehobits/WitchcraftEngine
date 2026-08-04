#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

enum class GizmoMode : std::uint8_t
{
	None = 0,
	Translate = 1,
	Rotate = 2,
	Scale = 3
};

enum class GizmoHandle : std::uint8_t
{
	None = 0,
	AxisX,
	AxisY,
	AxisZ
};

struct GizmoVertex
{
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
	float HandleId = 0.0f;
};

struct GizmoSubmeshDesc
{
	GizmoHandle Handle = GizmoHandle::None;
	std::uint32_t IndexStart = 0;
	std::uint32_t IndexCount = 0;
	std::uint32_t BaseVertex = 0;
};

struct GizmoPickMeshData
{
	std::vector<GizmoVertex> Vertices;
	std::vector<std::uint32_t> Indices;
	std::vector<GizmoSubmeshDesc> Submeshes;
};

struct GizmoRenderData
{
	bool Visible = false;
	GizmoMode Mode = GizmoMode::Translate;
	GizmoHandle HoverHandle = GizmoHandle::None;
	GizmoHandle ActiveHandle = GizmoHandle::None;
	DirectX::XMFLOAT3 OriginWS = { 0.0f, 0.0f, 0.0f };
	float DrawScale = 1.0f;
};
