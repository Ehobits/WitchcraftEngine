#pragma once

#include "../D3DHelpers.h"
#include "../D3D12_framework.h"
#include <atomic>

class SharedNormalPrepass
{
public:
	SharedNormalPrepass();
	~SharedNormalPrepass();

	// ── 统一接口 ──
	bool Initialize(ID3D12Device* device);
	bool IsInitialized() const { return md3dDevice != nullptr; }

	bool CreateRootSignature(ID3D12Device* device);
	bool CreatePipelineState(ID3D12Device* device, ID3DBlob* vertexShader, ID3DBlob* pixelShader);
	bool CreatePipelines(ID3D12Device* device, ID3DBlob* vertexShader, ID3DBlob* pixelShader);

	void SetRenderData() {} // 无逐帧数据

	ID3D12RootSignature* GetRootSignature() const { return nullptr; }
	ID3D12PipelineState* GetPipelineState() const { return nullptr; }

	void SetSharedRootSignature(ID3D12RootSignature*) {} // 本 Pass 在 BeginPass 参数中接收 RS

	// ── 本 Pass 特有接口 ──
	void OnResize(UINT newWidth, UINT newHeight);

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
		UINT cbvSrvUavDescriptorSize);
	void BuildSceneInputDescriptors(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT sceneInputHeapStartIndex,
		UINT cbvSrvUavDescriptorSize,
		UINT frameCount,
		ID3D12Resource* sceneDepthResource);

	D3D12_GPU_DESCRIPTOR_HANDLE GetSceneInputNormalSrv(UINT sceneInputHeapStartIndex, UINT cbvSrvUavDescriptorSize, UINT frameIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSceneInputDepthSrv(UINT sceneInputHeapStartIndex, UINT cbvSrvUavDescriptorSize, UINT frameIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetNormalDepthSrv() const { return mhNormalMapGpuSrv; }

	ID3D12Resource* GetNormalMapResource() const { return mNormalMap.Get(); }
	ID3D12Resource* GetDepthMapResource() const { return mDepthMap.Get(); }

	void BeginPass(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12RootSignature* sceneRootSignature,
		D3D12_GPU_VIRTUAL_ADDRESS passCBAddress,
		D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor,
		D3D12_GPU_DESCRIPTOR_HANDLE otherTexDescriptor,
		D3D12_GPU_DESCRIPTOR_HANDLE shadow2DDescriptorTable,
		D3D12_GPU_DESCRIPTOR_HANDLE pointLightShadowCubeDescriptor,
		D3D12_GPU_DESCRIPTOR_HANDLE ambientOcclusionDescriptor,
		D3D12_GPU_DESCRIPTOR_HANDLE directionalShadowMaskDescriptor,
		ID3D12DescriptorHeap* const* srvDescriptorHeaps,
		UINT srvHeapCount,
		bool clearTargets);
	void EndPass(ID3D12GraphicsCommandList* cmdList);

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhNormalMapGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuRtv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhDepthMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhDepthMapGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhDepthMapCpuDsv;

	void SetViewports(
		ID3D12GraphicsCommandList* cmdList,
		const D3D12_VIEWPORT& viewport,
		const D3D12_RECT& scissorRect);

private:
	void BuildResources();
	void BuildDescriptors();

	const DXGI_FORMAT SharedNormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	ComPtr<ID3D12Device> md3dDevice = nullptr;
	CD3DX12_VIEWPORT mViewport;
	CD3DX12_RECT mRect;
	bool mSetHandles = false;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mSceneInputBaseGpuSrv;
	std::atomic<bool> mDepthMapStartsInWriteState = false;

	ComPtr<ID3D12Resource> mDepthMap;
	ComPtr<ID3D12Resource> mNormalMap;
};
