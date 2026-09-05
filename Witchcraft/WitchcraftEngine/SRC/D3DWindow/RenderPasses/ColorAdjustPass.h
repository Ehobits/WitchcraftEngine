#pragma once

#include "../D3D12_framework.h"
#include "D3DPassContext.h"

// ColorAdjustPass：白平衡 / 对比度 / 饱和度后处理通道。
class ColorAdjustPass
{
public:
	void Initialize(ID3D12Device* device);

	void CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc);

	void Draw(const D3DPassContext& context);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

private:
	ComPtr<ID3D12Device> mDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;
};
