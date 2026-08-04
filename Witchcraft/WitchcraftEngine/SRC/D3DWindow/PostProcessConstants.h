#pragma once

#include "D3D12_framework.h"

// 后处理常量（FXAA / OIT composite 共用）
struct PostProcessConstants
{
	DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 __cbPostProcessPadRenderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT4 FxaaSettings = { 1.0f, 0.0312f, 0.125f, 8.0f };
};
