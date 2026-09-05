#include "Texture.h"

#include <png.h>

#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>

Texture::Texture()
{
	Name = L"";
	FilePath = L"";
}

Texture::Texture(ID3D12Device* device, ID3D12DescriptorHeap* SrvDescriptorHeap, ResourceUploadBatch* resourceUpload, std::wstring name, std::wstring filePath, TextureType type, UINT index, bool generateMips)
{
	Create(device, SrvDescriptorHeap, resourceUpload, name, filePath, type, index, generateMips);
}

Texture::~Texture()
{
}

void Texture::Create(ID3D12Device* device, ID3D12DescriptorHeap* SrvDescriptorHeap, ResourceUploadBatch* resourceUpload, std::wstring name, std::wstring filePath, TextureType type, UINT index, bool generateMips)
{
	Name = name;
	FilePath = filePath;
	Index = index;
	Type = type;

	if (Type == TextureType::PNG)
	{
		PngImageData imageData;
		ThrowIfFailed(LoadPngImageData(FilePath, imageData));

		const UINT mipCount = generateMips && resourceUpload->IsSupportedForGenerateMips(DXGI_FORMAT_R8G8B8A8_UNORM)
			? CountTextureMips(imageData.Width, imageData.Height)
			: 1u;
		const D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM,
			imageData.Width,
			imageData.Height,
			1,
			static_cast<UINT16>(mipCount));

		const D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(device->CreateCommittedResource(
			&defaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(textureResource.GetAddressOf())));

		D3D12_SUBRESOURCE_DATA subresourceData = {};
		subresourceData.pData = imageData.Pixels.data();
		subresourceData.RowPitch = static_cast<LONG_PTR>(imageData.Width) * 4;
		subresourceData.SlicePitch = subresourceData.RowPitch * static_cast<LONG_PTR>(imageData.Height);

		resourceUpload->Upload(textureResource.Get(), 0, &subresourceData, 1);
		resourceUpload->Transition(
			textureResource.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		if (mipCount > 1)
			resourceUpload->GenerateMips(textureResource.Get());
	}
	else if (Type == TextureType::DDS)
	{
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile(
			device,
			*resourceUpload,
			FilePath.c_str(),
			&textureResource,
			generateMips));
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

UINT Texture::CountTextureMips(UINT width, UINT height)
{
	UINT mipCount = 1;
	while (width > 1 || height > 1)
	{
		width = (std::max)(1u, width >> 1);
		height = (std::max)(1u, height >> 1);
		++mipCount;
	}
	return mipCount;
}

HRESULT Texture::LoadPngImageData(const std::wstring& filePath, PngImageData& imageData)
{
	FILE* file = nullptr;
	if (_wfopen_s(&file, filePath.c_str(), L"rb") != 0 || file == nullptr)
		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

	std::unique_ptr<FILE, decltype(&std::fclose)> fileGuard(file, &std::fclose);

	png_byte signature[8] = {};
	if (std::fread(signature, 1, sizeof(signature), file) != sizeof(signature))
		return E_FAIL;
	if (png_sig_cmp(signature, 0, sizeof(signature)) != 0)
		return E_INVALIDARG;

	png_structp pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (pngPtr == nullptr)
		return E_OUTOFMEMORY;

	png_infop infoPtr = png_create_info_struct(pngPtr);
	if (infoPtr == nullptr)
	{
		png_destroy_read_struct(&pngPtr, nullptr, nullptr);
		return E_OUTOFMEMORY;
	}

	if (setjmp(png_jmpbuf(pngPtr)) != 0)
	{
		png_destroy_read_struct(&pngPtr, &infoPtr, nullptr);
		return E_FAIL;
	}

	png_init_io(pngPtr, file);
	png_set_sig_bytes(pngPtr, sizeof(signature));
	png_read_info(pngPtr, infoPtr);

	png_uint_32 width = 0;
	png_uint_32 height = 0;
	int bitDepth = 0;
	int colorType = 0;
	png_get_IHDR(pngPtr, infoPtr, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr);

	if (width == 0 || height == 0 || width > UINT_MAX || height > UINT_MAX)
	{
		png_destroy_read_struct(&pngPtr, &infoPtr, nullptr);
		return E_INVALIDARG;
	}

	if (bitDepth == 16)
		png_set_strip_16(pngPtr);
	if (colorType == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(pngPtr);
	if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
		png_set_expand_gray_1_2_4_to_8(pngPtr);
	if (png_get_valid(pngPtr, infoPtr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(pngPtr);
	if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(pngPtr);
	if ((colorType & PNG_COLOR_MASK_ALPHA) == 0)
		png_set_filler(pngPtr, 0xFF, PNG_FILLER_AFTER);

	png_set_interlace_handling(pngPtr);
	png_read_update_info(pngPtr, infoPtr);

	const png_size_t rowBytes = png_get_rowbytes(pngPtr, infoPtr);
	const png_size_t expectedRowBytes = static_cast<png_size_t>(width) * 4u;
	if (rowBytes != expectedRowBytes)
	{
		png_destroy_read_struct(&pngPtr, &infoPtr, nullptr);
		return E_FAIL;
	}

	const UINT64 imageSize = static_cast<UINT64>(rowBytes) * static_cast<UINT64>(height);
	if (imageSize > static_cast<UINT64>((std::numeric_limits<size_t>::max)()))
	{
		png_destroy_read_struct(&pngPtr, &infoPtr, nullptr);
		return E_OUTOFMEMORY;
	}

	imageData.Width = static_cast<UINT>(width);
	imageData.Height = static_cast<UINT>(height);
	imageData.Pixels.resize(static_cast<size_t>(imageSize));

	std::vector<png_bytep> rows(imageData.Height);
	for (UINT rowIndex = 0; rowIndex < imageData.Height; ++rowIndex)
		rows[rowIndex] = imageData.Pixels.data() + static_cast<size_t>(rowIndex) * rowBytes;

	png_read_image(pngPtr, rows.data());
	png_read_end(pngPtr, nullptr);
	png_destroy_read_struct(&pngPtr, &infoPtr, nullptr);
	return S_OK;
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
