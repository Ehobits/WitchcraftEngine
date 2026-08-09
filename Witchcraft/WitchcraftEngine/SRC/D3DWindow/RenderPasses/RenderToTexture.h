#pragma once

#include "../D3DHelpers.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct RenderToTextureDesc
{
	std::uint32_t Id = 0;
	UINT Width = 1;
	UINT Height = 1;
	DXGI_FORMAT ColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT DepthFormat = DXGI_FORMAT_D32_FLOAT;
	bool HasDepth = true;
	bool AutoResizeWithViewport = false;
	std::wstring DebugName;
};

// RenderToTexture 只管理离屏颜色/深度资源、view 描述符和资源状态。
// 它不申请 descriptor slot，不调度 render pass，也不决定哪个 ECS Camera 输出到这里。
class RenderToTexture
{
public:
	void Initialize(ID3D12Device* device, const RenderToTextureDesc& desc);
	void ReleaseResources();

	void OnResize(UINT width, UINT height);

	const RenderToTextureDesc& GetDesc() const { return mDesc; }

	ID3D12Resource* GetColorResource() const { return mColorResource.Get(); }
	ID3D12Resource* GetDepthResource() const { return mDepthResource.Get(); }

	D3D12_RESOURCE_STATES GetColorState() const { return mColorState; }
	D3D12_RESOURCE_STATES GetDepthState() const { return mDepthState; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuSrv() const { return mCpuSrv; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrv() const { return mGpuSrv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetRtv() const { return mCpuRtv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDsv() const { return mCpuDsv; }

	const D3D12_VIEWPORT& GetViewport() const { return mViewport; }
	const D3D12_RECT& GetScissorRect() const { return mScissorRect; }

	void TransitionColor(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState);
	void TransitionDepth(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState);

private:
	friend class RenderToTextureManager;

	bool BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDsv);
	void CreateResources();
	void RebuildViews();
	void UpdateViewportAndScissor();

	ComPtr<ID3D12Device> mDevice = nullptr;
	RenderToTextureDesc mDesc;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mCpuRtv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mCpuDsv;

	ComPtr<ID3D12Resource> mColorResource = nullptr;
	ComPtr<ID3D12Resource> mDepthResource = nullptr;
	D3D12_RESOURCE_STATES mColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	D3D12_RESOURCE_STATES mDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	D3D12_VIEWPORT mViewport = {};
	D3D12_RECT mScissorRect = {};
};

// RenderToTextureManager 是按 RenderToTexture id 查找 GPU texture 对象的轻量容器。
// 它不参与 ECS 相机快照生成，也不直接插入主渲染流程。
class RenderToTextureManager
{
public:
	void Initialize(ID3D12Device* device);
	void Clear();

	// Create/Destroy 是唯一管理 RenderToTexture GPU resource 生命周期的入口。
	// 每帧同步流程应先 Find 并复用现有对象，不能把 Initialize 当作 update 路径调用。
	RenderToTexture* Create(
		const RenderToTextureDesc& desc,
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDsv);
	void Destroy(std::uint32_t id);

	RenderToTexture* Find(std::uint32_t id);
	const RenderToTexture* Find(std::uint32_t id) const;

	std::vector<std::uint32_t> GetIds() const;

private:
	ComPtr<ID3D12Device> mDevice = nullptr;
	std::unordered_map<std::uint32_t, std::unique_ptr<RenderToTexture>> mTextures;
};
