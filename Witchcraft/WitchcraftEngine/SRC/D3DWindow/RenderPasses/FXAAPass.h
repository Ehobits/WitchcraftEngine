#pragma once

#include "../D3D12_framework.h"

// FXAAPass：后处理抗锯齿通道。
class FXAAPass
{
public:
	void Initialize(ID3D12Device* device);

	void CreatePipesAndShaders();

	void Draw(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12DescriptorHeap* const* descriptorHeaps,
		UINT descriptorHeapCount,
		D3D12_GPU_VIRTUAL_ADDRESS postProcessCBAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE sceneColorDescriptor,
		const D3D12_VIEWPORT& viewport,
		const D3D12_RECT& scissorRect,
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetBasePsoDesc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) { mBasePsoDesc = desc; mBasePsoDescSet = true; }
	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

private:
	ComPtr<ID3D12Device> mDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC mBasePsoDesc = {};
	bool mBasePsoDescSet = false;
};
