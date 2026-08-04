#pragma once

#include "../D3D12_framework.h"

// OITCompositePass：Weighted Blended OIT 合成通道。
class OITCompositePass
{
public:
	void Initialize(ID3D12Device* device);

	bool CreatePipesAndShaders();

	void SetRenderData() {} // 全屏 Pass 无逐帧数据

	void Draw(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12DescriptorHeap* const* descriptorHeaps,
		UINT descriptorHeapCount,
		D3D12_GPU_VIRTUAL_ADDRESS postProcessCBAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE sceneColorDescriptor,
		D3D12_GPU_DESCRIPTOR_HANDLE oitAccumDescriptor,
		const D3D12_VIEWPORT& viewport,
		const D3D12_RECT& scissorRect,
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetBasePsoDesc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) { mBasePsoDesc = desc; mBasePsoDescSet = true; }
	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

private:
	ComPtr<ID3D12Device> md3dDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC mBasePsoDesc = {};
	bool mBasePsoDescSet = false;
};
