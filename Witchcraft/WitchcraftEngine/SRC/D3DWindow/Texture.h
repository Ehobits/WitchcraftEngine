#pragma once

#include "D3D12_framework.h"
#include "D3DHelpers.h"

enum TextureType : UINT
{
	PNG = 0,
	DDS
};

class Texture
{
public:
	Texture();
	Texture(ID3D12Device* device, ID3D12DescriptorHeap* SrvDescriptorHeap, ResourceUploadBatch* resourceUpload, std::wstring name, std::wstring filePath, TextureType type, UINT index);
	~Texture();

	void Create(ID3D12Device* device, ID3D12DescriptorHeap* SrvDescriptorHeap, ResourceUploadBatch* resourceUpload, std::wstring name, std::wstring filePath, TextureType type, UINT index);

	std::wstring GetName();
	ID3D12Resource* GetResource();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUTexDescriptor();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUTexDescriptor();
	UINT GetIndex();

private:
	std::wstring Name;
	std::wstring FilePath;
	UINT 	Index = 0;

	ComPtr<ID3D12Resource> textureResource = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GPUTexDescriptor;
	UINT CbvSrvUavDescriptorSize = 0;
};