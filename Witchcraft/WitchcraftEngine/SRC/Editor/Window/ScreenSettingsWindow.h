#include <imgui.h>

#include "D3DWindow/D3DWindow.h"

class D3DWindow;

class ScreenSettingsWindow
{
public:
	void Init(D3DWindow* dx, ID3D12DescriptorHeap* GUISrvDescriptorHeap);
	void Render();

	void NeedRender(bool render);

private:
	bool renderInspector = true;
	bool enableFPS = true;

	D3DWindow* m_dx = nullptr;

};
