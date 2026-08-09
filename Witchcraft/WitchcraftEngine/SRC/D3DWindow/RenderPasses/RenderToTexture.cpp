#include "RenderToTexture.h"

namespace Witchcraft::RenderDebugNames
{
const wchar_t* DxgiFormatDebugName(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return L"R8G8B8A8_UNORM";
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return L"R16G16B16A16_FLOAT";
	case DXGI_FORMAT_D32_FLOAT:
		return L"D32_FLOAT";
	case DXGI_FORMAT_R32_TYPELESS:
		return L"R32_TYPELESS";
	default:
		return L"UNKNOWN";
	}
}

std::wstring FormatRenderToTextureResourceName(
	const RenderToTextureDesc& desc,
	const wchar_t* role,
	DXGI_FORMAT format)
{
	const std::wstring baseName = !desc.DebugName.empty()
		? desc.DebugName
		: (L"RenderToTexture_id=" + std::to_wstring(desc.Id));
	return baseName +
		L"_role=" + role +
		L"_size=" + std::to_wstring(desc.Width) + L"x" + std::to_wstring(desc.Height) +
		L"_format=" + DxgiFormatDebugName(format) +
		L"(" + std::to_wstring(static_cast<UINT>(format)) + L")";
}
}

void RenderToTexture::Initialize(ID3D12Device* device, const RenderToTextureDesc& desc)
{
	mDevice = device;
	mDesc = desc;
	mDesc.Width = std::max<UINT>(1, mDesc.Width);
	mDesc.Height = std::max<UINT>(1, mDesc.Height);

	// Initialize 只用于第一次创建 RenderToTexture。
	// 已存在的输出目标不能在每帧同步阶段反复 Reset GPU resource；
	// 描述符和材质可能仍引用上一帧资源，强制释放容易导致设备移除。
	mColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	mDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	UpdateViewportAndScissor();

	CreateResources();
}

void RenderToTexture::ReleaseResources()
{
	mColorResource.Reset();
	mDepthResource.Reset();
	mColorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	mDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void RenderToTexture::OnResize(UINT width, UINT height)
{
	width = std::max<UINT>(1, width);
	height = std::max<UINT>(1, height);
	if (mDesc.Width == width && mDesc.Height == height)
		return;

	mDesc.Width = width;
	mDesc.Height = height;
	ReleaseResources();
	UpdateViewportAndScissor();

	CreateResources();

	RebuildViews();
}

bool RenderToTexture::BuildDescriptors(
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv,
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDsv)
{
	mCpuSrv = cpuSrv;
	mGpuSrv = gpuSrv;
	mCpuRtv = cpuRtv;
	mCpuDsv = cpuDsv;

	const bool hasRequiredDescriptors =
		mCpuSrv.ptr != 0 &&
		mGpuSrv.ptr != 0 &&
		mCpuRtv.ptr != 0 &&
		(!mDesc.HasDepth || mCpuDsv.ptr != 0);
	if (!hasRequiredDescriptors)
		return false;

	if (mColorResource != nullptr)
		RebuildViews();

	return true;
}

void RenderToTexture::TransitionColor(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState)
{
	TransitionTrackedResourceState(cmdList, mColorResource.Get(), mColorState, targetState);
}

void RenderToTexture::TransitionDepth(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState)
{
	TransitionTrackedResourceState(cmdList, mDepthResource.Get(), mDepthState, targetState);
}

void RenderToTexture::CreateResources()
{
	D3D12_CLEAR_VALUE colorClearValue = {};
	colorClearValue.Format = mDesc.ColorFormat;
	colorClearValue.Color[0] = 0.0f;
	colorClearValue.Color[1] = 0.0f;
	colorClearValue.Color[2] = 0.0f;
	colorClearValue.Color[3] = 1.0f;

	const auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		mDesc.ColorFormat,
		mDesc.Width,
		mDesc.Height,
		1,
		1,
		1,
		0,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

	const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(mDevice->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&colorDesc,
		mColorState,
		&colorClearValue,
		IID_PPV_ARGS(&mColorResource)));

	const std::wstring colorResourceName =
		Witchcraft::RenderDebugNames::FormatRenderToTextureResourceName(
			mDesc,
			L"ColorRT_SRV",
			mDesc.ColorFormat);
	SetD3DObjectName(mColorResource.Get(), colorResourceName.c_str());

	if (!mDesc.HasDepth)
		return;

	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.Format = mDesc.DepthFormat;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	const DXGI_FORMAT depthResourceFormat =
		mDesc.DepthFormat == DXGI_FORMAT_D32_FLOAT
		? DXGI_FORMAT_R32_TYPELESS
		: mDesc.DepthFormat;

	const auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		depthResourceFormat,
		mDesc.Width,
		mDesc.Height,
		1,
		1,
		1,
		0,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	ThrowIfFailed(mDevice->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		mDepthState,
		&depthClearValue,
		IID_PPV_ARGS(&mDepthResource)));
	const std::wstring depthResourceName =
		Witchcraft::RenderDebugNames::FormatRenderToTextureResourceName(
			mDesc,
			L"DepthDSV",
			mDesc.DepthFormat);
	SetD3DObjectName(mDepthResource.Get(), depthResourceName.c_str());
}

