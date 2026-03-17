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

	// 读取纹理资源并在共享 SRV 堆中写入对应描述符。
	void Create(ID3D12Device* device, ID3D12DescriptorHeap* SrvDescriptorHeap, ResourceUploadBatch* resourceUpload, std::wstring name, std::wstring filePath, TextureType type, UINT index);
	// 复用已有资源，额外创建一个指向同一纹理的 SRV 槽位。
	void CreateAlias(ID3D12Device* device, ID3D12DescriptorHeap* SrvDescriptorHeap, std::wstring name, ID3D12Resource* resource, UINT index);

	std::wstring GetName();
	ID3D12Resource* GetResource();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPUTexDescriptor();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUTexDescriptor();
	UINT GetIndex();

private:
	std::wstring Name;
	std::wstring FilePath;
	UINT 	Index = 0;
	TextureType Type;

	ComPtr<ID3D12Resource> textureResource = nullptr;
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GPUTexDescriptor;
};
