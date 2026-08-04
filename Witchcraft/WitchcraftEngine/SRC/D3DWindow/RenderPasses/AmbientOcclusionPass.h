#pragma once

#include "../D3DHelpers.h"
#include "../D3D12_framework.h"
#include "AOConstants.h"

class AmbientOcclusionPass
{
public:
	static constexpr UINT SsaoSampleCount = 14;
	static constexpr float ResolutionScale = 1.0f;

	AmbientOcclusionPass();
	~AmbientOcclusionPass();

	void Initialize(ID3D12Device* device);

	void OnResize(UINT newWidth, UINT newHeight);

	void CreateRootSignature();
	void CreatePipesAndShaders(std::vector<ComPtr<ID3DBlob>>& vertexShader, std::vector<ComPtr<ID3DBlob>>& pixelShader, D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc, std::vector<ComPtr<ID3D12PipelineState>>& PipelineState);

	void BuildResources();
	void BuildRandomVectorTexture(ID3D12GraphicsCommandList* cmdList);
	void BuildOffsetVectors();

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
		UINT cbvSrvUavDescriptorSize,
		UINT rtvDescriptorSize);
	void BuildDescriptors();

	ComPtr<ID3D12Resource> AmbientMap();
	UINT GetRenderWidth() const { return mRenderWidth; }
	UINT GetRenderHeight() const { return mRenderHeight; }

	AOConstants BuildConstants(
		const DirectX::XMMATRIX& projMatrix,
		const DirectX::XMFLOAT4X4& proj,
		const DirectX::XMFLOAT4X4& invProj,
		const DirectX::XMFLOAT4X4& invView,
		float renderTargetWidth,
		float renderTargetHeight,
		float blurSigma,
		float radius,
		float fadeStart,
		float fadeEnd,
		float surfaceEpsilon) const;
	void ClearAmbientMapsToNeutral(ID3D12GraphicsCommandList* cmdList);
	void RecordSsaoPasses(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		ID3D12PipelineState* ssaoPipelineState,
		ID3D12PipelineState* blurPipelineState,
		D3D12_GPU_VIRTUAL_ADDRESS aoCBAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE normalDepthSrvHandle);
	void SetViewports(ID3D12GraphicsCommandList* cmdList);
	void SetRenderTargets(ID3D12GraphicsCommandList* cmdList);
	void BlurAmbientMap(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12PipelineState* blurPipelineState,
		bool horzBlur,
		D3D12_GPU_VIRTUAL_ADDRESS aoCBAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE normalDepthSrvHandle);
	void TransitionAmbientMap0(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState);
	void TransitionAmbientMap1(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState);

	void GetOffsetVectors(DirectX::XMFLOAT4 offsets[SsaoSampleCount]) const;

	ComPtr<ID3D12RootSignature> GetRootSignature() const { return mSsaoRootSignature; }
	ComPtr<ID3D12RootSignature> GetSsaoRootSignature() const { return mSsaoRootSignature; }
	ComPtr<ID3D12RootSignature> GetBlurRootSignature() const { return mBlurRootSignature; }

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap0CpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap0GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap0CpuRtv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap1CpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap1GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap1CpuRtv;

private:
	// 计算缩放范围
	static UINT ComputeScaledDimension(UINT sourceDimension);

	const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R32_FLOAT;
	const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	ComPtr<ID3D12Device> md3dDevice = nullptr;
	ComPtr<ID3D12RootSignature> mSsaoRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mBlurRootSignature = nullptr;

	DirectX::XMFLOAT4 mOffsets[SsaoSampleCount];

	CD3DX12_VIEWPORT m_Viewport;
	CD3DX12_RECT m_Rect;
	UINT mRenderWidth = 1;
	UINT mRenderHeight = 1;

	bool m_setHandles = false;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhRandomVectorMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhRandomVectorMapGpuSrv;
	ComPtr<ID3D12Resource> mRandomVectorMap;
	ComPtr<ID3D12Resource> mRandomVectorMapUploadBuffer;
	ComPtr<ID3D12Resource> mAmbientMap0;
	ComPtr<ID3D12Resource> mAmbientMap1;
	D3D12_RESOURCE_STATES mAmbientMap0State = D3D12_RESOURCE_STATE_COMMON;
	D3D12_RESOURCE_STATES mAmbientMap1State = D3D12_RESOURCE_STATE_COMMON;
};
