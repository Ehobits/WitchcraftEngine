#pragma once

#include <imgui.h>

#include "AssetsWindow.h"

class D3DWindow;

class FileWindow
{
public:
	void Init(D3DWindow* dx, AssetsWindow* assetsWindow, ID3D12DescriptorHeap* GUISrvDescriptorHeap);
	void Render();

	void NeedRender(bool render);

private:
	bool renderFile = true;

	D3DWindow* m_dx = nullptr;
	AssetsWindow* m_assetsWindow = nullptr;

	ID3D12DescriptorHeap* SrvDescriptorHeap = nullptr;
	UINT SrvDescriptorHeapIndex = 11;
};
