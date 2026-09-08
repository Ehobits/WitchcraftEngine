#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <DirectXMath.h>
#include <imgui.h>

#include "Common/SceneEntityType.h"

class Engine;
class WitchcraECS;

class ProjectSettingsWindow
{
public:
	void Init(Engine* engine);
	void Render();
	void Open();

	bool ConsumePendingSceneSettingsReload(
		std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)>* typeColorDraft);
	void CompleteSceneSettingsReload(WitchcraECS* ecs);

private:
	void RenderSceneTypeColorDraftControls(
		std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)>& colorDraft,
		bool* dirty,
		const char* resetButtonLabel);

private:
	Engine* m_engine = nullptr;
	bool m_openProjectSettings = false;
	std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)> m_projectSceneTypeColorDraft = {};
	bool m_projectSceneTypeColorDraftInitialized = false;
	bool m_projectSceneTypeColorDraftDirty = false;
	bool m_pendingSceneSettingsReloadRequested = false;
};
