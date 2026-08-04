#include "SkinningComputePass.h"

#include "../../Helpers/Helpers.h"
#include "../D3DHelpers.h"

void SkinningComputePass::Initialize(ID3D12Device* device)
{
	md3dDevice = device;
}

bool SkinningComputePass::CreateRootSignature()
{
	CD3DX12_ROOT_PARAMETER rootParameters[4];
	rootParameters[0].InitAsShaderResourceView(0);
	rootParameters[1].InitAsUnorderedAccessView(0);
	rootParameters[2].InitAsConstantBufferView(0);
	rootParameters[3].InitAsConstants(4, 1);

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(
		_countof(rootParameters),
		rootParameters,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> serializedRootSignature = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	if (FAILED(D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSignature.GetAddressOf(),
		errorBlob.GetAddressOf())))
	{
		if (errorBlob != nullptr)
			::OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
		EngineHelpers::AddLog(L"[SkinningCompute] CreateRootSignature 失败：D3D12SerializeRootSignature 失败");
		return false;
	}

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSignature->GetBufferPointer(),
		serializedRootSignature->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
	return true;
}

void SkinningComputePass::CreatePipesAndShaders()
{
	mComputeShader = CompileShader(L"DATA/Shaders/SkinningCompute", nullptr, "CSMain", "cs_5_1");

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
	pipelineStateDesc.pRootSignature = mRootSignature.Get();
	pipelineStateDesc.CS = CD3DX12_SHADER_BYTECODE(mComputeShader.Get());
	ThrowIfFailed(md3dDevice->CreateComputePipelineState(
		&pipelineStateDesc,
		IID_PPV_ARGS(mPipelineState.GetAddressOf())));
	SetD3DObjectName(mPipelineState.Get(), L"管线_蒙皮Compute变形");
}

ID3D12RootSignature* SkinningComputePass::GetRootSignature() const
{
	return mRootSignature.Get();
}

ID3D12PipelineState* SkinningComputePass::GetPipelineState() const
{
	return mPipelineState.Get();
}
