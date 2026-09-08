#include "ProjectSettingsWindow.h"

#include "String/SStringUtils.h"
#include "ECS/WitchcraECS.h"
#include "Engine/Engine.h"

#include <string>

void ProjectSettingsWindow::Init(Engine* engine)
{
	m_engine = engine;
}

void ProjectSettingsWindow::Render()
{
	if (!m_openProjectSettings)
	{
		m_projectSceneTypeColorDraftInitialized = false;
		m_projectSceneTypeColorDraftDirty = false;
		return;
	}
	if (m_engine == nullptr)
		return;

	WitchcraECS* ecs = m_engine->GetECS();
	if (ecs == nullptr)
		return;

	if (!m_projectSceneTypeColorDraftInitialized)
	{
		for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
		{
			const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
			m_projectSceneTypeColorDraft[typeIndex] = ecs->GetEntitySceneTypeVertexColor(sceneType);
		}
		m_projectSceneTypeColorDraftInitialized = true;
		m_projectSceneTypeColorDraftDirty = false;
	}

	if (ImGui::Begin("项目设置", &m_openProjectSettings, ImGuiWindowFlags_NoDocking))
	{
		const bool playModeActive = m_engine != nullptr && m_engine->IsPlayModeActive();
		if (playModeActive)
			ImGui::TextDisabled("Play Mode 中项目设置可查看，但应用/重载场景已锁定。");

		if (ImGui::CollapsingHeader("场景设置", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::TreeNodeEx("实体描边颜色设置", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::TextDisabled("修改不会立即生效，点击“应用并重载场景”后才会生效。");
				ImGui::Separator();

				RenderSceneTypeColorDraftControls(
					m_projectSceneTypeColorDraft,
					&m_projectSceneTypeColorDraftDirty,
					"恢复默认颜色（待应用）");

				ImGui::SameLine();
				ImGui::BeginDisabled(!m_projectSceneTypeColorDraftDirty || playModeActive);
				if (ImGui::Button("应用并重载场景"))
					m_pendingSceneSettingsReloadRequested = true;
				ImGui::EndDisabled();
				ImGui::TreePop();
			}
		}
	}
	ImGui::End();
}

void ProjectSettingsWindow::Open()
{
	m_openProjectSettings = true;
}

bool ProjectSettingsWindow::ConsumePendingSceneSettingsReload(
	std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)>* typeColorDraft)
{
	if (!m_pendingSceneSettingsReloadRequested)
		return false;

	if (typeColorDraft != nullptr)
		*typeColorDraft = m_projectSceneTypeColorDraft;
	m_pendingSceneSettingsReloadRequested = false;
	return true;
}

void ProjectSettingsWindow::CompleteSceneSettingsReload(WitchcraECS* ecs)
{
	if (ecs == nullptr)
		return;

	for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
	{
		const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
		m_projectSceneTypeColorDraft[typeIndex] = ecs->GetEntitySceneTypeVertexColor(sceneType);
	}
	m_projectSceneTypeColorDraftDirty = false;
}

void ProjectSettingsWindow::RenderSceneTypeColorDraftControls(
	std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)>& colorDraft,
	bool* dirty,
	const char* resetButtonLabel)
{
	for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
	{
		const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
		DirectX::XMFLOAT4 typeColor = colorDraft[typeIndex];
		float color[4] = { typeColor.x, typeColor.y, typeColor.z, typeColor.w };
		const std::string label = SString::WstringToUTF8(SceneEntityTypeToDisplayName(sceneType));
		if (ImGui::ColorEdit4(label.c_str(), color))
		{
			colorDraft[typeIndex] = DirectX::XMFLOAT4(color[0], color[1], color[2], color[3]);
			if (dirty != nullptr)
				*dirty = true;
		}
	}

	if (ImGui::Button(resetButtonLabel != nullptr ? resetButtonLabel : "恢复默认颜色"))
	{
		colorDraft = WitchcraECS::BuildDefaultSceneEntityTypeColors();
		if (dirty != nullptr)
			*dirty = true;
	}
}
