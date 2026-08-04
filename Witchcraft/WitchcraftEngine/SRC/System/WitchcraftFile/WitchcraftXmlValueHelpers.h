#pragma once

#include "WitchcraftXmlFileBase.h"

#include <DirectXMath.h>
#include <cwchar>
#include <sstream>
#include <vector>

namespace WitchcraftXmlValueHelpers
{
	inline void AppendMatrixAttributes(pugi::xml_node node, const DirectX::XMFLOAT4X4& matrix);
	inline DirectX::XMFLOAT4X4 ReadMatrixAttributes(const pugi::xml_node& node, const DirectX::XMFLOAT4X4& fallback);

	// 文件里布尔值统一使用 true / false 文本。
	inline const pugi::char_t* BoolToXmlText(bool value)
	{
		return value ? PUGIXML_TEXT("true") : PUGIXML_TEXT("false");
	}

	inline std::wstring BoolToText(bool value)
	{
		return value ? L"true" : L"false";
	}

	inline bool TextToBool(const std::wstring& value)
	{
		return _wcsicmp(value.c_str(), L"true") == 0 || value == L"1";
	}

	// 简单的分隔文本解析工具，主要用于兼容 X,Y,Z / X,Y,Z,W 形式的向量文本。
	inline std::vector<std::wstring> SplitText(const std::wstring& value, wchar_t delimiter)
	{
		std::vector<std::wstring> result;
		std::wstringstream stream(value);
		std::wstring item;
		while (std::getline(stream, item, delimiter))
			result.push_back(WitchcraftXmlFileBase::Trim(item));
		return result;
	}

	inline void AppendBoolTextNode(pugi::xml_node parent, const pugi::char_t* nodeName, bool value)
	{
		WitchcraftXmlFileBase::AppendTextNode(parent, nodeName, BoolToText(value));
	}

	inline void AppendFloatTextNode(pugi::xml_node parent, const pugi::char_t* nodeName, float value)
	{
		parent.append_child(nodeName).text().set(value);
	}

	inline bool TryReadFloatTextNode(const pugi::xml_node& parent, const pugi::char_t* nodeName, float* outValue)
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

