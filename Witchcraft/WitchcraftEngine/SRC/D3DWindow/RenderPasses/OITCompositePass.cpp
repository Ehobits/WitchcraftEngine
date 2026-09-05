#include "OITCompositePass.h"
#include "../D3DHelpers.h"

void OITCompositePass::Initialize(ID3D12Device* device)
{
	md3dDevice = device;
}

bool OITCompositePass::CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc)
{
	mVertexShader = CompileShader(L"DATA/Shaders/OITComposite", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/OITComposite", nullptr, "PS", "ps_5_1");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC oitPsoDesc = basePsoDesc;
	oitPsoDesc.InputLayout = { nullptr, 0 };
	oitPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	oitPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mVertexShader.Get());
	oitPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mPixelShader.Get());
	oitPsoDesc.DepthStencilState.DepthEnable = FALSE;
	oitPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&oitPsoDesc, IID_PPV_ARGS(&mPipelineState)));
	SetD3DObjectName(mPipelineState.Get(), L"管线_透明OIT合成");
	return true;
}

void OITCompositePass::Draw(const D3DPassContext& context)
{
	if (context.CommandList == nullptr || context.DescriptorHeaps == nullptr)
		return;
	if (context.DescriptorHeapCount == 0 || context.PostProcessCBAddress == 0)
		return;
	if (context.SceneColorDescriptor.ptr == 0 || context.OitAccumDescriptor.ptr == 0)
		return;

	context.CommandList->SetGraphicsRootSignature(mRootSignature.Get());
	context.CommandList->SetDescriptorHeaps(context.DescriptorHeapCount, context.DescriptorHeaps);
	context.CommandList->SetGraphicsRootConstantBufferView(0, context.PostProcessCBAddress);
	context.CommandList->SetGraphicsRootDescriptorTable(5, context.SceneColorDescriptor);
	context.CommandList->SetGraphicsRootDescriptorTable(6, context.OitAccumDescriptor);
	context.CommandList->OMSetRenderTargets(1, &context.RtvHandle, true, nullptr);
	context.CommandList->SetPipelineState(mPipelineState.Get());
	context.CommandList->IASetVertexBuffers(0, 0, nullptr);
	context.CommandList->IASetIndexBuffer(nullptr);
	context.CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.CommandList->DrawInstanced(6, 1, 0, 0);

	// 恢复默认 OM 绑定
	context.CommandList->OMSetRenderTargets(1, &context.RtvHandle, true, &context.DsvHandle);
}
