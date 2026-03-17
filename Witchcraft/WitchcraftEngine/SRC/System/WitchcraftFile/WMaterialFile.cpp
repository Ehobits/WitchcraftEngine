#include "WMaterialFile.h"

#include <cwchar>
#include <sstream>
#include <vector>

#include "pugixml.hpp"
#include "String/SStringUtils.h"

namespace
{
	// pugixml 在宽字符模式下这里等价于 std::wstring；保留别名便于统一处理。
	using XmlString = pugi::string_t;

	std::wstring Trim(const std::wstring& value)
	{
		const size_t begin = value.find_first_not_of(L" \t\r\n");
		if (begin == std::wstring::npos)
			return L"";

		const size_t end = value.find_last_not_of(L" \t\r\n");
		return value.substr(begin, end - begin + 1);
	}

	std::vector<std::wstring> Split(const std::wstring& value, wchar_t delimiter)
	{
		std::vector<std::wstring> result;
		std::wstringstream stream(value);
		std::wstring item;
		while (std::getline(stream, item, delimiter))
			result.push_back(Trim(item));
		return result;
	}

	XmlString ToXmlString(const std::wstring& value)
	{
#ifdef PUGIXML_WCHAR_MODE
		// 宽字符模式下可直接交给 pugixml。
		return value;
#else
		// 非宽字符模式时退回到 UTF-8。
		return SString::WstringToUTF8(value);
#endif
	}

