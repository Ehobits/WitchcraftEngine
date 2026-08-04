#include "GizmoPass.h"
#include "../D3DHelpers.h"

void GizmoPass::Initialize(ID3D12Device* device)
{
	mDevice = device;
}

void GizmoPass::CreatePipesAndShaders()
{
	mVertexShader = CompileShader(L"DATA/Shaders/Gizmo", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/Gizmo", nullptr, "PS", "ps_5_1");

	D3D12_INPUT_ELEMENT_DESC gizmoInputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gizmoPsoDesc = mBasePsoDesc;
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
	ID3D12GraphicsCommandList* cmdList,
	D3D12_GPU_VIRTUAL_ADDRESS objectCBAddress,
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress,
	const D3D12_VIEWPORT& viewport,
	const D3D12_RECT& scissorRect,
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
	const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
	const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
	UINT indexCount,
	UINT startIndexLocation,
	INT baseVertexLocation)
{
	cmdList->SetGraphicsRootSignature(mRootSignature.Get());
	cmdList->SetGraphicsRootConstantBufferView(0, objectCBAddress);
	cmdList->SetGraphicsRootConstantBufferView(1, passCBAddress);
	cmdList->SetPipelineState(mPipelineState.Get());
	cmdList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
	cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
	cmdList->IASetIndexBuffer(&indexBufferView);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawIndexedInstanced(indexCount, 1, startIndexLocation, baseVertexLocation, 0);
}
