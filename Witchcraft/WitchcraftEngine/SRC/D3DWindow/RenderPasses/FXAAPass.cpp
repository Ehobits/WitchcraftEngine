#include "FXAAPass.h"
#include "../D3DHelpers.h"

void FXAAPass::Initialize(ID3D12Device* device)
{
	mDevice = device;
}

void FXAAPass::CreatePipesAndShaders()
{
	mVertexShader = CompileShader(L"DATA/Shaders/FXAA", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/FXAA", nullptr, "PS", "ps_5_1");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC fxaaPsoDesc = mBasePsoDesc;
	fxaaPsoDesc.InputLayout = { nullptr, 0 };
	fxaaPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	fxaaPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mVertexShader.Get());
	fxaaPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mPixelShader.Get());
	fxaaPsoDesc.DepthStencilState.DepthEnable = FALSE;
	fxaaPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&fxaaPsoDesc, IID_PPV_ARGS(&mPipelineState)));
	SetD3DObjectName(mPipelineState.Get(), L"管线_FXAA后处理");
}

void FXAAPass::Draw(
	ID3D12GraphicsCommandList* cmdList,
	ID3D12DescriptorHeap* const* descriptorHeaps,
	UINT descriptorHeapCount,
	D3D12_GPU_VIRTUAL_ADDRESS postProcessCBAddress,
	D3D12_GPU_DESCRIPTOR_HANDLE sceneColorDescriptor,
	const D3D12_VIEWPORT& viewport,
	const D3D12_RECT& scissorRect,
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
	cmdList->SetGraphicsRootSignature(mRootSignature.Get());
	cmdList->SetDescriptorHeaps(descriptorHeapCount, descriptorHeaps);
	cmdList->SetGraphicsRootConstantBufferView(0, postProcessCBAddress);
	cmdList->SetGraphicsRootDescriptorTable(5, sceneColorDescriptor);
	cmdList->OMSetRenderTargets(1, &rtvHandle, true, nullptr);
	cmdList->SetPipelineState(mPipelineState.Get());
	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, 1, 0, 0);

	// 恢复默认 OM 绑定
	cmdList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
}
