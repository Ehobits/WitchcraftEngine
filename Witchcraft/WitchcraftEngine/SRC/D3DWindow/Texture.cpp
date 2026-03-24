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
	Type = type;

	// 根据资源格式选择对应的 DirectXTex / WIC 加载路径。
	if (Type == TextureType::PNG)
	{
		ThrowIfFailed(DirectX::CreateWICTextureFromFile(
			device,
			*resourceUpload,
			FilePath.c_str(),
			&textureResource));
	}
	else if (Type == TextureType::DDS)
	{
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile(
			device,
			*resourceUpload,
			FilePath.c_str(),
			&textureResource));
	}

	CreateSrvDescriptor(device, SrvDescriptorHeap, textureResource.Get(), Index, CPUTexDescriptor, GPUTexDescriptor);
}

void Texture::CreateAlias(ID3D12Device* device, ID3D12DescriptorHeap* SrvDescriptorHeap, std::wstring name, ID3D12Resource* resource, UINT index)
{
	Name = name;
	FilePath = L"";
	Index = index;
	Type = TextureType::PNG;
	textureResource = resource;

	if (textureResource == nullptr)
		return;

	CreateSrvDescriptor(device, SrvDescriptorHeap, textureResource.Get(), Index, CPUTexDescriptor, GPUTexDescriptor);
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
	return CPUTexDescriptor;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE Texture::GetGPUTexDescriptor()
{
	return GPUTexDescriptor;
}

UINT Texture::GetIndex()
{
	return Index;
}

void Texture::CreateSrvDescriptor(ID3D12Device* device, ID3D12DescriptorHeap* srvDescriptorHeap, ID3D12Resource* resource, UINT index, CD3DX12_CPU_DESCRIPTOR_HANDLE& cpuDescriptor, CD3DX12_GPU_DESCRIPTOR_HANDLE& gpuDescriptor)
{
	UINT cbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	cpuDescriptor = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	gpuDescriptor = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	cpuDescriptor.Offset(index, cbvSrvUavDescriptorSize);
	gpuDescriptor.Offset(index, cbvSrvUavDescriptorSize);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.MipLevels = resource->GetDesc().MipLevels;
	srvDesc.Format = resource->GetDesc().Format;
	device->CreateShaderResourceView(resource, &srvDesc, cpuDescriptor);
}