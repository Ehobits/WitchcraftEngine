#include "ShadowMap.h"

ShadowMap::ShadowMap()
{
}

ShadowMap::~ShadowMap()
{
}

void ShadowMap::Create(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, int width, int height)
{
	md3dDevice = device;
	OnResize(width, height);
}

void ShadowMap::OnResize(UINT newWidth, UINT newHeight)
{	
	// 此处设置窗口大小和裁剪大小
	m_Viewport = CD3DX12_VIEWPORT{ 0.0f, 0.0f,
		static_cast<float>(newWidth),
		static_cast<float>(newHeight),
		0.0f,1.0f };
	m_ScissorRect = CD3DX12_RECT{ 0, 0,
		(long)newWidth,
		(long)newHeight };
}

void ShadowMap::Clear()
{
	m_entries.clear();
}

void ShadowMap::AddShadowMap(std::wstring name, DXGI_FORMAT DepthStencilFormat, CD3DX12_CPU_DESCRIPTOR_HANDLE DSVCpuHandle, CD3DX12_CPU_DESCRIPTOR_HANDLE SRVCpuHandle, UINT SrvDescriptorHeapIndex)
{
	shaderMapDSVCpuHandle = DSVCpuHandle;

	UINT Width = m_ScissorRect.right - m_ScissorRect.left;
	UINT Height = m_ScissorRect.bottom - m_ScissorRect.top;

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
	md3dDevice->CreateDepthStencilView(shadowMapResource.Get(), &dsvDesc, shaderMapDSVCpuHandle);

	ShadowMapEntry entry;
	entry.Name = std::move(name);
	entry.HeapIndex = SrvDescriptorHeapIndex;
	entry.DSVCpuHandle = DSVCpuHandle;
	entry.Resource = shadowMapResource;
	m_entries.push_back(std::move(entry));
}

void ShadowMap::CreateRootSignature()
{
	// shadowmap不需要根签名
}

void ShadowMap::CreatePipesAndShaders(std::vector<ComPtr<ID3DBlob>>& vertexShader, std::vector<ComPtr<ID3DBlob>>& pixelShader, D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc, std::vector<ComPtr<ID3D12PipelineState>>& PipelineState)
{
	vertexShader[0] = CompileShader(L"DATA/Shaders/Shadows", nullptr, "VS", "vs_5_1");
	pixelShader[0] = CompileShader(L"DATA/Shaders/Shadows", nullptr, "PS", "ps_5_1");

	//
	//用于阴影贴图的PSO。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = basePsoDesc;
	smapPsoDesc.RasterizerState.DepthBias = 14000;
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.25f;
	smapPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[0].Get());
	smapPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[0].Get());

	//阴影贴图传递没有渲染目标。
	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	smapPsoDesc.NumRenderTargets = 0;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc,
		IID_PPV_ARGS(&PipelineState[0])));
}

UINT ShadowMap::GetHeapIndex(UINT index)
{
	return m_entries[index].HeapIndex;
}

UINT ShadowMap::GetHeapIndexSize()
{
	return static_cast<UINT>(m_entries.size());
}

ComPtr<ID3D12Resource> ShadowMap::GetResource(UINT index)
{
	return m_entries[index].Resource;
}

ComPtr<ID3D12Resource> ShadowMap::GetResource(std::wstring name)
{
	for (const ShadowMapEntry& entry : m_entries)
	{
		if (entry.Name == name)
			return entry.Resource;
	}

	return nullptr;
}

void ShadowMap::SetRenderTargets(ComPtr<ID3D12GraphicsCommandList> cmdList, UINT index)
{
	cmdList->RSSetViewports(1, &m_Viewport);
	cmdList->RSSetScissorRects(1, &m_ScissorRect);
	shaderMapDSVCpuHandle = m_entries[index].DSVCpuHandle;

	// 设置空渲染目标，因为我们只会绘制到深度缓冲区。设置空渲染目标将禁用颜色写入。
	// 请注意，活动PSO还必须指定渲染目标计数为0。
	cmdList->OMSetRenderTargets(0, nullptr, false, &shaderMapDSVCpuHandle);
}

// 编译着色器
ComPtr<ID3DBlob> ShadowMap::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	std::wstring hlsl_Path = filename;
	hlsl_Path.append(L".hlsl");

	UINT compileFlags = 0;

#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(hlsl_Path.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if (errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

	return byteCode;
}
