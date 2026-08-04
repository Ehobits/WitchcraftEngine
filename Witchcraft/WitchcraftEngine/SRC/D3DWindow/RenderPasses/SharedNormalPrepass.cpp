#include "SharedNormalPrepass.h"

SharedNormalPrepass::SharedNormalPrepass()
{
}

SharedNormalPrepass::~SharedNormalPrepass()
{
}

// ── 统一接口 ──

bool SharedNormalPrepass::Initialize(ID3D12Device* device)
{
	if (device == nullptr)
		return false;
	md3dDevice = device;
	return true;
}

bool SharedNormalPrepass::CreateRootSignature(ID3D12Device* device)
{
	(void)device;
	return true; // 本 Pass 在 BeginPass 参数中接收外部 RS
}

bool SharedNormalPrepass::CreatePipelineState(ID3D12Device* device, ID3DBlob* vertexShader, ID3DBlob* pixelShader)
{
	(void)device;
	(void)vertexShader;
	(void)pixelShader;
	return true; // 本 Pass 管理渲染目标，不创建 PSO
}

bool SharedNormalPrepass::CreatePipelines(ID3D12Device* device, ID3DBlob* vertexShader, ID3DBlob* pixelShader)
{
	if (!CreateRootSignature(device))
		return false;
	return CreatePipelineState(device, vertexShader, pixelShader);
}

void SharedNormalPrepass::SetViewports(
	ID3D12GraphicsCommandList* cmdList,
	const D3D12_VIEWPORT& viewport,
	const D3D12_RECT& scissorRect)
{
	if (cmdList == nullptr)
		return;
	cmdList->RSSetViewports(1, &viewport);
	cmdList->RSSetScissorRects(1, &scissorRect);
}

// ── 本 Pass 特有接口 ──

void SharedNormalPrepass::OnResize(UINT newWidth, UINT newHeight)
{
	mViewport = CD3DX12_VIEWPORT(
		0.0f,
		0.0f,
		static_cast<float>(newWidth),
		static_cast<float>(newHeight),
		0.0f,
		1.0f);
	mRect = CD3DX12_RECT(0, 0, static_cast<LONG>(newWidth), static_cast<LONG>(newHeight));

	BuildResources();
}

void SharedNormalPrepass::BuildResources()
{
	mDepthMap = nullptr;
	mNormalMap = nullptr;

	D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	D3D12_RESOURCE_DESC depthMapDesc = {};
	depthMapDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthMapDesc.Alignment = 0;
	depthMapDesc.Width = static_cast<UINT64>(mViewport.Width);
	depthMapDesc.Height = static_cast<UINT>(mViewport.Height);
	depthMapDesc.DepthOrArraySize = 1;
	depthMapDesc.MipLevels = 1;
	depthMapDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	depthMapDesc.SampleDesc.Count = 1;
	depthMapDesc.SampleDesc.Quality = 0;
	depthMapDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthMapDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthMapDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&mDepthMap)));
	mDepthMapStartsInWriteState.store(true, std::memory_order_release);

	D3D12_RESOURCE_DESC normalMapDesc = {};
	normalMapDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	normalMapDesc.Alignment = 0;
	normalMapDesc.Width = static_cast<UINT64>(mViewport.Width);
	normalMapDesc.Height = static_cast<UINT>(mViewport.Height);
	normalMapDesc.DepthOrArraySize = 1;
	normalMapDesc.MipLevels = 1;
	normalMapDesc.Format = SharedNormalMapFormat;
	normalMapDesc.SampleDesc.Count = 1;
	normalMapDesc.SampleDesc.Quality = 0;
	normalMapDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	normalMapDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	const float normalClearColor[] = { 0.0f, 0.0f, 1.0f, 0.0f };
	CD3DX12_CLEAR_VALUE normalClearValue(SharedNormalMapFormat, normalClearColor);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&normalMapDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		&normalClearValue,
		IID_PPV_ARGS(&mNormalMap)));

	BuildDescriptors();
}

void SharedNormalPrepass::BuildDescriptors(
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
	UINT cbvSrvUavDescriptorSize)
{
	mhNormalMapCpuSrv = hCpuSrv;
	mhDepthMapCpuSrv = hCpuSrv;
	mhDepthMapCpuSrv.Offset(1, cbvSrvUavDescriptorSize);

	mhNormalMapGpuSrv = hGpuSrv;
	mhDepthMapGpuSrv = hGpuSrv;
	mhDepthMapGpuSrv.Offset(1, cbvSrvUavDescriptorSize);

	mhDepthMapCpuDsv = hCpuDsv;
	mhNormalMapCpuRtv = hCpuRtv;

	mSetHandles = true;
	BuildDescriptors();
}

void SharedNormalPrepass::BuildDescriptors()
{
	if (!mSetHandles || md3dDevice == nullptr)
		return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	srvDesc.Format = SharedNormalMapFormat;
	md3dDevice->CreateShaderResourceView(mNormalMap.Get(), &srvDesc, mhNormalMapCpuSrv);

	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	md3dDevice->CreateShaderResourceView(mDepthMap.Get(), &srvDesc, mhDepthMapCpuSrv);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = SharedNormalMapFormat;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	md3dDevice->CreateRenderTargetView(mNormalMap.Get(), &rtvDesc, mhNormalMapCpuRtv);
	mNormalMap->SetName(L"GlobalNormalMap");

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateDepthStencilView(mDepthMap.Get(), &dsvDesc, mhDepthMapCpuDsv);
	mDepthMap->SetName(L"SharedNormalPrepassDepthMap");
}

