#include "DirectionalShadowMaskPass.h"

void DirectionalShadowMaskPass::Initialize(ID3D12Device* device)
{
	mDevice = device;
}

void DirectionalShadowMaskPass::CreatePipesAndShaders(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc)
{
	mMaskVertexShader = CompileShader(L"DATA/Shaders/DirectionalShadowMask", nullptr, "VS", "vs_5_1");
	mMaskPixelShader = CompileShader(L"DATA/Shaders/DirectionalShadowMask", nullptr, "PS", "ps_5_1");
	mBlurVertexShader = CompileShader(L"DATA/Shaders/DirectionalShadowMaskBlur", nullptr, "VS", "vs_5_1");
	mBlurPixelShader = CompileShader(L"DATA/Shaders/DirectionalShadowMaskBlur", nullptr, "PS", "ps_5_1");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC maskPsoDesc = basePsoDesc;
	maskPsoDesc.InputLayout = { nullptr, 0 };
	maskPsoDesc.pRootSignature = mRootSignature.Get();
	maskPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mMaskVertexShader.Get());
	maskPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mMaskPixelShader.Get());
	maskPsoDesc.RTVFormats[0] = MaskFormat;
	maskPsoDesc.DepthStencilState.DepthEnable = FALSE;
	maskPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	maskPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	maskPsoDesc.SampleDesc.Count = 1;
	maskPsoDesc.SampleDesc.Quality = 0;
	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(
		&maskPsoDesc,
		IID_PPV_ARGS(mMaskPipelineState.ReleaseAndGetAddressOf())));
	SetD3DObjectName(mMaskPipelineState.Get(), L"管线_方向光阴影屏幕遮罩");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC blurPsoDesc = maskPsoDesc;
	blurPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mBlurVertexShader.Get());
	blurPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mMaskPixelShader.Get());
	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(
		&blurPsoDesc,
		IID_PPV_ARGS(mBlurPipelineState.ReleaseAndGetAddressOf())));
	SetD3DObjectName(mBlurPipelineState.Get(), L"管线_方向光阴影屏幕遮罩模糊");
}

void DirectionalShadowMaskPass::CreateRootSignature()
{
	const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] =
	{
		{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0 },
		{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 32, 13, 0 },
		{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0 }
	};

	CD3DX12_ROOT_PARAMETER1 slotRootParameter[6];
	slotRootParameter[0].InitAsConstantBufferView(1);
	slotRootParameter[1].InitAsConstantBufferView(2);
	slotRootParameter[2].InitAsConstants(1, 0);
	slotRootParameter[3].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[5].InitAsDescriptorTable(1, &descriptorRanges[2], D3D12_SHADER_VISIBILITY_PIXEL);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC shadowSampler(
		2,
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		0.0f,
		0,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

	const std::array<CD3DX12_STATIC_SAMPLER_DESC, 3> staticSamplers =
	{
		pointClamp,
		linearClamp,
		shadowSampler
	};

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(
		_countof(slotRootParameter),
		slotRootParameter,
		static_cast<UINT>(staticSamplers.size()),
		staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3DX12SerializeVersionedRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf());
	if (errorBlob != nullptr)
		::OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
	ThrowIfFailed(hr);

	ThrowIfFailed(mDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.ReleaseAndGetAddressOf())));
}

void DirectionalShadowMaskPass::OnResize(UINT width, UINT height)
{
	mWidth = (std::max)(width, 1u);
	mHeight = (std::max)(height, 1u);
	mViewport = CD3DX12_VIEWPORT(
		0.0f,
		0.0f,
		static_cast<float>(mWidth),
		static_cast<float>(mHeight),
		0.0f,
		1.0f);
	mRect = CD3DX12_RECT(0, 0, static_cast<LONG>(mWidth), static_cast<LONG>(mHeight));
	BuildResources();
}

