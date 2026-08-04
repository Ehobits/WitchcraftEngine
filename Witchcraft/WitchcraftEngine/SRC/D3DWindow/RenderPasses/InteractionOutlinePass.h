#pragma once

#include "../D3D12_framework.h"
#include "../D3DHelpers.h"

#include <functional>
#include <vector>

class InteractionOutlinePass
{
public:
	static constexpr UINT InteractionPipelineTag = 0x10001u;
	static constexpr UINT OutlinePipelineTag = 0x10002u;

	using DrawCallback = std::function<void(
		ID3D12GraphicsCommandList* commandList,
		ID3D12PipelineState* pipelineState,
		UINT pipelineTag)>;

	// ── 统一接口 ──
	void Initialize(ID3D12Device* device);

	void CreatePipesAndShaders();

	void Draw(
		ID3D12GraphicsCommandList* commandList,
		ID3D12DescriptorHeap* const* descriptorHeaps,
		UINT descriptorHeapCount,
		D3D12_GPU_VIRTUAL_ADDRESS passCbAddress,
		const D3D12_VIEWPORT& viewport,
		const D3D12_RECT& scissorRect,
		D3D12_CPU_DESCRIPTOR_HANDLE mainRtvHandle,
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
		ID3D12Resource* outlineMaskResource,
		D3D12_CPU_DESCRIPTOR_HANDLE outlineMaskRtvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE outlineMaskSrvHandle,
		const DrawCallback& drawCallback);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mOutlinePipelineState.Get(); }
	ID3D12PipelineState* GetInteractionPipelineState() const { return mInteractionPipelineState.Get(); }
	ID3D12PipelineState* GetSkinnedInteractionPipelineState() const { return mSkinnedInteractionPipelineState.Get(); }
	ID3D12PipelineState* GetSkinnedOutlinePipelineState() const { return mSkinnedOutlinePipelineState.Get(); }

	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

	// ── 本 Pass 特有的双 PSO 创建 ──
	void SetPipelineInputs(
		const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElementDescs,
		const std::vector<D3D12_INPUT_ELEMENT_DESC>& skinnedInputElementDescs,
		DXGI_FORMAT backBufferFormat,
		DXGI_FORMAT depthStencilFormat);

private:
	D3D12_GRAPHICS_PIPELINE_STATE_DESC BuildBaseInteractionPsoDesc(bool skinned) const;

	ComPtr<ID3D12Device> md3dDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputElementDescs;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mSkinnedInputElementDescs;
	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_UNKNOWN;

	ComPtr<ID3DBlob> mInteractionVertexShader = nullptr;
	ComPtr<ID3DBlob> mInteractionPixelShader = nullptr;
	ComPtr<ID3DBlob> mOutlineVertexShader = nullptr;
	ComPtr<ID3DBlob> mOutlinePixelShader = nullptr;
	ComPtr<ID3DBlob> mSkinnedInteractionVertexShader = nullptr;
	ComPtr<ID3DBlob> mSkinnedOutlineVertexShader = nullptr;

	ComPtr<ID3D12PipelineState> mInteractionPipelineState = nullptr;
	ComPtr<ID3D12PipelineState> mOutlinePipelineState = nullptr;
	ComPtr<ID3D12PipelineState> mSkinnedInteractionPipelineState = nullptr;
	ComPtr<ID3D12PipelineState> mSkinnedOutlinePipelineState = nullptr;
};