void SharedNormalPrepass::BuildSceneInputDescriptors(
	ID3D12Device* device,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	UINT sceneInputHeapStartIndex,
	UINT cbvSrvUavDescriptorSize,
	UINT frameCount,
	ID3D12Resource* sceneDepthResource)
{
	if (device == nullptr || srvDescriptorHeap == nullptr)
		return;

	D3D12_SHADER_RESOURCE_VIEW_DESC normalSrvDesc = {};
	normalSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	normalSrvDesc.Format = SharedNormalMapFormat;
	normalSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	normalSrvDesc.Texture2D.MostDetailedMip = 0;
	normalSrvDesc.Texture2D.MipLevels = 1;
	normalSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
	depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSrvDesc.Texture2D.MostDetailedMip = 0;
	depthSrvDesc.Texture2D.MipLevels = 1;
	depthSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	CD3DX12_CPU_DESCRIPTOR_HANDLE sceneInputSrvCpuHandle(
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		sceneInputHeapStartIndex,
		cbvSrvUavDescriptorSize);
	mSceneInputBaseGpuSrv = CD3DX12_GPU_DESCRIPTOR_HANDLE(
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		sceneInputHeapStartIndex,
		cbvSrvUavDescriptorSize);

	for (UINT frameIndex = 0; frameIndex < frameCount; ++frameIndex)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE normalHandle = sceneInputSrvCpuHandle;
		normalHandle.Offset(frameIndex * 2, cbvSrvUavDescriptorSize);
		device->CreateShaderResourceView(mNormalMap.Get(), &normalSrvDesc, normalHandle);

		CD3DX12_CPU_DESCRIPTOR_HANDLE depthHandle = sceneInputSrvCpuHandle;
		depthHandle.Offset(frameIndex * 2 + 1, cbvSrvUavDescriptorSize);
		device->CreateShaderResourceView(sceneDepthResource, &depthSrvDesc, depthHandle);
	}
}

D3D12_GPU_DESCRIPTOR_HANDLE SharedNormalPrepass::GetSceneInputNormalSrv(
	UINT sceneInputHeapStartIndex,
	UINT cbvSrvUavDescriptorSize,
	UINT frameIndex) const
{
	(void)sceneInputHeapStartIndex;
	(void)cbvSrvUavDescriptorSize;
	CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSceneInputBaseGpuSrv);
	handle.Offset(static_cast<INT>(frameIndex * 2), cbvSrvUavDescriptorSize);
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE SharedNormalPrepass::GetSceneInputDepthSrv(
	UINT sceneInputHeapStartIndex,
	UINT cbvSrvUavDescriptorSize,
	UINT frameIndex) const
{
	(void)sceneInputHeapStartIndex;
	(void)cbvSrvUavDescriptorSize;
	CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSceneInputBaseGpuSrv);
	handle.Offset(static_cast<INT>(frameIndex * 2 + 1), cbvSrvUavDescriptorSize);
	return handle;
}

void SharedNormalPrepass::BeginPass(
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
	bool clearTargets)
{
	if (cmdList == nullptr || sceneRootSignature == nullptr)
		return;

	SetViewports(cmdList, mViewport, mRect);

	if (clearTargets)
	{
		D3D12_RESOURCE_BARRIER barriers[2];
		UINT barrierCount = 0;
		const bool depthAlreadyInWriteState = mDepthMapStartsInWriteState.exchange(false, std::memory_order_acq_rel);
		if (!depthAlreadyInWriteState)
		{
			barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
				mDepthMap.Get(),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				D3D12_RESOURCE_STATE_DEPTH_WRITE);
		}
		barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(
			mNormalMap.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_RENDER_TARGET);

		if (barrierCount > 0)
			cmdList->ResourceBarrier(barrierCount, barriers);
	}

	cmdList->OMSetRenderTargets(1, &mhNormalMapCpuRtv, true, &mhDepthMapCpuDsv);
	cmdList->SetGraphicsRootSignature(sceneRootSignature);
	cmdList->SetDescriptorHeaps(srvHeapCount, srvDescriptorHeaps);
	cmdList->SetGraphicsRootConstantBufferView(1, passCBAddress);
	cmdList->SetGraphicsRootConstantBufferView(2, lightCBAddress);
	cmdList->SetGraphicsRootDescriptorTable(5, skyTexDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(6, otherTexDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(7, shadow2DDescriptorTable);
	cmdList->SetGraphicsRootDescriptorTable(8, pointLightShadowCubeDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(9, ambientOcclusionDescriptor);
	cmdList->SetGraphicsRootDescriptorTable(10, directionalShadowMaskDescriptor);

	if (clearTargets)
	{
		const float clearNormalMapColor[] = { 0.0f, 0.0f, 1.0f, 0.0f };
		cmdList->ClearRenderTargetView(mhNormalMapCpuRtv, clearNormalMapColor, 0, nullptr);
		cmdList->ClearDepthStencilView(
			mhDepthMapCpuDsv,
			D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
			1.0f,
			0,
			0,
			nullptr);
	}
}

void SharedNormalPrepass::EndPass(ID3D12GraphicsCommandList* cmdList)
{
	if (cmdList == nullptr)
		return;

	D3D12_RESOURCE_BARRIER barriers[2] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(
			mNormalMap.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_GENERIC_READ),
		CD3DX12_RESOURCE_BARRIER::Transition(
			mDepthMap.Get(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_GENERIC_READ)
	};
	cmdList->ResourceBarrier(_countof(barriers), barriers);
}
