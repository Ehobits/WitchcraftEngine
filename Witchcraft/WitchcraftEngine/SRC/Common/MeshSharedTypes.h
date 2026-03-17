#pragma once

#include <cstdint>

#include <DirectXMath.h>

struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexC;
	DirectX::XMFLOAT3 Tangent;
	DirectX::XMFLOAT3 Bitangent;
};

