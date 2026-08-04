#pragma once

#include "D3DHelpers.h"
#include "D3D12_framework.h"

#include <vector>

// 点光源专用的 cubemap 阴影资源池。
// 这个类型只负责 point light cubemap 自身的资源创建、DSV/SRV 组织与当前面绑定，
// 不再混入方向光/聚光的 2D shadow map 语义。
class PointLightShadowCubePool
{
public:
	struct CubeEntry
	{
		std::wstring Name;
		UINT SrvHeapIndex = 0;
		UINT FaceSize = 0;
		ComPtr<ID3D12Resource> Resource = nullptr;
		CD3DX12_CPU_DESCRIPTOR_HANDLE DsvHandles[6]{};
		CD3DX12_CPU_DESCRIPTOR_HANDLE SrvHandle{};
		CD3DX12_VIEWPORT Viewport{};
		CD3DX12_RECT ScissorRect{};
	};

public:
	PointLightShadowCubePool() = default;
	~PointLightShadowCubePool() = default;

	void Create(ID3D12Device* device, UINT defaultFaceSize);
	void OnResize(UINT newDefaultFaceSize);
	void Clear();

	void AddCube(
		std::wstring name,
		DXGI_FORMAT depthStencilFormat,
		UINT firstDsvIndex,
		UINT srvDescriptorHeapIndex,
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHeapStartHandle,
		UINT dsvDescriptorSize,
		D3D12_CPU_DESCRIPTOR_HANDLE srvHeapStartHandle,
		UINT cbvSrvUavDescriptorSize,
		UINT faceSize);

	bool HasCube(UINT cubeIndex) const;
	UINT GetCubeCount() const;
	UINT GetSrvHeapIndex(UINT cubeIndex) const;
	ComPtr<ID3D12Resource> GetCubeResource(UINT cubeIndex) const;
	const CubeEntry* GetCubeEntry(UINT cubeIndex) const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetFaceDsv(UINT cubeIndex, UINT faceIndex) const;
	const CD3DX12_VIEWPORT& GetCubeViewport(UINT cubeIndex) const;
	const CD3DX12_RECT& GetCubeScissorRect(UINT cubeIndex) const;

private:
	ID3D12Device* md3dDevice = nullptr;
	UINT m_defaultFaceSize = 0;
	std::vector<CubeEntry> m_cubes;
};
