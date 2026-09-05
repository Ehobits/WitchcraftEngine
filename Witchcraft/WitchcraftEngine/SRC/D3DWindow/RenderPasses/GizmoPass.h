#pragma once

#include "../D3D12_framework.h"
#include "D3DPassContext.h"
#include "Common/GizmoSharedTypes.h"

// GizmoPass：编辑器 Transform Gizmo 的渲染通道。
class GizmoPass
{
public:
	void Initialize(ID3D12Device* device);

	void CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc);

	void SetRenderData(const GizmoRenderData& renderData) { mRenderData = renderData; }
	void ClearRenderData() { mRenderData = {}; }
	bool IsVisible() const { return mRenderData.Visible; }
	GizmoMode GetMode() const { return mRenderData.Mode; }
	const GizmoRenderData& GetRenderData() const { return mRenderData; }

	void Draw(
		const D3DPassContext& context,
		const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
		const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
		UINT indexCount,
		UINT startIndexLocation,
		INT baseVertexLocation);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

private:
	ComPtr<ID3D12Device> mDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;
	GizmoRenderData mRenderData;
};
