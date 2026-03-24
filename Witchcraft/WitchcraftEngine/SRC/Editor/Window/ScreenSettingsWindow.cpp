#include "ScreenSettingsWindow.h"

void ScreenSettingsWindow::Init(D3DWindow* dx, ID3D12DescriptorHeap* GUISrvDescriptorHeap)
{
	m_dx = dx;
}

void ScreenSettingsWindow::Render()
{
	if (!renderInspector || m_dx == nullptr)
		return;

	enableFPS = m_dx->IsFPSRender();
	float shadowOpacity = m_dx->GetShadowOpacity();
	float shadowSoftness = m_dx->GetShadowSoftness();

	ImGui::Begin("画面设置");
	{
		if (ImGui::Checkbox("显示帧率", &enableFPS))
			m_dx->SetFPSRender(enableFPS);

		if (ImGui::SliderFloat("阴影透明度", &shadowOpacity, 0.0f, 1.0f, "%.2f"))
			m_dx->SetShadowOpacity(shadowOpacity);

		if (ImGui::SliderFloat("阴影柔化", &shadowSoftness, 0.0f, 4.0f, "%.2f"))
			m_dx->SetShadowSoftness(shadowSoftness);
	}
	ImGui::End();
}

void ScreenSettingsWindow::NeedRender(bool render)
{
	renderInspector = render;
}
