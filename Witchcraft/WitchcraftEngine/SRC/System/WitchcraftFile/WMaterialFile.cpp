#include "WMaterialFile.h"

#include <cwchar>
#include <sstream>
#include <vector>

namespace
{
	std::vector<std::wstring> Split(const std::wstring& value, wchar_t delimiter)
	{
		std::vector<std::wstring> result;
		std::wstringstream stream(value);
		std::wstring item;
		while (std::getline(stream, item, delimiter))
			result.push_back(WitchcraftXmlFileBase::Trim(item));
		return result;
	}

	std::wstring BoolToText(bool value)
	{
		return value ? L"true" : L"false";
	}

	bool TextToBool(const std::wstring& value)
	{
		return _wcsicmp(value.c_str(), L"true") == 0 || value == L"1";
	}

	bool ParseFloat4Text(const std::wstring& text, DirectX::XMFLOAT4* outValue)
	{
		if (outValue == nullptr)
			return false;

		std::vector<std::wstring> parts = Split(text, L',');
		if (parts.size() != 4)
			return false;

		outValue->x = std::stof(parts[0]);
		outValue->y = std::stof(parts[1]);
		outValue->z = std::stof(parts[2]);
		outValue->w = std::stof(parts[3]);
		return true;
	}

	bool ParseFloat3Text(const std::wstring& text, DirectX::XMFLOAT3* outValue)
	{
		if (outValue == nullptr)
			return false;

		std::vector<std::wstring> parts = Split(text, L',');
		if (parts.size() != 3)
			return false;

		outValue->x = std::stof(parts[0]);
		outValue->y = std::stof(parts[1]);
		outValue->z = std::stof(parts[2]);
		return true;
	}

	void AppendBoolNode(pugi::xml_node parent, const pugi::char_t* nodeName, bool value)
	{
		WitchcraftXmlFileBase::AppendTextNode(parent, nodeName, BoolToText(value));
	}

	void AppendFloatNode(pugi::xml_node parent, const pugi::char_t* nodeName, float value)
	{
		parent.append_child(nodeName).text().set(value);
	}

	void AppendFloat4Node(pugi::xml_node parent, const pugi::char_t* nodeName, const DirectX::XMFLOAT4& value)
	{
		pugi::xml_node node = parent.append_child(nodeName);
		node.append_attribute(PUGIXML_TEXT("x")).set_value(value.x);
		node.append_attribute(PUGIXML_TEXT("y")).set_value(value.y);
		node.append_attribute(PUGIXML_TEXT("z")).set_value(value.z);
		node.append_attribute(PUGIXML_TEXT("w")).set_value(value.w);
	}

	void AppendFloat3Node(pugi::xml_node parent, const pugi::char_t* nodeName, const DirectX::XMFLOAT3& value)
	{
		pugi::xml_node node = parent.append_child(nodeName);
		node.append_attribute(PUGIXML_TEXT("x")).set_value(value.x);
		node.append_attribute(PUGIXML_TEXT("y")).set_value(value.y);
		node.append_attribute(PUGIXML_TEXT("z")).set_value(value.z);
	}

	bool TryReadFloatNode(const pugi::xml_node& parent, const pugi::char_t* nodeName, float* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		const std::wstring text = WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(node.text().as_string()));
		if (text.empty())
			return false;

		*outValue = std::stof(text);
		return true;
	}

	bool TryReadBoolNode(const pugi::xml_node& parent, const pugi::char_t* nodeName, bool* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		*outValue = TextToBool(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(node.text().as_string())));
		return true;
	}

	bool TryReadFloat4Node(const pugi::xml_node& parent, const pugi::char_t* nodeName, DirectX::XMFLOAT4* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		const pugi::xml_attribute x = node.attribute(PUGIXML_TEXT("x"));
		const pugi::xml_attribute y = node.attribute(PUGIXML_TEXT("y"));
		const pugi::xml_attribute z = node.attribute(PUGIXML_TEXT("z"));
		const pugi::xml_attribute w = node.attribute(PUGIXML_TEXT("w"));
		if (x && y && z && w)
		{
			outValue->x = x.as_float();
			outValue->y = y.as_float();
			outValue->z = z.as_float();
			outValue->w = w.as_float();
			return true;
		}

		return ParseFloat4Text(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(node.text().as_string())), outValue);
	}

	bool TryReadFloat3Node(const pugi::xml_node& parent, const pugi::char_t* nodeName, DirectX::XMFLOAT3* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		const pugi::xml_attribute x = node.attribute(PUGIXML_TEXT("x"));
		const pugi::xml_attribute y = node.attribute(PUGIXML_TEXT("y"));
		const pugi::xml_attribute z = node.attribute(PUGIXML_TEXT("z"));
		if (x && y && z)
		{
			outValue->x = x.as_float();
			outValue->y = y.as_float();
			outValue->z = z.as_float();
			return true;
		}

		return ParseFloat3Text(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(node.text().as_string())), outValue);
	}
}

