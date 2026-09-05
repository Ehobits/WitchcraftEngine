#pragma once

#include "D3D12_framework.h"

// SSAO 常量
struct AOConstants
{
	DirectX::XMFLOAT4X4 Proj;
	DirectX::XMFLOAT4X4 InvProj;
	DirectX::XMFLOAT4X4 InvView;
	DirectX::XMFLOAT4X4 ProjTex;
	DirectX::XMFLOAT4   OffsetVectors[14];

	// 供 SsaoBlur.hlsl 使用
	DirectX::XMFLOAT4 BlurWeights[3];
	DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };

	// 坐标位于观察空间。
	float OcclusionRadius = 0.5f;
	float OcclusionFadeStart = 0.2f;
	float OcclusionFadeEnd = 2.0f;
	float SurfaceEpsilon = 0.05f;
	float ProjScaleX = 1.0f;
	float ProjScaleY = 1.0f;
	float ProjDepthA = 1.0f;
	float ProjDepthB = 1.0f;
};
