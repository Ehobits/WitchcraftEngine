#pragma once

#include "../D3D12_framework.h"
#include "../Light.h"

#include <functional>
#include <vector>

class VolumetricLightPass
{
public:
	static constexpr UINT MaxDrawCount = 256u;

	using PrepareDrawCallback = std::function<D3D12_GPU_VIRTUAL_ADDRESS(UINT drawIndex, UINT lightIndex)>;

	// ── 统一接口 ──
	void Initialize(ID3D12Device* device);

	bool CreatePipesAndShaders();

	void SetRenderData() {} // 无逐帧数据

	void Draw(
		ID3D12GraphicsCommandList* commandList,
		ID3D12DescriptorHeap* const* descriptorHeaps,
		UINT descriptorHeapCount,
		D3D12_GPU_VIRTUAL_ADDRESS passCbAddress,
		D3D12_GPU_VIRTUAL_ADDRESS lightCbAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrvHandle,
		ID3D12Resource* depthStencilResource,
		D3D12_CPU_DESCRIPTOR_HANDLE colorRtvHandle,
		D3D12_CPU_DESCRIPTOR_HANDLE depthDsvHandle,
		const std::vector<Light>& Lights,
		UINT shaderLightCount,
		float directionalLightTypeValue,
		float pointLightTypeValue,
		float spotLightTypeValue,
		const PrepareDrawCallback& prepareDrawCallback);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetBasePsoDesc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) { mBasePsoDesc = desc; mBasePsoDescSet = true; }
	void SetSharedRootSignature(ID3D12RootSignature* rootSignature) { mRootSignature = rootSignature; }

private:
	ComPtr<ID3D12Device> md3dDevice = nullptr;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC mBasePsoDesc = {};
	bool mBasePsoDescSet = false;
};