void RenderToTexture::RebuildViews()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mDesc.ColorFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = mDesc.ColorFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	mDevice->CreateShaderResourceView(mColorResource.Get(), &srvDesc, mCpuSrv);
	mDevice->CreateRenderTargetView(mColorResource.Get(), &rtvDesc, mCpuRtv);

	if (mDesc.HasDepth)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = mDesc.DepthFormat;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		dsvDesc.Texture2D.MipSlice = 0;
		mDevice->CreateDepthStencilView(mDepthResource.Get(), &dsvDesc, mCpuDsv);
	}
}

void RenderToTexture::UpdateViewportAndScissor()
{
	mViewport.TopLeftX = 0.0f;
	mViewport.TopLeftY = 0.0f;
	mViewport.Width = static_cast<float>(mDesc.Width);
	mViewport.Height = static_cast<float>(mDesc.Height);
	mViewport.MinDepth = 0.0f;
	mViewport.MaxDepth = 1.0f;

	mScissorRect.left = 0;
	mScissorRect.top = 0;
	mScissorRect.right = static_cast<LONG>(mDesc.Width);
	mScissorRect.bottom = static_cast<LONG>(mDesc.Height);
}

void RenderToTextureManager::Initialize(ID3D12Device* device)
{
	mDevice = device;
	mTextures.clear();
}

void RenderToTextureManager::Clear()
{
	mTextures.clear();
}

RenderToTexture* RenderToTextureManager::Create(
	const RenderToTextureDesc& desc,
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv,
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDsv)
{
	if (desc.Id == 0)
		return nullptr;

	if (mTextures.find(desc.Id) != mTextures.end())
		return nullptr;

	auto texture = std::make_unique<RenderToTexture>();
	texture->Initialize(mDevice.Get(), desc);
	if (!texture->BuildDescriptors(cpuSrv, gpuSrv, cpuRtv, cpuDsv))
		return nullptr;
	if (texture->GetColorResource() == nullptr ||
		texture->GetRtv().ptr == 0 ||
		texture->GetGpuSrv().ptr == 0 ||
		(desc.HasDepth && (texture->GetDepthResource() == nullptr || texture->GetDsv().ptr == 0)))
	{
		return nullptr;
	}

	RenderToTexture* rawTexture = texture.get();
	mTextures.emplace(desc.Id, std::move(texture));
	return rawTexture;
}

void RenderToTextureManager::Destroy(std::uint32_t id)
{
	mTextures.erase(id);
}

RenderToTexture* RenderToTextureManager::Find(std::uint32_t id)
{
	auto iter = mTextures.find(id);
	if (iter == mTextures.end())
		return nullptr;

	return iter->second.get();
}

const RenderToTexture* RenderToTextureManager::Find(std::uint32_t id) const
{
	auto iter = mTextures.find(id);
	if (iter == mTextures.end())
		return nullptr;

	return iter->second.get();
}

std::vector<std::uint32_t> RenderToTextureManager::GetIds() const
{
	std::vector<std::uint32_t> ids;
	ids.reserve(mTextures.size());
	for (const auto& entry : mTextures)
	{
		ids.push_back(entry.first);
	}
	return ids;
}