void DirectionalShadowMaskPass::BuildResources()
{
	mMask0 = nullptr;
	mMask1 = nullptr;

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = static_cast<UINT64>(mWidth);
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = MaskFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	const float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	CD3DX12_CLEAR_VALUE clearValue(MaskFormat, clearColor);
	const D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(mDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		&clearValue,
		IID_PPV_ARGS(mMask0.ReleaseAndGetAddressOf())));
	mMask0->SetName(L"DirectionalShadowMask0");
	mMask0State = D3D12_RESOURCE_STATE_GENERIC_READ;

	ThrowIfFailed(mDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		&clearValue,
		IID_PPV_ARGS(mMask1.ReleaseAndGetAddressOf())));
	mMask1->SetName(L"DirectionalShadowMask1");
	mMask1State = D3D12_RESOURCE_STATE_GENERIC_READ;

	BuildDescriptors();
}

void DirectionalShadowMaskPass::BuildDescriptors(
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
	UINT cbvSrvUavDescriptorSize,
	UINT rtvDescriptorSize)
{
	mhMask0CpuSrv = hCpuSrv;
	mhMask1CpuSrv = hCpuSrv;
	mhMask1CpuSrv.Offset(1, cbvSrvUavDescriptorSize);

	mhMask0GpuSrv = hGpuSrv;
	mhMask1GpuSrv = hGpuSrv;
	mhMask1GpuSrv.Offset(1, cbvSrvUavDescriptorSize);

	mhMask0CpuRtv = hCpuRtv;
	mhMask1CpuRtv = hCpuRtv;
	mhMask1CpuRtv.Offset(1, rtvDescriptorSize);

	mSetHandles = true;
	BuildDescriptors();
}

void DirectionalShadowMaskPass::BuildDescriptors()
{
	if (!mhMask0CpuSrv.ptr || !mhMask1CpuSrv.ptr)
		return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = MaskFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	mDevice->CreateShaderResourceView(mMask0.Get(), &srvDesc, mhMask0CpuSrv);
	mDevice->CreateShaderResourceView(mMask1.Get(), &srvDesc, mhMask1CpuSrv);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = MaskFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	mDevice->CreateRenderTargetView(mMask0.Get(), &rtvDesc, mhMask0CpuRtv);
	mDevice->CreateRenderTargetView(mMask1.Get(), &rtvDesc, mhMask1CpuRtv);
}

void DirectionalShadowMaskPass::SetViewport(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->RSSetViewports(1, &mViewport);
	cmdList->RSSetScissorRects(1, &mRect);
}

void DirectionalShadowMaskPass::RecordPasses(
	const D3DPassContext& context,
	ID3D12PipelineState* maskPipelineState,
	ID3D12PipelineState* blurPipelineState)
{
	if (context.CommandList == nullptr || context.DescriptorHeaps == nullptr)
		return;
	if (context.DescriptorHeapCount == 0 || context.PassCBAddress == 0 || context.LightCBAddress == 0)
		return;
	if (context.NormalDepthDescriptor.ptr == 0 || context.ShadowMapDescriptor.ptr == 0)
		return;
	if (maskPipelineState == nullptr || blurPipelineState == nullptr)
		return;

	const float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };

	SetViewport(context.CommandList);
	TransitionMask0(context.CommandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
	context.CommandList->ClearRenderTargetView(mhMask0CpuRtv, clearColor, 0, nullptr);
	context.CommandList->OMSetRenderTargets(1, &mhMask0CpuRtv, true, nullptr);
	context.CommandList->SetGraphicsRootSignature(mRootSignature.Get());
	context.CommandList->SetDescriptorHeaps(context.DescriptorHeapCount, context.DescriptorHeaps);
	context.CommandList->SetGraphicsRootConstantBufferView(0, context.PassCBAddress);
	context.CommandList->SetGraphicsRootConstantBufferView(1, context.LightCBAddress);
	context.CommandList->SetGraphicsRoot32BitConstant(2, 0u, 0);
	context.CommandList->SetGraphicsRootDescriptorTable(3, context.NormalDepthDescriptor);
	context.CommandList->SetGraphicsRootDescriptorTable(4, context.ShadowMapDescriptor);
	context.CommandList->SetGraphicsRootDescriptorTable(5, mhMask0GpuSrv);
	context.CommandList->SetPipelineState(maskPipelineState);
	context.CommandList->IASetVertexBuffers(0, 0, nullptr);
	context.CommandList->IASetIndexBuffer(nullptr);
	context.CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.CommandList->DrawInstanced(6, 1, 0, 0);
	TransitionMask0(context.CommandList, D3D12_RESOURCE_STATE_GENERIC_READ);

	BlurMask(context.CommandList, blurPipelineState, true, context.PassCBAddress, context.LightCBAddress, context.NormalDepthDescriptor, context.ShadowMapDescriptor);
	BlurMask(context.CommandList, blurPipelineState, false, context.PassCBAddress, context.LightCBAddress, context.NormalDepthDescriptor, context.ShadowMapDescriptor);
}

