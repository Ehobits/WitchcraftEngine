#pragma once

#include "D3DHelpers.h"
#include "D3D12_framework.h"

class AmbientOcclusion
{
public:
	AmbientOcclusion();
	~AmbientOcclusion();

	void Create(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, int width, int height);

	void OnResize(UINT newWidth, UINT newHeight);

	void CreateRootSignature();
	// 创建顶点布局和管道
	void CreatePipesAndShaders(std::vector<ComPtr<ID3DBlob>>& vertexShader, std::vector<ComPtr<ID3DBlob>>& pixelShader, D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc, std::vector<ComPtr<ID3D12PipelineState>>& PipelineState);

	void BuildResources();
	void BuildRandomVectorTexture(ID3D12GraphicsCommandList* cmdList);

	void BuildOffsetVectors();

	void BuildDescriptors(ID3D12Resource* depthStencilBuffer,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
		UINT cbvSrvUavDescriptorSize,
		UINT rtvDescriptorSize);
	void BuildDescriptors();

	ComPtr<ID3D12Resource> NormalMap();
	ComPtr<ID3D12Resource> AmbientMap();

	void SetViewports(ComPtr<ID3D12GraphicsCommandList> cmdList);

	void SetRenderTargets(ComPtr<ID3D12GraphicsCommandList> cmdList);

	void BlurAmbientMap(ComPtr<ID3D12GraphicsCommandList> cmdList, bool horzBlur);

	void GetOffsetVectors(DirectX::XMFLOAT4 offsets[14]);
	
	ComPtr<ID3D12RootSignature> GetRootSignature() const { return AORootSignature; }

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhNormalMapGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhNormalMapCpuRtv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhDepthMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhDepthMapGpuSrv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhRandomVectorMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhRandomVectorMapGpuSrv;

	// 在模糊处理时需要两个用于乒乓效果。
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap0CpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap0GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap0CpuRtv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap1CpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap1GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap1CpuRtv;

private:
	ComPtr<ID3D12Device> md3dDevice = nullptr;
	ComPtr<ID3D12RootSignature> AORootSignature = nullptr;

	DirectX::XMFLOAT4 mOffsets[14];

	CD3DX12_VIEWPORT m_Viewport;
	CD3DX12_RECT m_Rect;

	bool m_setHandles = false;
	ComPtr<ID3D12Resource> mDepthStencilBuffer;

	ComPtr<ID3D12Resource> mRandomVectorMap;
	ComPtr<ID3D12Resource> mRandomVectorMapUploadBuffer;
	ComPtr<ID3D12Resource> mNormalMap;
	ComPtr<ID3D12Resource> mAmbientMap0;
	ComPtr<ID3D12Resource> mAmbientMap1;

	// 编译着色器
	static ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);
};