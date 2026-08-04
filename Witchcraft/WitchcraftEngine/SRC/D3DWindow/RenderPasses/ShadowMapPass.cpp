#include "ShadowMapPass.h"
#include <algorithm>

ShadowMapPass::ShadowMapPass()
{
}

ShadowMapPass::~ShadowMapPass()
{
}

void ShadowMapPass::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, int width, int height)
{
	md3dDevice = device;
	OnResize(width, height);
}

void ShadowMapPass::OnResize(UINT newWidth, UINT newHeight)
{
	m_defaultWidth = newWidth;
	m_defaultHeight = newHeight;
}

void ShadowMapPass::Clear()
{
	m_entries.clear();
}

bool ShadowMapPass::MatchesLayout(const std::vector<UINT>& desiredSizes) const
{
	if (m_entries.size() != desiredSizes.size())
		return false;

	for (size_t shadowIndex = 0; shadowIndex < desiredSizes.size(); ++shadowIndex)
	{
		const UINT desiredSize = (std::max)(desiredSizes[shadowIndex], 1u);
		const ShadowMapEntry& entry = m_entries[shadowIndex];
		if (entry.Width != desiredSize || entry.Height != desiredSize)
			return false;
		if (entry.Resource == nullptr)
			return false;
	}

	return true;
}

void ShadowMapPass::RebuildShadowMaps(
	const std::vector<UINT>& desiredSizes,
	DXGI_FORMAT depthStencilFormat,
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHeapStartHandle,
	UINT dsvDescriptorSize,
	D3D12_CPU_DESCRIPTOR_HANDLE srvHeapStartHandle,
	UINT srvDescriptorHeapStartIndex,
	UINT cbvSrvUavDescriptorSize)
{
	Clear();

	for (UINT shadowIndex = 0; shadowIndex < desiredSizes.size(); ++shadowIndex)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle(dsvHeapStartHandle, 1 + shadowIndex, dsvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE shadowSrvHandle(
			srvHeapStartHandle,
			srvDescriptorHeapStartIndex + shadowIndex,
			cbvSrvUavDescriptorSize);
		AddShadowMap(
			L"shadowmap" + std::to_wstring(shadowIndex),
			depthStencilFormat,
			shadowDsvHandle,
			shadowSrvHandle,
			srvDescriptorHeapStartIndex + shadowIndex,
			desiredSizes[shadowIndex],
			desiredSizes[shadowIndex]);
	}
}

void ShadowMapPass::AddShadowMap(
	std::wstring name,
	DXGI_FORMAT DepthStencilFormat,
	CD3DX12_CPU_DESCRIPTOR_HANDLE DSVCpuHandle,
	CD3DX12_CPU_DESCRIPTOR_HANDLE SRVCpuHandle,
	UINT SrvDescriptorHeapIndex,
	UINT width,
	UINT height)
{
	UINT Width = width;
	UINT Height = height;
	if (Width == 0)
		Width = m_defaultWidth;
	if (Height == 0)
		Height = m_defaultHeight;
	Width = (std::max)(Width, 1u);
	Height = (std::max)(Height, 1u);

	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = Width;
	texDesc.Height = Height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = DepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	ComPtr<ID3D12Resource> shadowMapResource = nullptr;
	D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		&optClear,
		IID_PPV_ARGS(&shadowMapResource)));

	// 为资源创建SRV，以便我们可以在着色器程序中对阴影贴图进行采样。
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = shadowMapResource->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.PlaneSlice = 0;
	md3dDevice->CreateShaderResourceView(shadowMapResource.Get(), &srvDesc, SRVCpuHandle);

	// 为资源创建DSV，以便我们可以渲染到阴影贴图。
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateDepthStencilView(shadowMapResource.Get(), &dsvDesc, DSVCpuHandle);

	ShadowMapEntry entry;
	entry.Name = std::move(name);
	entry.HeapIndex = SrvDescriptorHeapIndex;
	entry.Width = Width;
	entry.Height = Height;
	entry.Viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f);
	entry.ScissorRect = CD3DX12_RECT(0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height));
	entry.DSVCpuHandle = DSVCpuHandle;
	entry.Resource = shadowMapResource;
	m_entries.push_back(std::move(entry));
}

void ShadowMapPass::CreatePipesAndShaders(ComPtr<ID3DBlob>& vertexShader, ComPtr<ID3DBlob>& pixelShader, D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc, ComPtr<ID3D12PipelineState>& PipelineState)
{
	vertexShader = CompileShader(L"DATA/Shaders/Shadows", nullptr, "VS", "vs_5_1");
	pixelShader = CompileShader(L"DATA/Shaders/Shadows", nullptr, "PS", "ps_5_1");

	//
	//用于阴影贴图的PSO。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = basePsoDesc;
	smapPsoDesc.RasterizerState.DepthBias = 14000;
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.25f;
	smapPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
	smapPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());

	//阴影贴图传递没有渲染目标。
	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	smapPsoDesc.NumRenderTargets = 0;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc,
		IID_PPV_ARGS(&PipelineState)));
	SetD3DObjectName(PipelineState.Get(), L"管线_阴影深度_ShadowMap通用");
}

UINT ShadowMapPass::GetHeapIndex(UINT index)
{
	return m_entries[index].HeapIndex;
}

UINT ShadowMapPass::GetHeapIndexSize()
{
	return static_cast<UINT>(m_entries.size());
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowMapPass::GetDsvHandle(UINT index) const
{
	if (index >= m_entries.size())
		return {};
	return m_entries[index].DSVCpuHandle;
}

const CD3DX12_VIEWPORT& ShadowMapPass::GetViewport(UINT index) const
{
	static const CD3DX12_VIEWPORT kEmptyViewport{};
	if (index >= m_entries.size())
		return kEmptyViewport;
	return m_entries[index].Viewport;
}

const CD3DX12_RECT& ShadowMapPass::GetScissorRect(UINT index) const
{
	static const CD3DX12_RECT kEmptyScissorRect{};
	if (index >= m_entries.size())
		return kEmptyScissorRect;
	return m_entries[index].ScissorRect;
}

ComPtr<ID3D12Resource> ShadowMapPass::GetResource(UINT index)
{
	if (index >= m_entries.size())
		return nullptr;
	return m_entries[index].Resource;
}

ComPtr<ID3D12Resource> ShadowMapPass::GetResource(std::wstring name)
{
	for (const ShadowMapEntry& entry : m_entries)
	{
		if (entry.Name == name)
			return entry.Resource;
	}

	return nullptr;
}

void ShadowMapPass::SetRenderTargets(ComPtr<ID3D12GraphicsCommandList> cmdList, UINT index)
{
	if (index >= m_entries.size())
		return;

	cmdList->RSSetViewports(1, &m_entries[index].Viewport);
	cmdList->RSSetScissorRects(1, &m_entries[index].ScissorRect);
	const CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_entries[index].DSVCpuHandle;

	// 设置空渲染目标，因为我们只会绘制到深度缓冲区。设置空渲染目标将禁用颜色写入。
	// 请注意，活动PSO还必须指定渲染目标计数为0。
	cmdList->OMSetRenderTargets(0, nullptr, false, &dsvHandle);
}

ShadowMapPass::ShadowMapLayout ShadowMapPass::GetLayout() const
{
	ShadowMapLayout layout;
	layout.Texture2DCount = static_cast<UINT>(m_entries.size());
	layout.TextureCubeCount = 0;
	return layout;
}
