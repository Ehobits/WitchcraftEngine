#include "MaterialEditorWindow.h"

#include <cstring>

#include "String/SStringUtils.h"

void MaterialEditorWindow::Init()
{
}

void MaterialEditorWindow::Render()
{
	if (!renderMaterialEditor)
		return;

	std::string title = "材质编辑器###MaterialEditorWindow";

	if (!ImGui::Begin(title.c_str(), &renderMaterialEditor))
	{
		ImGui::End();
		return;
	}

	if (!m_hasLoadedMaterial)
	{
		ImGui::TextDisabled("未打开材质文件。");
		ImGui::End();
		return;
	}

	const std::wstring displayMaterialName =
		m_data.MaterialName.empty() ? m_currentPath.stem().wstring() : m_data.MaterialName;
	ImGui::TextWrapped("材质名：%s", SString::WstringToUTF8(displayMaterialName).c_str());
	if (!m_statusMessage.empty())
	{
		const std::string utf8Status = SString::WstringToUTF8(m_statusMessage);
		ImGui::TextColored(m_statusColor, "%s", utf8Status.c_str());
	}
	if (m_isDirty)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "有未保存修改");
	}

	if (ImGui::Button("保存"))
	{
		SyncDataFromUi();
		if (WMaterialFile::SaveToFile(m_currentPath, m_data))
		{
			m_isDirty = false;
			m_statusMessage = L"保存成功";
			m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
		}
		else
		{
			m_statusMessage = L"保存失败";
			m_statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("重新加载"))
	{
		OpenMaterialFile(m_currentPath.wstring());
	}

	ImGui::Separator();

	bool changed = false;
	changed |= ImGui::InputText("材质名称", m_materialName, IM_ARRAYSIZE(m_materialName));
	changed |= ImGui::ColorEdit4("漫反射颜色", &m_data.DiffuseColor.x);
	changed |= ImGui::ColorEdit3("自发光", &m_data.Emissive.x);
	changed |= ImGui::Checkbox("使用金属度贴图", &m_data.UseMetallicTexture);
	changed |= ImGui::Checkbox("使用透明贴图", &m_data.UseOpacityTexture);
	changed |= ImGui::SliderFloat("金属度", &m_data.Metallic, 0.0f, 1.0f);
	changed |= ImGui::SliderFloat("粗糙度", &m_data.Roughness, 0.0f, 1.0f);
	float transparency = 1.0f - m_data.Opacity;
	if (ImGui::SliderFloat("透明度", &transparency, 0.0f, 1.0f))
	{
		m_data.Opacity = 1.0f - transparency;
		changed = true;
	}

	ImGui::Separator();
	ImGui::TextDisabled("贴图文件名相对于同级 ImportedAssets/.../Textures 目录保存。");
	changed |= ImGui::InputText("漫反射贴图", m_diffuseTexture, IM_ARRAYSIZE(m_diffuseTexture));
	changed |= ImGui::InputText("法线贴图", m_normalTexture, IM_ARRAYSIZE(m_normalTexture));
	changed |= ImGui::InputText("金属度贴图", m_metallicTexture, IM_ARRAYSIZE(m_metallicTexture));
	changed |= ImGui::InputText("粗糙度贴图", m_roughnessTexture, IM_ARRAYSIZE(m_roughnessTexture));
	changed |= ImGui::InputText("透明贴图", m_opacityTexture, IM_ARRAYSIZE(m_opacityTexture));

	if (changed)
	{
		m_isDirty = true;
		m_statusMessage.clear();
	}

	ImGui::End();
}

void MaterialEditorWindow::NeedRender(bool render)
{
	renderMaterialEditor = render;
}

bool MaterialEditorWindow::OpenMaterialFile(const std::wstring& path)
{
	WMaterialFileData loadedData;
	if (!WMaterialFile::LoadFromFile(path, &loadedData))
	{
		m_statusMessage = L"打开材质失败";
		m_statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		return false;
	}

	m_currentPath = path;
	m_data = loadedData;
	m_hasLoadedMaterial = true;
	m_isDirty = false;
	renderMaterialEditor = true;
	m_statusMessage = L"已加载材质";
	m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
	SyncUiFromData();
	return true;
}

void MaterialEditorWindow::SyncUiFromData()
{
	WriteUtf8Buffer(m_data.MaterialName, m_materialName, IM_ARRAYSIZE(m_materialName));
	WriteUtf8Buffer(m_data.DiffuseTexture, m_diffuseTexture, IM_ARRAYSIZE(m_diffuseTexture));
	WriteUtf8Buffer(m_data.NormalTexture, m_normalTexture, IM_ARRAYSIZE(m_normalTexture));
	WriteUtf8Buffer(m_data.MetallicTexture, m_metallicTexture, IM_ARRAYSIZE(m_metallicTexture));
	WriteUtf8Buffer(m_data.RoughnessTexture, m_roughnessTexture, IM_ARRAYSIZE(m_roughnessTexture));
	WriteUtf8Buffer(m_data.OpacityTexture, m_opacityTexture, IM_ARRAYSIZE(m_opacityTexture));
}

void MaterialEditorWindow::SyncDataFromUi()
{
	m_data.MaterialName = ReadUtf8Buffer(m_materialName);
	m_data.DiffuseTexture = ReadUtf8Buffer(m_diffuseTexture);
	m_data.NormalTexture = ReadUtf8Buffer(m_normalTexture);
	m_data.MetallicTexture = ReadUtf8Buffer(m_metallicTexture);
	m_data.RoughnessTexture = ReadUtf8Buffer(m_roughnessTexture);
	m_data.OpacityTexture = ReadUtf8Buffer(m_opacityTexture);
}

void MaterialEditorWindow::WriteUtf8Buffer(const std::wstring& text, char* buffer, size_t bufferSize)
{
	if (buffer == nullptr || bufferSize == 0)
		return;

	const std::string utf8Text = SString::WstringToUTF8(text);
	strncpy_s(buffer, bufferSize, utf8Text.c_str(), _TRUNCATE);
}

std::wstring MaterialEditorWindow::ReadUtf8Buffer(const char* buffer)
{
	if (buffer == nullptr)
		return L"";

	return SString::UTF8ToWstring(buffer);
}
