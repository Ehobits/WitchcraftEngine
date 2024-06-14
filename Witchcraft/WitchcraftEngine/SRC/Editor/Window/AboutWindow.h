#pragma once

#include <xstring>
#include <d3d11.h>
#include <imgui.h>

#include "D3DWindow/D3DWindow.h"

class AboutWindow
{
public:
	void Init(D3DWindow* dx, ID3D12DescriptorHeap* GUISrvDescriptorHeap);
	void Render();
	void Shutdown();

	void NeedRender(bool render);

private:
	bool renderAbout = false;
	
	Texture Longer;
	UINT SrvDescriptorHeapIndex = 1;

	std::wstring _VersionText = L"";
};
