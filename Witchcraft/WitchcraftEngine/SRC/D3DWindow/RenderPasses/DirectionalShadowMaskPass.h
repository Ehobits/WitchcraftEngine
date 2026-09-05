#pragma once

#include "../D3D12_framework.h"
#include "../D3DHelpers.h"
#include "D3DPassContext.h"

class DirectionalShadowMaskPass
{
public:
	static constexpr DXGI_FORMAT MaskFormat = DXGI_FORMAT_R16_FLOAT;

	void Initialize(ID3D12Device* device);
	void CreateRootSignature();
	void CreatePipesAndShaders(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc);

	void OnResize(UINT width, UINT height);

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
		UINT cbvSrvUavDescriptorSize,
		UINT rtvDescriptorSize);

	void RecordPasses(
		const D3DPassContext& context,
		ID3D12PipelineState* maskPipelineState,
		ID3D12PipelineState* blurPipelineState);

	void ClearToNeutral(ID3D12GraphicsCommandList* cmdList);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetMaskPipelineState() const { return mMaskPipelineState.Get(); }
	ID3D12PipelineState* GetBlurPipelineState() const { return mBlurPipelineState.Get(); }
	D3D12_GPU_DESCRIPTOR_HANDLE GetMaskSrv() const { return mhMask0GpuSrv; }
	ID3D12Resource* GetMaskResource() const { return mMask0.Get(); }

private:
	void BuildResources();
	void BuildDescriptors();
	void SetViewport(ID3D12GraphicsCommandList* cmdList);
	void BlurMask(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12PipelineState* blurPipelineState,
		bool horizontalBlur,
		D3D12_GPU_VIRTUAL_ADDRESS passCBAddress,
		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE normalDepthSrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE shadow2DDescriptorTable);
	void TransitionMask0(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState);
	void TransitionMask1(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState);

	ComPtr<ID3D12Device> mDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3DBlob> mMaskVertexShader = nullptr;
	ComPtr<ID3DBlob> mMaskPixelShader = nullptr;
	ComPtr<ID3DBlob> mBlurVertexShader = nullptr;
	ComPtr<ID3DBlob> mBlurPixelShader = nullptr;
	ComPtr<ID3D12PipelineState> mMaskPipelineState = nullptr;
	ComPtr<ID3D12PipelineState> mBlurPipelineState = nullptr;

	ComPtr<ID3D12Resource> mMask0 = nullptr;
	ComPtr<ID3D12Resource> mMask1 = nullptr;
	D3D12_RESOURCE_STATES mMask0State = D3D12_RESOURCE_STATE_GENERIC_READ;
	D3D12_RESOURCE_STATES mMask1State = D3D12_RESOURCE_STATE_GENERIC_READ;

	CD3DX12_VIEWPORT mViewport;
	CD3DX12_RECT mRect;
	UINT mWidth = 1;
	UINT mHeight = 1;

	bool mSetHandles = false;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhMask0CpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhMask0GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhMask0CpuRtv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhMask1CpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhMask1GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhMask1CpuRtv;
};
