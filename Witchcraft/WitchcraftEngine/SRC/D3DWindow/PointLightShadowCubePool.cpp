#include "PointLightShadowCubePool.h"

#include <algorithm>

void PointLightShadowCubePool::Create(ID3D12Device* device, UINT defaultFaceSize)
{
	md3dDevice = device;
	m_defaultFaceSize = defaultFaceSize;
}

void PointLightShadowCubePool::OnResize(UINT newDefaultFaceSize)
{
	m_defaultFaceSize = newDefaultFaceSize;
}

void PointLightShadowCubePool::Clear()
{
	m_cubes.clear();
}

void PointLightShadowCubePool::AddCube(
	std::wstring name,
	DXGI_FORMAT depthStencilFormat,
	UINT firstDsvIndex,
	UINT srvDescriptorHeapIndex,
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHeapStartHandle,
	UINT dsvDescriptorSize,
	D3D12_CPU_DESCRIPTOR_HANDLE srvHeapStartHandle,
	UINT cbvSrvUavDescriptorSize,
	UINT faceSize)
{
	if (md3dDevice == nullptr)
		return;

	faceSize = (std::max)(faceSize != 0 ? faceSize : m_defaultFaceSize, 1u);

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = faceSize;
	texDesc.Height = faceSize;
	texDesc.DepthOrArraySize = 6;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear = {};
	optClear.Format = depthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	ComPtr<ID3D12Resource> cubeResource = nullptr;
	D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		&optClear,
		IID_PPV_ARGS(&cubeResource)));

	CubeEntry entry;
	entry.Name = std::move(name);
	entry.SrvHeapIndex = srvDescriptorHeapIndex;
	entry.FaceSize = faceSize;
	entry.Resource = cubeResource;
	entry.Viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(faceSize), static_cast<float>(faceSize), 0.0f, 1.0f);
	entry.ScissorRect = CD3DX12_RECT(0, 0, static_cast<LONG>(faceSize), static_cast<LONG>(faceSize));

	for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsvHeapStartHandle, firstDsvIndex + faceIndex, dsvDescriptorSize);

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		dsvDesc.Format = depthStencilFormat;
		dsvDesc.Texture2DArray.MipSlice = 0;
		dsvDesc.Texture2DArray.FirstArraySlice = faceIndex;
		dsvDesc.Texture2DArray.ArraySize = 1;

		md3dDevice->CreateDepthStencilView(cubeResource.Get(), &dsvDesc, dsvHandle);
		entry.DsvHandles[faceIndex] = dsvHandle;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(srvHeapStartHandle, srvDescriptorHeapIndex, cbvSrvUavDescriptorSize);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = 1;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(cubeResource.Get(), &srvDesc, srvHandle);
	entry.SrvHandle = srvHandle;

	m_cubes.push_back(std::move(entry));
}

bool PointLightShadowCubePool::HasCube(UINT cubeIndex) const
{
	return cubeIndex < m_cubes.size();
}

UINT PointLightShadowCubePool::GetCubeCount() const
{
	return static_cast<UINT>(m_cubes.size());
}

UINT PointLightShadowCubePool::GetSrvHeapIndex(UINT cubeIndex) const
{
	if (cubeIndex >= m_cubes.size())
		return 0;
	return m_cubes[cubeIndex].SrvHeapIndex;
}

ComPtr<ID3D12Resource> PointLightShadowCubePool::GetCubeResource(UINT cubeIndex) const
{
	if (cubeIndex >= m_cubes.size())
		return nullptr;
	return m_cubes[cubeIndex].Resource;
}

const PointLightShadowCubePool::CubeEntry* PointLightShadowCubePool::GetCubeEntry(UINT cubeIndex) const
{
	if (cubeIndex >= m_cubes.size())
		return nullptr;
	return &m_cubes[cubeIndex];
}

CD3DX12_CPU_DESCRIPTOR_HANDLE PointLightShadowCubePool::GetFaceDsv(UINT cubeIndex, UINT faceIndex) const
{
	if (cubeIndex >= m_cubes.size() || faceIndex >= 6)
		return {};
	return m_cubes[cubeIndex].DsvHandles[faceIndex];
}

const CD3DX12_VIEWPORT& PointLightShadowCubePool::GetCubeViewport(UINT cubeIndex) const
{
	static const CD3DX12_VIEWPORT kEmptyViewport{};
	if (cubeIndex >= m_cubes.size())
		return kEmptyViewport;
	return m_cubes[cubeIndex].Viewport;
}

const CD3DX12_RECT& PointLightShadowCubePool::GetCubeScissorRect(UINT cubeIndex) const
{
	static const CD3DX12_RECT kEmptyScissorRect{};
	if (cubeIndex >= m_cubes.size())
		return kEmptyScissorRect;
	return m_cubes[cubeIndex].ScissorRect;
}
