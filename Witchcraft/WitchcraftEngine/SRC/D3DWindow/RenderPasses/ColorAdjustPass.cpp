#include "ColorAdjustPass.h"
#include "../D3DHelpers.h"

void ColorAdjustPass::Initialize(ID3D12Device* device)
{
	mDevice = device;
}

void ColorAdjustPass::CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc)
{
	mVertexShader = CompileShader(L"DATA/Shaders/PostProcess", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/PostProcess", nullptr, "PS", "ps_5_1");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC colorAdjustPsoDesc = basePsoDesc;
	colorAdjustPsoDesc.InputLayout = { nullptr, 0 };
	colorAdjustPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	colorAdjustPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mVertexShader.Get());
	colorAdjustPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mPixelShader.Get());
	colorAdjustPsoDesc.DepthStencilState.DepthEnable = FALSE;
	colorAdjustPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&colorAdjustPsoDesc, IID_PPV_ARGS(&mPipelineState)));
	SetD3DObjectName(mPipelineState.Get(), L"绠＄嚎_鑹插僵璋冩暣鍚庡鐞?");
}

void ColorAdjustPass::Draw(const D3DPassContext& context)
{
	if (context.CommandList == nullptr || context.DescriptorHeaps == nullptr)
		return;
	if (context.DescriptorHeapCount == 0 || context.PostProcessCBAddress == 0)
		return;
	if (context.SceneColorDescriptor.ptr == 0)
		return;

	context.CommandList->SetGraphicsRootSignature(mRootSignature.Get());
	context.CommandList->SetDescriptorHeaps(context.DescriptorHeapCount, context.DescriptorHeaps);
	context.CommandList->SetGraphicsRootConstantBufferView(0, context.PostProcessCBAddress);
	context.CommandList->SetGraphicsRootDescriptorTable(5, context.SceneColorDescriptor);
	context.CommandList->OMSetRenderTargets(1, &context.RtvHandle, true, nullptr);
	context.CommandList->SetPipelineState(mPipelineState.Get());
	context.CommandList->IASetVertexBuffers(0, 0, nullptr);
	context.CommandList->IASetIndexBuffer(nullptr);
	context.CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.CommandList->DrawInstanced(6, 1, 0, 0);

	// 恢复默认 OM 绑定
	context.CommandList->OMSetRenderTargets(1, &context.RtvHandle, true, &context.DsvHandle);
}
