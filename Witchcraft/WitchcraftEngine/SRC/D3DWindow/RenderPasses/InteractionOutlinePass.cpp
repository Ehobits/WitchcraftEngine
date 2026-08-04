#include "InteractionOutlinePass.h"

#include <cassert>

// ── 统一接口 ──

void InteractionOutlinePass::Initialize(ID3D12Device* device)
{
	md3dDevice = device;
}

void InteractionOutlinePass::Draw(
	ID3D12GraphicsCommandList* commandList,
	ID3D12DescriptorHeap* const* descriptorHeaps,
	UINT descriptorHeapCount,
	D3D12_GPU_VIRTUAL_ADDRESS passCbAddress,
	const D3D12_VIEWPORT& viewport,
	const D3D12_RECT& scissorRect,
	D3D12_CPU_DESCRIPTOR_HANDLE mainRtvHandle,
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
	ID3D12Resource* outlineMaskResource,
	D3D12_CPU_DESCRIPTOR_HANDLE outlineMaskRtvHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE outlineMaskSrvHandle,
	const DrawCallback& drawCallback)
{
	// Pass A: outlineMask 从 SRV 切到 RT，清屏后写入交互遮罩。
	D3D12_RESOURCE_BARRIER toOutlineMaskRenderTarget =
		CD3DX12_RESOURCE_BARRIER::Transition(outlineMaskResource,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
	commandList->ResourceBarrier(1, &toOutlineMaskRenderTarget);

	const float outlineMaskClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	commandList->ClearRenderTargetView(outlineMaskRtvHandle, outlineMaskClearColor, 0, nullptr);

	commandList->SetGraphicsRootSignature(mRootSignature.Get());
	if (descriptorHeaps != nullptr && descriptorHeapCount > 0)
		commandList->SetDescriptorHeaps(descriptorHeapCount, descriptorHeaps);
	commandList->SetGraphicsRootConstantBufferView(1, passCbAddress);
	commandList->OMSetRenderTargets(1, &outlineMaskRtvHandle, true, &dsvHandle);
	drawCallback(commandList, mInteractionPipelineState.Get(), InteractionPipelineTag);

	// Pass B: outlineMask 从 RT 切回 SRV，供描边像素着色器采样。
	D3D12_RESOURCE_BARRIER toOutlineMaskShaderResource =
		CD3DX12_RESOURCE_BARRIER::Transition(outlineMaskResource,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList->ResourceBarrier(1, &toOutlineMaskShaderResource);

	commandList->SetGraphicsRootSignature(mRootSignature.Get());
	if (descriptorHeaps != nullptr && descriptorHeapCount > 0)
		commandList->SetDescriptorHeaps(descriptorHeapCount, descriptorHeaps);
	commandList->SetGraphicsRootConstantBufferView(1, passCbAddress);
	commandList->SetGraphicsRootDescriptorTable(5, outlineMaskSrvHandle);
	commandList->OMSetRenderTargets(1, &mainRtvHandle, true, &dsvHandle);
	drawCallback(commandList, mOutlinePipelineState.Get(), OutlinePipelineTag);
}

// ── 双 PSO 创建 ──

void InteractionOutlinePass::SetPipelineInputs(
	const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElementDescs,
	const std::vector<D3D12_INPUT_ELEMENT_DESC>& skinnedInputElementDescs,
	DXGI_FORMAT backBufferFormat,
	DXGI_FORMAT depthStencilFormat)
{
	mInputElementDescs = inputElementDescs;
	mSkinnedInputElementDescs = skinnedInputElementDescs;
	mBackBufferFormat = backBufferFormat;
	mDepthStencilFormat = depthStencilFormat;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC InteractionOutlinePass::BuildBaseInteractionPsoDesc(bool skinned) const
{
	const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout =
		skinned ? mSkinnedInputElementDescs : mInputElementDescs;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC interactionPsoDesc = {};
	ZeroMemory(&interactionPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	interactionPsoDesc.InputLayout = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };
	interactionPsoDesc.pRootSignature = mRootSignature.Get();
	interactionPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	interactionPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	interactionPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	interactionPsoDesc.SampleMask = UINT_MAX;
	interactionPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	interactionPsoDesc.NumRenderTargets = 1;
	interactionPsoDesc.RTVFormats[0] = mBackBufferFormat;
	interactionPsoDesc.SampleDesc.Count = 1;
	interactionPsoDesc.SampleDesc.Quality = 0;
	interactionPsoDesc.DSVFormat = mDepthStencilFormat;
	interactionPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	interactionPsoDesc.DepthStencilState.DepthEnable = TRUE;
	interactionPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	interactionPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	return interactionPsoDesc;
}

void InteractionOutlinePass::CreatePipesAndShaders()
{
	D3D_SHADER_MACRO skinnedDefines[] =
	{
		{ "SKINNED_MESH", "1" },
		{ nullptr, nullptr }
	};

	mInteractionVertexShader = CompileShader(L"DATA/Shaders/Interaction", nullptr, "VS", "vs_5_1");
	mInteractionPixelShader = CompileShader(L"DATA/Shaders/Interaction", nullptr, "PS", "ps_5_1");
	mOutlineVertexShader = CompileShader(L"DATA/Shaders/Outline", nullptr, "VS", "vs_5_1");
	mOutlinePixelShader = CompileShader(L"DATA/Shaders/Outline", nullptr, "PS", "ps_5_1");
	mSkinnedInteractionVertexShader = CompileShader(L"DATA/Shaders/Interaction", skinnedDefines, "VS", "vs_5_1");
	mSkinnedOutlineVertexShader = CompileShader(L"DATA/Shaders/Outline", skinnedDefines, "VS", "vs_5_1");

	// 阶段1：交互遮罩 PSO。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC interactionPsoDesc = BuildBaseInteractionPsoDesc(false);
	interactionPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mInteractionVertexShader.Get());
	interactionPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mInteractionPixelShader.Get());
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&interactionPsoDesc,
		IID_PPV_ARGS(mInteractionPipelineState.ReleaseAndGetAddressOf())));
	SetD3DObjectName(mInteractionPipelineState.Get(), L"管线_交互遮罩");

	// 阶段2：描边 PSO。除 shader 外复用阶段1配置。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC outlinePsoDesc = interactionPsoDesc;
	outlinePsoDesc.VS = CD3DX12_SHADER_BYTECODE(mOutlineVertexShader.Get());
	outlinePsoDesc.PS = CD3DX12_SHADER_BYTECODE(mOutlinePixelShader.Get());
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&outlinePsoDesc,
		IID_PPV_ARGS(mOutlinePipelineState.ReleaseAndGetAddressOf())));
	SetD3DObjectName(mOutlinePipelineState.Get(), L"管线_交互描边");

	// 阶段3：蒙皮交互遮罩 PSO。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedInteractionPsoDesc = BuildBaseInteractionPsoDesc(true);
	skinnedInteractionPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mSkinnedInteractionVertexShader.Get());
	skinnedInteractionPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mInteractionPixelShader.Get());
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skinnedInteractionPsoDesc,
		IID_PPV_ARGS(mSkinnedInteractionPipelineState.ReleaseAndGetAddressOf())));
	SetD3DObjectName(mSkinnedInteractionPipelineState.Get(), L"管线_蒙皮交互遮罩");

	// 阶段4：蒙皮描边 PSO。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skinnedOutlinePsoDesc = skinnedInteractionPsoDesc;
	skinnedOutlinePsoDesc.VS = CD3DX12_SHADER_BYTECODE(mSkinnedOutlineVertexShader.Get());
	skinnedOutlinePsoDesc.PS = CD3DX12_SHADER_BYTECODE(mOutlinePixelShader.Get());
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skinnedOutlinePsoDesc,
		IID_PPV_ARGS(mSkinnedOutlinePipelineState.ReleaseAndGetAddressOf())));
	SetD3DObjectName(mSkinnedOutlinePipelineState.Get(), L"管线_蒙皮描边");
}
