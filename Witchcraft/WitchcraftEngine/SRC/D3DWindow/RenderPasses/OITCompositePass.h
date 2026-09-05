#pragma once

#include "../D3D12_framework.h"
#include "D3DPassContext.h"

// OITCompositePass：Weighted Blended OIT 合成通道。
class OITCompositePass
{
public:
	void Initialize(ID3D12Device* device);

	bool CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc);

	void SetRenderData() {} // 全屏 Pass 无逐帧数据

	void Draw(const D3DPassContext& context);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

private:
	ComPtr<ID3D12Device> md3dDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;
};
