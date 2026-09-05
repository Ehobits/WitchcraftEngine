#include "SkeletonOverlayPass.h"
#include "../D3DHelpers.h"

void SkeletonOverlayPass::Initialize(ID3D12Device* device)
{
	md3dDevice = device;
}

void SkeletonOverlayPass::CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc)
{
	mVertexShader = CompileShader(L"DATA/Shaders/SkeletonOverlay", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/SkeletonOverlay", nullptr, "PS", "ps_5_1");

	D3D12_INPUT_ELEMENT_DESC skeletonOverlayInputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skeletonOverlayPsoDesc = basePsoDesc;
	skeletonOverlayPsoDesc.InputLayout = { skeletonOverlayInputLayout, _countof(skeletonOverlayInputLayout) };
	skeletonOverlayPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	skeletonOverlayPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mVertexShader.Get());
	skeletonOverlayPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mPixelShader.Get());
	skeletonOverlayPsoDesc.DepthStencilState.DepthEnable = FALSE;
	skeletonOverlayPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	skeletonOverlayPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	skeletonOverlayPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skeletonOverlayPsoDesc, IID_PPV_ARGS(&mPipelineState)));
	SetD3DObjectName(mPipelineState.Get(), L"管线_骨骼覆盖显示");
}

void SkeletonOverlayPass::Draw(
	const D3DPassContext& context,
	const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
	const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
	UINT indexCount)
{
	if (!mRenderData.Visible)
		return;
	if (mRenderData.Vertices.empty() || mRenderData.Indices.empty())
		return;
	if (context.CommandList == nullptr)
		return;
	if (context.ObjectCBAddress == 0 || context.PassCBAddress == 0)
		return;

	context.CommandList->SetGraphicsRootSignature(mRootSignature.Get());
	context.CommandList->SetGraphicsRootConstantBufferView(0, context.ObjectCBAddress);
	context.CommandList->SetGraphicsRootConstantBufferView(1, context.PassCBAddress);
	context.CommandList->SetPipelineState(mPipelineState.Get());
	context.CommandList->OMSetRenderTargets(1, &context.RtvHandle, true, &context.DsvHandle);
	context.CommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	context.CommandList->IASetIndexBuffer(&indexBufferView);
	context.CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.CommandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}
