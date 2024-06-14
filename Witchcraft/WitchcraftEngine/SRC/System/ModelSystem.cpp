#include "ModelSystem.h"
#include "HELPERS/Helpers.h"

struct ConstantBuffer
{
	DirectX::XMMATRIX sProjection;
	DirectX::XMMATRIX sView;
	DirectX::XMMATRIX sModel;
};

bool ModelSystem::Init(D3DWindow* dx)
{
	ComPtr<ID3DBlob> VS = nullptr;
	ComPtr<ID3DBlob> PS = nullptr;

	// 定义顶点输入布局。
	std::vector<D3D12_INPUT_ELEMENT_DESC> InputElementDescs =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	///////////////////////////////////////////////////////////

	//D3D11_BUFFER_DESC bd;
	//ZeroMemory(&bd, sizeof(D3D11_BUFFER_DESC));
	//bd.Usage = D3D11_USAGE_DEFAULT;
	//bd.ByteWidth = sizeof(ConstantBuffer);
	//bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	//bd.CPUAccessFlags = 0;
	//if (FAILED(dx->GetDevice()->CreateBuffer(&bd, nullptr, &pConstantBuffer)))
	//	return false;

	///////////////////////////////////////////////////////////

	//VS->Release();
	//PS->Release();

	///////////////////////////////////////////////////////////

	//D3D11_SAMPLER_DESC samplerDesc;
	//ZeroMemory(&samplerDesc, sizeof(D3D11_SAMPLER_DESC));
	//samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	//samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	//samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	//samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	//samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	//samplerDesc.MinLOD = 0;
	//samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	//if (FAILED(dx->GetDevice()->CreateSamplerState(&samplerDesc, &pSamplerState)))
	//	return false;

	///////////////////////////////////////////////////////////

	return true;
}

void ModelSystem::Shutdown()
{
}