	std::wstring FromXmlString(const pugi::char_t* value)
	{
		if (value == nullptr)
			return L"";

#ifdef PUGIXML_WCHAR_MODE
		// 宽字符模式下直接回传。
		return value;
#else
		return SString::UTF8ToWstring(value);
#endif
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

	void AppendTextNode(pugi::xml_node parent, const pugi::char_t* nodeName, const std::wstring& value)
	{
		const XmlString xmlValue = ToXmlString(value);
		parent.append_child(nodeName).text().set(xmlValue.c_str());
	}

	void AppendBoolNode(pugi::xml_node parent, const pugi::char_t* nodeName, bool value)
	{
		AppendTextNode(parent, nodeName, BoolToText(value));
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

	std::wstring ReadChildText(const pugi::xml_node& parent, const pugi::char_t* nodeName)
	{
		return FromXmlString(parent.child(nodeName).text().as_string());
	}

	bool TryReadFloatNode(const pugi::xml_node& parent, const pugi::char_t* nodeName, float* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		const std::wstring text = Trim(FromXmlString(node.text().as_string()));
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

		*outValue = TextToBool(Trim(FromXmlString(node.text().as_string())));
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

		return ParseFloat4Text(Trim(FromXmlString(node.text().as_string())), outValue);
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

		return ParseFloat3Text(Trim(FromXmlString(node.text().as_string())), outValue);
	}

	void BuildXmlDocument(const WMaterialFileData& data, pugi::xml_document* outDocument)
	{
		if (outDocument == nullptr)
			return;

		// 每次保存都重新构建一份完整 XML，避免复用旧节点内容。
		outDocument->reset();

		pugi::xml_node declaration = outDocument->append_child(pugi::node_declaration);
		declaration.append_attribute(PUGIXML_TEXT("version")) = PUGIXML_TEXT("1.0");
		declaration.append_attribute(PUGIXML_TEXT("encoding")) = PUGIXML_TEXT("utf-8");

		pugi::xml_node root = outDocument->append_child(WMaterialFile::RootNodeName);
		root.append_attribute(PUGIXML_TEXT("version")) = PUGIXML_TEXT("1");

		AppendTextNode(root, PUGIXML_TEXT("MaterialName"), data.MaterialName);
		AppendFloat4Node(root, PUGIXML_TEXT("DiffuseColor"), data.DiffuseColor);
		AppendFloat3Node(root, PUGIXML_TEXT("Emissive"), data.Emissive);
		AppendBoolNode(root, PUGIXML_TEXT("UseMetallicTexture"), data.UseMetallicTexture);
		AppendBoolNode(root, PUGIXML_TEXT("UseOpacityTexture"), data.UseOpacityTexture);
		AppendFloatNode(root, PUGIXML_TEXT("Metallic"), data.Metallic);
		AppendFloatNode(root, PUGIXML_TEXT("Roughness"), data.Roughness);
		AppendFloatNode(root, PUGIXML_TEXT("Opacity"), data.Opacity);

		// 贴图统一放在单独节点下，后续扩展更多贴图槽会更直观。
		pugi::xml_node textures = root.append_child(PUGIXML_TEXT("Textures"));
		AppendTextNode(textures, PUGIXML_TEXT("Diffuse"), data.DiffuseTexture);
		AppendTextNode(textures, PUGIXML_TEXT("Normal"), data.NormalTexture);
		AppendTextNode(textures, PUGIXML_TEXT("Metallic"), data.MetallicTexture);
		AppendTextNode(textures, PUGIXML_TEXT("Roughness"), data.RoughnessTexture);
		AppendTextNode(textures, PUGIXML_TEXT("Opacity"), data.OpacityTexture);
	}

	bool DeserializeXmlDocument(const pugi::xml_document& document, WMaterialFileData* outData)
	{
		if (outData == nullptr)
			return false;

		const pugi::xml_node root = document.child(WMaterialFile::RootNodeName);
		if (!root)
			return false;

		WMaterialFileData parsedData;
		parsedData.MaterialName = ReadChildText(root, PUGIXML_TEXT("MaterialName"));

		// 读取相关字段
		TryReadFloat4Node(root, PUGIXML_TEXT("DiffuseColor"), &parsedData.DiffuseColor);
		TryReadFloat3Node(root, PUGIXML_TEXT("Emissive"), &parsedData.Emissive);
		TryReadBoolNode(root, PUGIXML_TEXT("UseMetallicTexture"), &parsedData.UseMetallicTexture);
		TryReadBoolNode(root, PUGIXML_TEXT("UseOpacityTexture"), &parsedData.UseOpacityTexture);
		TryReadFloatNode(root, PUGIXML_TEXT("Metallic"), &parsedData.Metallic);
		TryReadFloatNode(root, PUGIXML_TEXT("Roughness"), &parsedData.Roughness);
		TryReadFloatNode(root, PUGIXML_TEXT("Opacity"), &parsedData.Opacity);

		const pugi::xml_node textures = root.child(PUGIXML_TEXT("Textures"));
		if (textures)
		{
			parsedData.DiffuseTexture = ReadChildText(textures, PUGIXML_TEXT("Diffuse"));
			parsedData.NormalTexture = ReadChildText(textures, PUGIXML_TEXT("Normal"));
			parsedData.MetallicTexture = ReadChildText(textures, PUGIXML_TEXT("Metallic"));
			parsedData.RoughnessTexture = ReadChildText(textures, PUGIXML_TEXT("Roughness"));
			parsedData.OpacityTexture = ReadChildText(textures, PUGIXML_TEXT("Opacity"));
		}

		*outData = parsedData;
		return true;
	}

}

std::wstring WMaterialFile::SerializeToText(const WMaterialFileData& data)
{
	pugi::xml_document document;
	BuildXmlDocument(data, &document);

	// 仍按 UTF-8 输出文本，便于磁盘文件统一编码。
	std::ostringstream output;
	document.save(output, PUGIXML_TEXT("  "), pugi::format_default, pugi::encoding_utf8);
	return SString::UTF8ToWstring(output.str());
}

bool WMaterialFile::DeserializeFromText(const std::wstring& text, WMaterialFileData* outData)
{
	if (outData == nullptr)
		return false;

	const std::wstring trimmedText = Trim(text);
	if (trimmedText.empty())
		return false;

	if (trimmedText[0] != L'<')
		return false;

	pugi::xml_document document;
	const XmlString xmlText = ToXmlString(trimmedText);
	const pugi::xml_parse_result result = document.load_string(xmlText.c_str(), pugi::parse_default);
	if (!result)
		return false;

	return DeserializeXmlDocument(document, outData);
}

bool WMaterialFile::SaveToFile(const std::filesystem::path& path, const WMaterialFileData& data)
{
	if (!path.parent_path().empty())
		std::filesystem::create_directories(path.parent_path());

	pugi::xml_document document;
	BuildXmlDocument(data, &document);
	// 路径和 XML API 都走 wchar_t / pugi::char_t，文件编码固定为 UTF-8。
	return document.save_file(path.c_str(), PUGIXML_TEXT("  "), pugi::format_default, pugi::encoding_utf8);
}

bool WMaterialFile::LoadFromFile(const std::filesystem::path& path, WMaterialFileData* outData)
{
	if (outData == nullptr || !std::filesystem::exists(path))
		return false;

	pugi::xml_document document;
	const pugi::xml_parse_result xmlResult = document.load_file(path.c_str(), pugi::parse_default, pugi::encoding_utf8);
	if (xmlResult)
		return DeserializeXmlDocument(document, outData);
	return false;
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
	data.UseMetallicTexture = !materialInfo.MetallicTexture.AssetName.empty();
	data.UseOpacityTexture = false;
	return data;
}
