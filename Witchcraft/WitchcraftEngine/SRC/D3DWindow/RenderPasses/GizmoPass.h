#pragma once

#include "../D3D12_framework.h"
#include "Common/GizmoSharedTypes.h"

// GizmoPass：编辑器 Transform Gizmo 的渲染通道。
class GizmoPass
{
public:
	void Initialize(ID3D12Device* device);

	void CreatePipesAndShaders();

	void SetRenderData(const GizmoRenderData& renderData) { mRenderData = renderData; }
	void ClearRenderData() { mRenderData = {}; }
	bool IsVisible() const { return mRenderData.Visible; }
	GizmoMode GetMode() const { return mRenderData.Mode; }
	const GizmoRenderData& GetRenderData() const { return mRenderData; }

	void Draw(
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
		INT baseVertexLocation);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetBasePsoDesc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) { mBasePsoDesc = desc; mBasePsoDescSet = true; }
	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

private:
	ComPtr<ID3D12Device> mDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;
	GizmoRenderData mRenderData;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC mBasePsoDesc = {};
	bool mBasePsoDescSet = false;
};
