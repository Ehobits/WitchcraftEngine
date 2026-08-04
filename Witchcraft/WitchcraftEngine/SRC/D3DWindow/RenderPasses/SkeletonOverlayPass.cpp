#include "SkeletonOverlayPass.h"
#include "../D3DHelpers.h"

void SkeletonOverlayPass::Initialize(ID3D12Device* device)
{
	md3dDevice = device;
}

void SkeletonOverlayPass::CreatePipesAndShaders()
{
	mVertexShader = CompileShader(L"DATA/Shaders/SkeletonOverlay", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/SkeletonOverlay", nullptr, "PS", "ps_5_1");

	D3D12_INPUT_ELEMENT_DESC skeletonOverlayInputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skeletonOverlayPsoDesc = mBasePsoDesc;
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
	ID3D12GraphicsCommandList* cmdList,
	D3D12_GPU_VIRTUAL_ADDRESS objectCBAddress,
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress,
	const D3D12_VIEWPORT& viewport,
	const D3D12_RECT& scissorRect,
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
	const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
	const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
	UINT indexCount)
{
	if (!mRenderData.Visible)
		return;
	if (mRenderData.Vertices.empty() || mRenderData.Indices.empty())
		return;

	cmdList->SetGraphicsRootSignature(mRootSignature.Get());
	cmdList->SetGraphicsRootConstantBufferView(0, objectCBAddress);
	cmdList->SetGraphicsRootConstantBufferView(1, passCBAddress);
	cmdList->SetPipelineState(mPipelineState.Get());
	cmdList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
	cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
	cmdList->IASetIndexBuffer(&indexBufferView);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}
