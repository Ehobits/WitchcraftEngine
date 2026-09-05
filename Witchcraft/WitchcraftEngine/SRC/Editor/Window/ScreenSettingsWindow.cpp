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
	float shadowEdgeSoftness = m_dx->GetShadowSoftness();
	bool aoEnabled = m_dx->IsAOEnabled();
	float aoStrength = m_dx->GetAOStrength();
	float aoRadius = m_dx->GetAORadius();
	float aoFadeStart = m_dx->GetAOFadeStart();
	float aoFadeEnd = m_dx->GetAOFadeEnd();
	float aoSurfaceEpsilon = m_dx->GetAOSurfaceEpsilon();
	float aoBlurSigma = m_dx->GetAOBlurSigma();
	bool fxaaEnabled = m_dx->IsFXAAEnabled();
	float fxaaContrastThreshold = m_dx->GetFXAAContrastThreshold();
	float fxaaRelativeThreshold = m_dx->GetFXAARelativeThreshold();
	float fxaaSpanMax = m_dx->GetFXAASpanMax();
	DirectX::XMFLOAT3 colorAdjustWhiteBalance = m_dx->GetColorAdjustWhiteBalance();
	float colorAdjustContrast = m_dx->GetColorAdjustContrast();
	float colorAdjustSaturation = m_dx->GetColorAdjustSaturation();
	float environmentDiffuseIntensity = m_dx->GetEnvironmentDiffuseIntensity();
	float environmentSpecularIntensity = m_dx->GetEnvironmentSpecularIntensity();
	bool environmentBrdfLutEnabled = m_dx->IsEnvironmentBrdfLutEnabled();

	ImGui::Begin("画面设置");
	{
		if (ImGui::Checkbox("显示帧率", &enableFPS))
			m_dx->SetFPSRender(enableFPS);

		if (ImGui::SliderFloat("阴影透明度", &shadowOpacity, 0.0f, 1.0f, "%.2f"))
			m_dx->SetShadowOpacity(shadowOpacity);

		if (ImGui::SliderFloat("阴影边缘柔和度", &shadowEdgeSoftness, 0.0f, 4.0f, "%.2f"))
			m_dx->SetShadowSoftness(shadowEdgeSoftness);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("0 = 更硬的阴影边缘，4 = 更柔和的阴影边缘");

		ImGui::Separator();
		if (ImGui::CollapsingHeader("AO", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Checkbox("启用 AO", &aoEnabled))
				m_dx->SetAOEnabled(aoEnabled);

			if (ImGui::SliderFloat("AO 强度", &aoStrength, 0.0f, 1.0f, "%.2f"))
				m_dx->SetAOStrength(aoStrength);

			if (ImGui::SliderFloat("AO 半径", &aoRadius, 0.0f, 4.0f, "%.2f"))
				m_dx->SetAORadius(aoRadius);

			if (ImGui::SliderFloat("AO 淡出起点", &aoFadeStart, 0.0f, 10.0f, "%.2f"))
				m_dx->SetAOFadeStart(aoFadeStart);

			if (ImGui::SliderFloat("AO 淡出终点", &aoFadeEnd, 0.0f, 10.0f, "%.2f"))
				m_dx->SetAOFadeEnd(aoFadeEnd);

			if (ImGui::SliderFloat("AO 表面偏移", &aoSurfaceEpsilon, 0.0001f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic))
				m_dx->SetAOSurfaceEpsilon(aoSurfaceEpsilon);

			if (ImGui::SliderFloat("AO 模糊 Sigma", &aoBlurSigma, 0.1f, 2.5f, "%.2f"))
				m_dx->SetAOBlurSigma(aoBlurSigma);
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("FXAA", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Checkbox("启用 FXAA", &fxaaEnabled))
				m_dx->SetFXAAEnabled(fxaaEnabled);

			if (ImGui::SliderFloat("FXAA 对比阈值", &fxaaContrastThreshold, 0.0f, 0.25f, "%.4f"))
				m_dx->SetFXAAContrastThreshold(fxaaContrastThreshold);

			if (ImGui::SliderFloat("FXAA 相对阈值", &fxaaRelativeThreshold, 0.0f, 0.5f, "%.4f"))
				m_dx->SetFXAARelativeThreshold(fxaaRelativeThreshold);

			if (ImGui::SliderFloat("FXAA 最大跨度", &fxaaSpanMax, 1.0f, 32.0f, "%.2f"))
				m_dx->SetFXAASpanMax(fxaaSpanMax);
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("色彩调整", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::SliderFloat3("白平衡 RGB", &colorAdjustWhiteBalance.x, 0.2f, 2.0f, "%.2f"))
				m_dx->SetColorAdjustWhiteBalance(colorAdjustWhiteBalance);

			if (ImGui::SliderFloat("对比度", &colorAdjustContrast, 0.0f, 2.0f, "%.2f"))
				m_dx->SetColorAdjustContrast(colorAdjustContrast);

			if (ImGui::SliderFloat("饱和度", &colorAdjustSaturation, 0.0f, 2.0f, "%.2f"))
				m_dx->SetColorAdjustSaturation(colorAdjustSaturation);
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("环境光照", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::SliderFloat("漫反射 IBL", &environmentDiffuseIntensity, 0.0f, 4.0f, "%.2f"))
				m_dx->SetEnvironmentDiffuseIntensity(environmentDiffuseIntensity);

			if (ImGui::SliderFloat("镜面 IBL", &environmentSpecularIntensity, 0.0f, 4.0f, "%.2f"))
				m_dx->SetEnvironmentSpecularIntensity(environmentSpecularIntensity);

			if (ImGui::Checkbox("BRDF LUT", &environmentBrdfLutEnabled))
				m_dx->SetEnvironmentBrdfLutEnabled(environmentBrdfLutEnabled);
		}

	}
	ImGui::End();
}

void ScreenSettingsWindow::NeedRender(bool render)
{
	renderInspector = render;
}
