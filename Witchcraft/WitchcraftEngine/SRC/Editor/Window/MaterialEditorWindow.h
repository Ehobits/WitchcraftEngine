#pragma once

#include <filesystem>
#include <xstring>

#include <imgui.h>

#include "System/WitchcraftFile/WMaterialFile.h"

class MaterialEditorWindow
{
public:
	void Init();
	void Render();

	void NeedRender(bool render);
	bool OpenMaterialFile(const std::wstring& path);

private:
	void SyncUiFromData();
	void SyncDataFromUi();

	static void WriteUtf8Buffer(const std::wstring& text, char* buffer, size_t bufferSize);
	static std::wstring ReadUtf8Buffer(const char* buffer);

private:
	bool renderMaterialEditor = false;
	bool m_hasLoadedMaterial = false;
	bool m_isDirty = false;

	std::filesystem::path m_currentPath;
	WMaterialFileData m_data;

	char m_materialName[256] = {};
	float m_fresnelR0[3] = { 0.04f, 0.04f, 0.04f };
	char m_diffuseTexture[260] = {};
	char m_normalTexture[260] = {};
	char m_metallicTexture[260] = {};
	char m_roughnessTexture[260] = {};
	char m_opacityTexture[260] = {};

	std::wstring m_statusMessage;
	ImVec4 m_statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
};
