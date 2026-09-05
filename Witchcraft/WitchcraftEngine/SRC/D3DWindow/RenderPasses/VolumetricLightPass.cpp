#include "VolumetricLightPass.h"
#include "../D3DRenderBindingContract.h"
#include "../D3DHelpers.h"

#include <algorithm>
#include <cmath>

// ── 统一接口 ──

void VolumetricLightPass::Initialize(ID3D12Device* device)
{
	md3dDevice = device;
}

bool VolumetricLightPass::CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc)
{
	mVertexShader = CompileShader(L"DATA/Shaders/VolumetricLight", nullptr, "VS", "vs_5_1");
	mPixelShader = CompileShader(L"DATA/Shaders/VolumetricLight", nullptr, "PS", "ps_5_1");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC volumetricLightPsoDesc = basePsoDesc;
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
	const D3DPassContext& context,
	const std::vector<Light>& Lights,
	UINT shaderLightCount,
	float directionalLightTypeValue,
	float pointLightTypeValue,
	float spotLightTypeValue,
	const PrepareDrawCallback& prepareDrawCallback)
{
	if (context.CommandList == nullptr || context.DescriptorHeaps == nullptr)
		return;
	if (context.DescriptorHeapCount == 0 || context.PassCBAddress == 0 || context.LightCBAddress == 0)
		return;
	if (context.NormalDepthDescriptor.ptr == 0)
		return;

	context.CommandList->SetGraphicsRootSignature(mRootSignature.Get());
	context.CommandList->SetDescriptorHeaps(context.DescriptorHeapCount, context.DescriptorHeaps);
	context.CommandList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::PassCB, context.PassCBAddress);
	context.CommandList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::LightCB, context.LightCBAddress);

	context.CommandList->SetGraphicsRootDescriptorTable(D3DRenderBindingContract::PointLightShadowCubeTable, context.NormalDepthDescriptor);

	context.CommandList->OMSetRenderTargets(1, &context.RtvHandle, true, nullptr);
	context.CommandList->SetPipelineState(mPipelineState.Get());
	context.CommandList->IASetVertexBuffers(0, 0, nullptr);
	context.CommandList->IASetIndexBuffer(nullptr);
	context.CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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

		context.CommandList->SetGraphicsRootConstantBufferView(D3DRenderBindingContract::ObjectCB, objectCbAddress);
		context.CommandList->DrawInstanced(6, 1, 0, 0);
		++drawIndex;
	}
}