void DirectionalShadowMaskPass::BlurMask(
	ID3D12GraphicsCommandList* cmdList,
	ID3D12PipelineState* blurPipelineState,
	bool horizontalBlur,
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress,
	D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress,
	D3D12_GPU_DESCRIPTOR_HANDLE normalDepthSrvHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE shadow2DDescriptorTable)
{
	ID3D12Resource* input = horizontalBlur ? mMask0.Get() : mMask1.Get();
	ID3D12Resource* output = horizontalBlur ? mMask1.Get() : mMask0.Get();
	D3D12_RESOURCE_STATES* inputState = horizontalBlur ? &mMask0State : &mMask1State;
	D3D12_RESOURCE_STATES* outputState = horizontalBlur ? &mMask1State : &mMask0State;
	CD3DX12_GPU_DESCRIPTOR_HANDLE inputSrv = horizontalBlur ? mhMask0GpuSrv : mhMask1GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE outputRtv = horizontalBlur ? mhMask1CpuRtv : mhMask0CpuRtv;

	TransitionTrackedResourceState(cmdList, input, *inputState, D3D12_RESOURCE_STATE_GENERIC_READ);
	TransitionTrackedResourceState(cmdList, output, *outputState, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	cmdList->ClearRenderTargetView(outputRtv, clearColor, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &outputRtv, true, nullptr);
	cmdList->SetGraphicsRootSignature(mRootSignature.Get());
	cmdList->SetGraphicsRootConstantBufferView(0, passCBAddress);
	cmdList->SetGraphicsRootConstantBufferView(1, lightCBAddress);
	cmdList->SetGraphicsRoot32BitConstant(2, horizontalBlur ? 1u : 0u, 0);
	cmdList->SetGraphicsRootDescriptorTable(3, normalDepthSrvHandle);
	cmdList->SetGraphicsRootDescriptorTable(4, shadow2DDescriptorTable);
	cmdList->SetGraphicsRootDescriptorTable(5, inputSrv);
	cmdList->SetPipelineState(blurPipelineState);
	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, 1, 0, 0);
	cmdList->OMSetRenderTargets(0, nullptr, false, nullptr);

	TransitionTrackedResourceState(cmdList, output, *outputState, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void DirectionalShadowMaskPass::ClearToNeutral(ID3D12GraphicsCommandList* cmdList)
{
	const float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	TransitionMask0(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
	TransitionMask1(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->ClearRenderTargetView(mhMask0CpuRtv, clearColor, 0, nullptr);
	cmdList->ClearRenderTargetView(mhMask1CpuRtv, clearColor, 0, nullptr);
	TransitionMask0(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
	TransitionMask1(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void DirectionalShadowMaskPass::TransitionMask0(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState)
{
	TransitionTrackedResourceState(cmdList, mMask0.Get(), mMask0State, targetState);
}

void DirectionalShadowMaskPass::TransitionMask1(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState)
{
	TransitionTrackedResourceState(cmdList, mMask1.Get(), mMask1State, targetState);
}
