#pragma once

#include "D3D12_framework.h"
#include "D3DHelpers.h"

// 创建常量缓冲区
template<typename T>
class UploadBuffer
{
public:
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
		mIsConstantBuffer(isConstantBuffer)
	{
		mElementByteSize = sizeof(T);

		// 常量缓冲区元素必须是 256 字节的倍数。
		// 这是因为硬件只能以 m*256 字节的偏移和 n*256 字节的长度来访问常量数据。
		// typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
		// UINT64 OffsetInBytes; // 256 的倍数
		// UINT   SizeInBytes;   // 256 的倍数
		// } D3D12_CONSTANT_BUFFER_VIEW_DESC;
		if (isConstantBuffer)
			mElementByteSize = CalculateConstantBufferByteSize(sizeof(T));

		D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount);
		ThrowIfFailed(device->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&Desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mUploadBuffer)));

		ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

		// 在使用完资源之前不需要 Unmap。但资源正在被 GPU 使用时不能写入
		// （因此必须使用同步机制）。
	}

	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
	~UploadBuffer()
	{
		if (mUploadBuffer != nullptr)
			mUploadBuffer->Unmap(0, nullptr);

		mMappedData = nullptr;
	}

	ID3D12Resource* Resource()const
	{
		return mUploadBuffer.Get();
	}

	void CopyData(int elementIndex, const T& data)
	{
		memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
	}

private:
	ComPtr<ID3D12Resource> mUploadBuffer = nullptr;
	BYTE* mMappedData = nullptr;
	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};
