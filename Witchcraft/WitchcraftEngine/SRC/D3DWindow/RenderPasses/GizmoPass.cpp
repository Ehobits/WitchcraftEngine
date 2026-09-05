#include "GizmoPass.h"
#include "../D3DHelpers.h"

void GizmoPass::Initialize(ID3D12Device* device)
{
	mDevice = device;
}

void GizmoPass::CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc)
{
	mVertexShader = CompileShader(L"DATA/Shaders/Gizmo", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/Gizmo", nullptr, "PS", "ps_5_1");

	D3D12_INPUT_ELEMENT_DESC gizmoInputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gizmoPsoDesc = basePsoDesc;
	gizmoPsoDesc.InputLayout = { gizmoInputLayout, _countof(gizmoInputLayout) };
	gizmoPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gizmoPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mVertexShader.Get());
	gizmoPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mPixelShader.Get());
	gizmoPsoDesc.DepthStencilState.DepthEnable = FALSE;
	gizmoPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	gizmoPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	gizmoPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&gizmoPsoDesc, IID_PPV_ARGS(&mPipelineState)));
	SetD3DObjectName(mPipelineState.Get(), L"管线_编辑器Gizmo");
}

void GizmoPass::Draw(
	const D3DPassContext& context,
	const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
	const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
	UINT indexCount,
	UINT startIndexLocation,
	INT baseVertexLocation)
{
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
	context.CommandList->DrawIndexedInstanced(indexCount, 1, startIndexLocation, baseVertexLocation, 0);
}
