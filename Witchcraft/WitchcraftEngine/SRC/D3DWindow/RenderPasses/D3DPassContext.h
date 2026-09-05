#pragma once

#include "../D3D12_framework.h"

struct D3DPassContext
{
	ID3D12GraphicsCommandList* CommandList = nullptr;
	ID3D12DescriptorHeap* const* DescriptorHeaps = nullptr;
	UINT DescriptorHeapCount = 0;
	D3D12_VIEWPORT Viewport = {};
	D3D12_RECT ScissorRect = {};
	D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = {};
	D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle = {};
	D3D12_GPU_VIRTUAL_ADDRESS ObjectCBAddress = 0;
	D3D12_GPU_VIRTUAL_ADDRESS PassCBAddress = 0;
	D3D12_GPU_VIRTUAL_ADDRESS LightCBAddress = 0;
	D3D12_GPU_VIRTUAL_ADDRESS PostProcessCBAddress = 0;
	D3D12_GPU_VIRTUAL_ADDRESS AoCBAddress = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE SceneColorDescriptor = {};
	D3D12_GPU_DESCRIPTOR_HANDLE OitAccumDescriptor = {};
	D3D12_GPU_DESCRIPTOR_HANDLE NormalDepthDescriptor = {};
	D3D12_GPU_DESCRIPTOR_HANDLE ShadowMapDescriptor = {};
};
