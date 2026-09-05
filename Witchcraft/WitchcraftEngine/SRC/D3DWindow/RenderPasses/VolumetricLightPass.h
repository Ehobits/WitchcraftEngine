#pragma once

#include "../D3D12_framework.h"
#include "../Light.h"
#include "D3DPassContext.h"

#include <functional>
#include <vector>

class VolumetricLightPass
{
public:
	static constexpr UINT MaxDrawCount = 256u;

	using PrepareDrawCallback = std::function<D3D12_GPU_VIRTUAL_ADDRESS(UINT drawIndex, UINT lightIndex)>;

	// ── 统一接口 ──
	void Initialize(ID3D12Device* device);

	bool CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc);

	void SetRenderData() {} // 无逐帧数据

	void Draw(
		const D3DPassContext& context,
		const std::vector<Light>& Lights,
		UINT shaderLightCount,
		float directionalLightTypeValue,
		float pointLightTypeValue,
		float spotLightTypeValue,
		const PrepareDrawCallback& prepareDrawCallback);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

private:
	ComPtr<ID3D12Device> md3dDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC mBasePsoDesc = {};
};
