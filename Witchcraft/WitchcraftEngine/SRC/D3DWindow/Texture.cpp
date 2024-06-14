#include "Texture.h"

Texture::Texture()
{
	Name = L"";
	FilePath = L"";
}

Texture::Texture(ID3D12Device* device, ID3D12DescriptorHeap* SrvDescriptorHeap, ResourceUploadBatch* resourceUpload, std::wstring name, std::wstring filePath, TextureType type, UINT index)
{
	Create(device, SrvDescriptorHeap, resourceUpload, name, filePath, type, index);
}

Texture::~Texture()
{
}

void Texture::Create(ID3D12Device* device, ID3D12DescriptorHeap* SrvDescriptorHeap, ResourceUploadBatch* resourceUpload, std::wstring name, std::wstring filePath, TextureType type, UINT index)
{
	Name = name;
	FilePath = filePath;
	Index = index;

	if (type == TextureType::PNG)
	{
		ThrowIfFailed(DirectX::CreateWICTextureFromFile(
			device,
			*resourceUpload,
			FilePath.c_str(),
			&textureResource));
	}
	else if (type == TextureType::DDS)
	{
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile(
			device,
			*resourceUpload,
			FilePath.c_str(),
			&textureResource));
	}

	CbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//
	// 用实际的描述符填充堆。
	//
	CPUTexDescriptor = SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	GPUTexDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	// 决定放在堆的那一个位置（用一个index来设置）
	CPUTexDescriptor.Offset(Index, CbvSrvUavDescriptorSize);
	GPUTexDescriptor.Offset(Index, CbvSrvUavDescriptorSize);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	srvDesc.Texture2D.MipLevels = textureResource->GetDesc().MipLevels;
	srvDesc.Format = textureResource->GetDesc().Format;
	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, CPUTexDescriptor);
}

std::wstring Texture::GetName()
{
	return Name;
}

ID3D12Resource* Texture::GetResource()
{
	return textureResource.Get();
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Texture::GetCPUTexDescriptor()
{
	//CPUTexDescriptor.Offset(Index, CbvSrvUavDescriptorSize);
	return CPUTexDescriptor;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE Texture::GetGPUTexDescriptor()
{
	//GPUTexDescriptor.Offset(Index, CbvSrvUavDescriptorSize);
	return GPUTexDescriptor;
}

UINT Texture::GetIndex()
{
	return Index;
}
