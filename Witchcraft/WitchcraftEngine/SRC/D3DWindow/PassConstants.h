#pragma once

#include "D3D12_framework.h"
#include "Helpers/MathHelpers.h"
#include "Common/TransformSharedTypes.h"

// 通道常量 —— 被所有渲染 Pass 共享的主 Pass CB。
struct PassConstants
{
	DirectX::XMFLOAT4X4 View = MathHelps::Identity;
	DirectX::XMFLOAT4X4 InvView = MathHelps::Identity;
	DirectX::XMFLOAT4X4 Proj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 InvProj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 ViewProj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 InvViewProj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 ViewProjTex = MathHelps::Identity;
	DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	float cbPerObjectPad0 = 0.0f;
	DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 __cbPassPadRenderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 ShadowTransform[256] = { MathHelps::Identity };
	DirectX::XMFLOAT2 ShadowSettings = { 0.65f, 1.5f };
	DirectX::XMFLOAT2 AOSettings = { 1.0f, 1.0f };
	DirectX::XMFLOAT4 ShadowMaskSettings = { 1.0f, 0.0f, 2.0f, 3.0f };
	DirectX::XMFLOAT4 DirectionalShadowCascadeSplits = { 8.0f, 24.0f, 72.0f, 256.0f };
	DirectX::XMFLOAT4 DirectionalShadowCascadeSettings = { 4.0f, 0.08f, 0.0f, 0.0f };
	DirectX::XMFLOAT4 DirectionalShadowCascadeWorldTexelSize = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT4 DirectionalShadowCascadeDepthScale = { 1.0f, 1.0f, 1.0f, 1.0f };
	UINT LightConst = 0;
	DirectX::XMFLOAT3 __cbPassPad001 = { 0.0f, 0.0f, 0.0f };
};
