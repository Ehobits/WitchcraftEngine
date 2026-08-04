#include "WMaterialFile.h"

#include "WitchcraftXmlValueHelpers.h"

#include <cwchar>
#include <sstream>
#include <vector>

const wchar_t* WMaterialFile::GetRootNodeName() const
{
	return RootNodeName;
}

// 写出材质核心属性与贴图引用。
void WMaterialFile::BuildBody(pugi::xml_node root) const
{
	WitchcraftXmlFileBase::AppendTextNode(root, PUGIXML_TEXT("MaterialName"), m_data.MaterialName);
	WitchcraftXmlValueHelpers::AppendFloat4TextNode(root, PUGIXML_TEXT("DiffuseColor"), m_data.DiffuseColor);
	WitchcraftXmlValueHelpers::AppendFloat3TextNode(root, PUGIXML_TEXT("FresnelR0"), m_data.FresnelR0);
	WitchcraftXmlValueHelpers::AppendFloat3TextNode(root, PUGIXML_TEXT("Emissive"), m_data.Emissive);
	WitchcraftXmlValueHelpers::AppendBoolTextNode(root, PUGIXML_TEXT("UseNormalTexture"), m_data.UseNormalTexture);
	WitchcraftXmlValueHelpers::AppendBoolTextNode(root, PUGIXML_TEXT("UseMetallicTexture"), m_data.UseMetallicTexture);
	WitchcraftXmlValueHelpers::AppendBoolTextNode(root, PUGIXML_TEXT("UseRoughnessTexture"), m_data.UseRoughnessTexture);
	WitchcraftXmlValueHelpers::AppendBoolTextNode(root, PUGIXML_TEXT("UseSpecularTexture"), m_data.UseSpecularTexture);
	WitchcraftXmlValueHelpers::AppendBoolTextNode(root, PUGIXML_TEXT("UseOpacityTexture"), m_data.UseOpacityTexture);
	WitchcraftXmlValueHelpers::AppendFloatTextNode(root, PUGIXML_TEXT("Metallic"), m_data.Metallic);
	WitchcraftXmlValueHelpers::AppendFloatTextNode(root, PUGIXML_TEXT("Roughness"), m_data.Roughness);
	WitchcraftXmlValueHelpers::AppendFloatTextNode(root, PUGIXML_TEXT("Opacity"), m_data.Opacity);

	pugi::xml_node textures = root.append_child(PUGIXML_TEXT("Textures"));
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Diffuse"), m_data.DiffuseTexture);
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Normal"), m_data.NormalTexture);
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Metallic"), m_data.MetallicTexture);
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Roughness"), m_data.RoughnessTexture);
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Opacity"), m_data.OpacityTexture);
}