	inline bool TryReadBoolTextNode(const pugi::xml_node& parent, const pugi::char_t* nodeName, bool* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		*outValue = TextToBool(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(node.text().as_string())));
		return true;
	}

	inline void AppendFloat3Attributes(pugi::xml_node node, const DirectX::XMFLOAT3& value)
	{
		node.append_attribute(PUGIXML_TEXT("x")).set_value(value.x);
		node.append_attribute(PUGIXML_TEXT("y")).set_value(value.y);
		node.append_attribute(PUGIXML_TEXT("z")).set_value(value.z);
	}

	inline void AppendFloat4Attributes(pugi::xml_node node, const DirectX::XMFLOAT4& value)
	{
		node.append_attribute(PUGIXML_TEXT("x")).set_value(value.x);
		node.append_attribute(PUGIXML_TEXT("y")).set_value(value.y);
		node.append_attribute(PUGIXML_TEXT("z")).set_value(value.z);
		node.append_attribute(PUGIXML_TEXT("w")).set_value(value.w);
	}

	inline DirectX::XMFLOAT3 ReadFloat3Attributes(const pugi::xml_node& node, const DirectX::XMFLOAT3& fallback)
	{
		DirectX::XMFLOAT3 value = fallback;
		value.x = node.attribute(PUGIXML_TEXT("x")).as_float(value.x);
		value.y = node.attribute(PUGIXML_TEXT("y")).as_float(value.y);
		value.z = node.attribute(PUGIXML_TEXT("z")).as_float(value.z);
		return value;
	}

	inline DirectX::XMFLOAT4 ReadFloat4Attributes(const pugi::xml_node& node, const DirectX::XMFLOAT4& fallback)
	{
		DirectX::XMFLOAT4 value = fallback;
		value.x = node.attribute(PUGIXML_TEXT("x")).as_float(value.x);
		value.y = node.attribute(PUGIXML_TEXT("y")).as_float(value.y);
		value.z = node.attribute(PUGIXML_TEXT("z")).as_float(value.z);
		value.w = node.attribute(PUGIXML_TEXT("w")).as_float(value.w);
		return value;
	}

	inline void AppendFloat3ChildNode(pugi::xml_node parent, const pugi::char_t* nodeName, const DirectX::XMFLOAT3& value)
	{
		AppendFloat3Attributes(parent.append_child(nodeName), value);
	}

	inline bool TryReadFloat3ChildNode(const pugi::xml_node& parent, const pugi::char_t* nodeName, DirectX::XMFLOAT3* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		*outValue = ReadFloat3Attributes(node, *outValue);
		return true;
	}

	inline void AppendFloat3TextNode(pugi::xml_node parent, const pugi::char_t* nodeName, const DirectX::XMFLOAT3& value)
	{
		pugi::xml_node node = parent.append_child(nodeName);
		AppendFloat3Attributes(node, value);
	}

	inline void AppendFloat4TextNode(pugi::xml_node parent, const pugi::char_t* nodeName, const DirectX::XMFLOAT4& value)
	{
		pugi::xml_node node = parent.append_child(nodeName);
		AppendFloat4Attributes(node, value);
	}

	inline bool ParseFloat3Text(const std::wstring& text, DirectX::XMFLOAT3* outValue)
	{
		if (outValue == nullptr)
			return false;

		const std::vector<std::wstring> parts = SplitText(text, L',');
		if (parts.size() != 3)
			return false;

		outValue->x = std::stof(parts[0]);
		outValue->y = std::stof(parts[1]);
		outValue->z = std::stof(parts[2]);
		return true;
	}

	inline bool ParseFloat4Text(const std::wstring& text, DirectX::XMFLOAT4* outValue)
	{
		if (outValue == nullptr)
			return false;

		const std::vector<std::wstring> parts = SplitText(text, L',');
		if (parts.size() != 4)
			return false;

		outValue->x = std::stof(parts[0]);
		outValue->y = std::stof(parts[1]);
		outValue->z = std::stof(parts[2]);
		outValue->w = std::stof(parts[3]);
		return true;
	}

	inline bool TryReadFloat3TextOrAttributesNode(const pugi::xml_node& parent, const pugi::char_t* nodeName, DirectX::XMFLOAT3* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		if (node.attribute(PUGIXML_TEXT("x")) && node.attribute(PUGIXML_TEXT("y")) && node.attribute(PUGIXML_TEXT("z")))
		{
			*outValue = ReadFloat3Attributes(node, *outValue);
			return true;
		}

		return ParseFloat3Text(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(node.text().as_string())), outValue);
	}

	inline bool TryReadFloat4TextOrAttributesNode(const pugi::xml_node& parent, const pugi::char_t* nodeName, DirectX::XMFLOAT4* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		if (node.attribute(PUGIXML_TEXT("x")) && node.attribute(PUGIXML_TEXT("y")) && node.attribute(PUGIXML_TEXT("z")) && node.attribute(PUGIXML_TEXT("w")))
		{
			*outValue = ReadFloat4Attributes(node, *outValue);
			return true;
		}

		return ParseFloat4Text(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(node.text().as_string())), outValue);
	}

	inline void AppendMatrixNode(pugi::xml_node parent, const pugi::char_t* nodeName, const DirectX::XMFLOAT4X4& matrix)
	{
		pugi::xml_node node = parent.append_child(nodeName);
		AppendMatrixAttributes(node, matrix);
	}

	inline void AppendMatrixAttributes(pugi::xml_node node, const DirectX::XMFLOAT4X4& matrix)
	{
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				const std::wstring attributeName = L"m" + std::to_wstring(row) + std::to_wstring(column);
				node.append_attribute(WitchcraftXmlFileBase::ToXmlString(attributeName).c_str()).set_value(matrix.m[row][column]);
			}
		}
	}

	inline DirectX::XMFLOAT4X4 ReadMatrixAttributes(const pugi::xml_node& node, const DirectX::XMFLOAT4X4& fallback)
	{
		DirectX::XMFLOAT4X4 matrix = fallback;
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				const std::wstring attributeName = L"m" + std::to_wstring(row) + std::to_wstring(column);
				matrix.m[row][column] = node.attribute(WitchcraftXmlFileBase::ToXmlString(attributeName).c_str()).as_float(matrix.m[row][column]);
			}
		}
		return matrix;
	}

	inline DirectX::XMFLOAT4X4 ReadMatrixNode(const pugi::xml_node& parent, const pugi::char_t* nodeName, const DirectX::XMFLOAT4X4& fallback)
	{
		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return fallback;

		return ReadMatrixAttributes(node, fallback);
	}
}
