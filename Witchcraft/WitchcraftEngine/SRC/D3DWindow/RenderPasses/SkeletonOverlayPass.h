#pragma once

#include "../D3D12_framework.h"
#include "D3DPassContext.h"
#include "Common/SkeletonOverlaySharedTypes.h"

// SkeletonOverlayPass：骨骼关节覆层的编辑器渲染通道。
class SkeletonOverlayPass
{
public:
	void Initialize(ID3D12Device* device);

	void CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc);

	void SetRenderData(const SkeletonOverlayRenderData& renderData) { mRenderData = renderData; }
	void ClearRenderData() { mRenderData = {}; }
	bool IsVisible() const { return mRenderData.Visible; }
	const SkeletonOverlayRenderData& GetRenderData() const { return mRenderData; }

	void Draw(
		const D3DPassContext& context,
		const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
		const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
		UINT indexCount);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

private:
	ComPtr<ID3D12Device> md3dDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;
	SkeletonOverlayRenderData mRenderData;
};
