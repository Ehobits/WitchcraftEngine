#include "VolumetricLightPass.h"
#include "../D3DHelpers.h"

#include <algorithm>
#include <cmath>

// ── 统一接口 ──

void VolumetricLightPass::Initialize(ID3D12Device* device)
{
	md3dDevice = device;
}

bool VolumetricLightPass::CreatePipesAndShaders()
{
	if (!mBasePsoDescSet)
		return false;

	mVertexShader = CompileShader(L"DATA/Shaders/VolumetricLight", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/VolumetricLight", nullptr, "PS", "ps_5_1");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC volumetricLightPsoDesc = mBasePsoDesc;
	volumetricLightPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	volumetricLightPsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	volumetricLightPsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	volumetricLightPsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	volumetricLightPsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	volumetricLightPsoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	volumetricLightPsoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	volumetricLightPsoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	volumetricLightPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	volumetricLightPsoDesc.VS = CD3DX12_SHADER_BYTECODE(mVertexShader.Get());
	volumetricLightPsoDesc.PS = CD3DX12_SHADER_BYTECODE(mPixelShader.Get());
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
		&volumetricLightPsoDesc,
		IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf())));
	SetD3DObjectName(mPipelineState.Get(), L"管线_体积光_全屏逐灯");
	return true;
}

void VolumetricLightPass::Draw(
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
	const PrepareDrawCallback& prepareDrawCallback)
{
	(void)depthStencilResource;
	(void)depthDsvHandle;

	commandList->SetGraphicsRootSignature(mRootSignature.Get());
	if (descriptorHeaps != nullptr && descriptorHeapCount > 0)
		commandList->SetDescriptorHeaps(descriptorHeapCount, descriptorHeaps);
	commandList->SetGraphicsRootConstantBufferView(1, passCbAddress);
	commandList->SetGraphicsRootConstantBufferView(2, lightCbAddress);

	commandList->SetGraphicsRootDescriptorTable(7, sceneDepthSrvHandle);

	commandList->OMSetRenderTargets(1, &colorRtvHandle, true, nullptr);
	commandList->SetPipelineState(mPipelineState.Get());
	commandList->IASetVertexBuffers(0, 0, nullptr);
	commandList->IASetIndexBuffer(nullptr);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const UINT lightCount = (std::min)(shaderLightCount, static_cast<UINT>(Lights.size()));
	const int directionalLightType = static_cast<int>(std::lround(directionalLightTypeValue));
	const int pointLightType = static_cast<int>(std::lround(pointLightTypeValue));
	const int spotLightType = static_cast<int>(std::lround(spotLightTypeValue));

	UINT drawIndex = 0;
	for (UINT lightIndex = 0; lightIndex < lightCount && drawIndex < MaxDrawCount; ++lightIndex)
	{
		const Light& light = Lights[lightIndex];
		const int lightType = static_cast<int>(std::lround(light.Type));
		const bool isDirectional = (lightType == directionalLightType);
		const bool isPointOrSpot = (lightType == pointLightType) || (lightType == spotLightType);
		if ((!isPointOrSpot && !isDirectional) ||
			!light.EnableVolumetric ||
			light.Power <= 0.0f ||
			light.VolumetricIntensity <= 0.0f ||
			light.VolumetricAttenuationDistance <= 0.0f)
		{
			continue;
		}

		if (!prepareDrawCallback)
			continue;

		const D3D12_GPU_VIRTUAL_ADDRESS objectCbAddress = prepareDrawCallback(drawIndex, lightIndex);
		if (objectCbAddress == 0)
			continue;

		commandList->SetGraphicsRootConstantBufferView(0, objectCbAddress);
		commandList->DrawInstanced(6, 1, 0, 0);
		++drawIndex;
	}
}
