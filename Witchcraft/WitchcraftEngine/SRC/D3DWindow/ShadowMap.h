#pragma once

#include "D3DHelpers.h"
#include "D3D12_framework.h"

// 阴影贴图槽位类型
enum class ShadowMapSlotType : UINT
{
	Texture2D = 0,     // 单层 2D 纹理，用于方向光级联和聚光灯
	TextureCube = 1    // 6 层立方体纹理，用于点光源
};

class ShadowMap
{
public:
	ShadowMap();
	~ShadowMap();

	void Create(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, int width, int height);

	void OnResize(UINT newWidth, UINT newHeight);

	void Clear();
	bool MatchesLayout(const std::vector<UINT>& desiredSizes) const;
	void RebuildShadowMaps(
		const std::vector<UINT>& desiredSizes,
		DXGI_FORMAT depthStencilFormat,
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHeapStartHandle,
		UINT dsvDescriptorSize,
		D3D12_CPU_DESCRIPTOR_HANDLE srvHeapStartHandle,
		UINT srvDescriptorHeapStartIndex,
		UINT cbvSrvUavDescriptorSize);
	void AddShadowMap(
		std::wstring name,
		DXGI_FORMAT DepthStencilFormat,
		CD3DX12_CPU_DESCRIPTOR_HANDLE DSVCpuHandle,
		CD3DX12_CPU_DESCRIPTOR_HANDLE SRVCpuHandle,
		UINT SrvDescriptorHeapIndex,
		UINT width = 0,
		UINT height = 0);

	// 阴影贴图数组布局描述
	// 用于告诉 shader 侧哪些槽位是 Texture2D，哪些是 TextureCube
	struct ShadowMapLayout
	{
		UINT Texture2DCount = 0;
		UINT TextureCubeCount = 0;
	};
	ShadowMapLayout GetLayout() const;

	void CreateRootSignature();
	// 创建顶点布局和管道
	void CreatePipesAndShaders(std::vector<ComPtr<ID3DBlob>>& vertexShader, std::vector<ComPtr<ID3DBlob>>& pixelShader, D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc, std::vector<ComPtr<ID3D12PipelineState>>& PipelineState);

	UINT GetHeapIndex(UINT index);
	UINT GetHeapIndexSize();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsvHandle(UINT index) const;
	const CD3DX12_VIEWPORT& GetViewport(UINT index) const;
	const CD3DX12_RECT& GetScissorRect(UINT index) const;

	ComPtr<ID3D12Resource> GetResource(UINT index);
	ComPtr<ID3D12Resource> GetResource(std::wstring name);

	void SetRenderTargets(ComPtr<ID3D12GraphicsCommandList> cmdList, UINT index);

private:
	struct ShadowMapEntry
	{
		std::wstring Name;
		UINT HeapIndex = 0;
		UINT Width = 0;
		UINT Height = 0;
		CD3DX12_VIEWPORT Viewport{};
		CD3DX12_RECT ScissorRect{};
		CD3DX12_CPU_DESCRIPTOR_HANDLE DSVCpuHandle{};
		ComPtr<ID3D12Resource> Resource = nullptr;
	};

	ID3D12Device* md3dDevice = nullptr;

	UINT m_defaultWidth = 0;
	UINT m_defaultHeight = 0;

	std::vector<ShadowMapEntry> m_entries;

	// 编译着色器
	static ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);
};
