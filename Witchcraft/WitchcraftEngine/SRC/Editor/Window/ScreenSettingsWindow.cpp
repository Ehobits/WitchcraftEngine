#include "ScreenSettingsWindow.h"

void ScreenSettingsWindow::Init(D3DWindow* dx, ID3D12DescriptorHeap* GUISrvDescriptorHeap)
{
	m_dx = dx;
}

void ScreenSettingsWindow::Render()
{
	if (!renderInspector)
		return;

	ImGui::Begin(u8"画面设置");
	{
		if (ImGui::Checkbox(u8"显示帧率", &enableFPS))
			m_dx->SetFPSRender(enableFPS);
	}
	ImGui::End();
}

void ScreenSettingsWindow::NeedRender(bool render)
{
	renderInspector = render;
}
