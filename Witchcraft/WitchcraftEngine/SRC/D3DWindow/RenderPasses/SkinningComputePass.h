#pragma once

#include "../D3D12_framework.h"

class SkinningComputePass
{
public:
	void Initialize(ID3D12Device* device);

	bool CreateRootSignature();
	void CreatePipesAndShaders();

	void SetRenderData() {} // Compute Pass 无逐帧渲染数据

	ID3D12RootSignature* GetRootSignature() const;
	ID3D12PipelineState* GetPipelineState() const;

	void SetComputeShader(ID3DBlob* computeShader) { mComputeShader = computeShader; }

private:
	ComPtr<ID3D12Device> md3dDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mComputeShader = nullptr;
};