const wchar_t* WMaterialFile::GetRootNodeName() const
{
	return RootNodeName;
}

void WMaterialFile::BuildBody(pugi::xml_node root) const
{
	WitchcraftXmlFileBase::AppendTextNode(root, PUGIXML_TEXT("MaterialName"), m_data.MaterialName);
	AppendFloat4Node(root, PUGIXML_TEXT("DiffuseColor"), m_data.DiffuseColor);
	AppendFloat3Node(root, PUGIXML_TEXT("Emissive"), m_data.Emissive);
	AppendBoolNode(root, PUGIXML_TEXT("UseNormalTexture"), m_data.UseNormalTexture);
	AppendBoolNode(root, PUGIXML_TEXT("UseMetallicTexture"), m_data.UseMetallicTexture);
	AppendBoolNode(root, PUGIXML_TEXT("UseRoughnessTexture"), m_data.UseRoughnessTexture);
	AppendBoolNode(root, PUGIXML_TEXT("UseOpacityTexture"), m_data.UseOpacityTexture);
	AppendFloatNode(root, PUGIXML_TEXT("Metallic"), m_data.Metallic);
	AppendFloatNode(root, PUGIXML_TEXT("Roughness"), m_data.Roughness);
	AppendFloatNode(root, PUGIXML_TEXT("Opacity"), m_data.Opacity);

	pugi::xml_node textures = root.append_child(PUGIXML_TEXT("Textures"));
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Diffuse"), m_data.DiffuseTexture);
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Normal"), m_data.NormalTexture);
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Metallic"), m_data.MetallicTexture);
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Roughness"), m_data.RoughnessTexture);
	WitchcraftXmlFileBase::AppendTextNode(textures, PUGIXML_TEXT("Opacity"), m_data.OpacityTexture);
}

bool WMaterialFile::ReadBody(const pugi::xml_node& root)
{
	WMaterialFileData parsedData;
	parsedData.MaterialName = WitchcraftXmlFileBase::ReadChildText(root, PUGIXML_TEXT("MaterialName"));

	TryReadFloat4Node(root, PUGIXML_TEXT("DiffuseColor"), &parsedData.DiffuseColor);
	TryReadFloat3Node(root, PUGIXML_TEXT("Emissive"), &parsedData.Emissive);
	TryReadBoolNode(root, PUGIXML_TEXT("UseNormalTexture"), &parsedData.UseNormalTexture);
	TryReadBoolNode(root, PUGIXML_TEXT("UseMetallicTexture"), &parsedData.UseMetallicTexture);
	TryReadBoolNode(root, PUGIXML_TEXT("UseRoughnessTexture"), &parsedData.UseRoughnessTexture);
	TryReadBoolNode(root, PUGIXML_TEXT("UseOpacityTexture"), &parsedData.UseOpacityTexture);
	TryReadFloatNode(root, PUGIXML_TEXT("Metallic"), &parsedData.Metallic);
	TryReadFloatNode(root, PUGIXML_TEXT("Roughness"), &parsedData.Roughness);
	TryReadFloatNode(root, PUGIXML_TEXT("Opacity"), &parsedData.Opacity);

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
	data.UseNormalTexture = !materialInfo.NormalTexture.AssetName.empty();
	data.UseMetallicTexture = !materialInfo.MetallicTexture.AssetName.empty();
	data.UseRoughnessTexture = !materialInfo.RoughnessTexture.AssetName.empty();
	data.UseOpacityTexture = false;
	return data;
}