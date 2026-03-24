#pragma once

#include "D3DHelpers.h"
#include "D3D12_framework.h"

class ShadowMap
{
public:
	ShadowMap();
	~ShadowMap();

	void Create(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, int width, int height);

	void OnResize(UINT newWidth, UINT newHeight);

	void Clear();
	void AddShadowMap(std::wstring name, DXGI_FORMAT DepthStencilFormat, CD3DX12_CPU_DESCRIPTOR_HANDLE DSVCpuHandle, CD3DX12_CPU_DESCRIPTOR_HANDLE SRVCpuHandle, UINT SrvDescriptorHeapIndex);

	void CreateRootSignature();
	// 创建顶点布局和管道
	void CreatePipesAndShaders(std::vector<ComPtr<ID3DBlob>>& vertexShader, std::vector<ComPtr<ID3DBlob>>& pixelShader, D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc, std::vector<ComPtr<ID3D12PipelineState>>& PipelineState);

	UINT GetHeapIndex(UINT index);
	UINT GetHeapIndexSize();

	ComPtr<ID3D12Resource> GetResource(UINT index);
	ComPtr<ID3D12Resource> GetResource(std::wstring name);

	void SetRenderTargets(ComPtr<ID3D12GraphicsCommandList> cmdList, UINT index);

	CD3DX12_CPU_DESCRIPTOR_HANDLE shaderMapDSVCpuHandle;

private:
	struct ShadowMapEntry
	{
		std::wstring Name;
		UINT HeapIndex = 0;
		CD3DX12_CPU_DESCRIPTOR_HANDLE DSVCpuHandle{};
		ComPtr<ID3D12Resource> Resource = nullptr;
	};

	ID3D12Device* md3dDevice = nullptr;

	CD3DX12_VIEWPORT m_Viewport;
	CD3DX12_RECT m_ScissorRect;

	std::vector<ShadowMapEntry> m_entries;

	// 编译着色器
	static ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);
};