// 从 XML 读取材质数据；缺省字段保持结构体默认值。
bool WMaterialFile::ReadBody(const pugi::xml_node& root)
{
	WMaterialFileData parsedData;
	parsedData.MaterialName = WitchcraftXmlFileBase::ReadChildText(root, PUGIXML_TEXT("MaterialName"));

	WitchcraftXmlValueHelpers::TryReadFloat4TextOrAttributesNode(root, PUGIXML_TEXT("DiffuseColor"), &parsedData.DiffuseColor);
	WitchcraftXmlValueHelpers::TryReadFloat3TextOrAttributesNode(root, PUGIXML_TEXT("FresnelR0"), &parsedData.FresnelR0);
	WitchcraftXmlValueHelpers::TryReadFloat3TextOrAttributesNode(root, PUGIXML_TEXT("Emissive"), &parsedData.Emissive);
	WitchcraftXmlValueHelpers::TryReadBoolTextNode(root, PUGIXML_TEXT("UseNormalTexture"), &parsedData.UseNormalTexture);
	WitchcraftXmlValueHelpers::TryReadBoolTextNode(root, PUGIXML_TEXT("UseMetallicTexture"), &parsedData.UseMetallicTexture);
	WitchcraftXmlValueHelpers::TryReadBoolTextNode(root, PUGIXML_TEXT("UseRoughnessTexture"), &parsedData.UseRoughnessTexture);
	WitchcraftXmlValueHelpers::TryReadBoolTextNode(root, PUGIXML_TEXT("UseSpecularTexture"), &parsedData.UseSpecularTexture);
	WitchcraftXmlValueHelpers::TryReadBoolTextNode(root, PUGIXML_TEXT("UseOpacityTexture"), &parsedData.UseOpacityTexture);
	WitchcraftXmlValueHelpers::TryReadFloatTextNode(root, PUGIXML_TEXT("Metallic"), &parsedData.Metallic);
	WitchcraftXmlValueHelpers::TryReadFloatTextNode(root, PUGIXML_TEXT("Roughness"), &parsedData.Roughness);
	WitchcraftXmlValueHelpers::TryReadFloatTextNode(root, PUGIXML_TEXT("Opacity"), &parsedData.Opacity);

	const pugi::xml_node textures = root.child(PUGIXML_TEXT("Textures"));
	if (textures)
	{
		parsedData.DiffuseTexture = WitchcraftXmlFileBase::ReadChildText(textures, PUGIXML_TEXT("Diffuse"));
		parsedData.NormalTexture = WitchcraftXmlFileBase::ReadChildText(textures, PUGIXML_TEXT("Normal"));
		parsedData.MetallicTexture = WitchcraftXmlFileBase::ReadChildText(textures, PUGIXML_TEXT("Metallic"));
		parsedData.RoughnessTexture = WitchcraftXmlFileBase::ReadChildText(textures, PUGIXML_TEXT("Roughness"));
		parsedData.OpacityTexture = WitchcraftXmlFileBase::ReadChildText(textures, PUGIXML_TEXT("Opacity"));
	}

	m_data = std::move(parsedData);
	return true;
}

std::wstring WMaterialFile::SerializeToText(const WMaterialFileData& data)
{
	WMaterialFile file;
	file.m_data = data;
	return file.SerializeDocumentToText();
}

bool WMaterialFile::DeserializeFromText(const std::wstring& text, WMaterialFileData* outData)
{
	if (outData == nullptr)
		return false;

	WMaterialFile file;
	if (!file.DeserializeDocumentFromText(text))
		return false;

	*outData = std::move(file.m_data);
	return true;
}

bool WMaterialFile::SaveToFile(const std::filesystem::path& path, const WMaterialFileData& data)
{
	WMaterialFile file;
	file.m_data = data;
	return file.SaveDocumentToFile(path);
}

bool WMaterialFile::LoadFromFile(const std::filesystem::path& path, WMaterialFileData* outData)
{
	if (outData == nullptr)
		return false;

	WMaterialFile file;
	if (!file.LoadDocumentFromFile(path))
		return false;

	*outData = std::move(file.m_data);
	return true;
}

WMaterialFileData WMaterialFile::FromImportedMaterial(const ImportedMaterialInfo& materialInfo)
{
	// 导入器只负责把外部材质信息映射到引擎自己的材质文件数据。
	WMaterialFileData data;
	data.MaterialName = materialInfo.Name;
	data.DiffuseColor = materialInfo.DiffuseColor;
	data.Emissive = materialInfo.Emissive;
	data.Metallic = materialInfo.Metallic;
	data.Roughness = materialInfo.Roughness;
	data.Opacity = materialInfo.Opacity;
	data.DiffuseTexture = materialInfo.DiffuseTexture.AssetName;
	data.NormalTexture = materialInfo.NormalTexture.AssetName;
	data.MetallicTexture = materialInfo.MetallicTexture.AssetName;
	data.RoughnessTexture = materialInfo.RoughnessTexture.AssetName;
	data.OpacityTexture = materialInfo.OpacityTexture.AssetName;
	data.UseNormalTexture = !materialInfo.NormalTexture.AssetName.empty();
	data.UseMetallicTexture = !materialInfo.MetallicTexture.AssetName.empty();
	data.UseRoughnessTexture = !materialInfo.RoughnessTexture.AssetName.empty();
	data.UseSpecularTexture = !materialInfo.SpecularTexture.AssetName.empty();
	data.UseOpacityTexture = materialInfo.UseOpacityTexture && !materialInfo.OpacityTexture.AssetName.empty();
	return data;
}
