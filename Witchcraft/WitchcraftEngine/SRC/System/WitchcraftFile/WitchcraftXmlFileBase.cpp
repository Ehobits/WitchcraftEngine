#include "WitchcraftXmlFileBase.h"

std::wstring WitchcraftXmlFileBase::Trim(const std::wstring& value)
{
	const size_t begin = value.find_first_not_of(L" \t\r\n");
	if (begin == std::wstring::npos)
		return L"";

	const size_t end = value.find_last_not_of(L" \t\r\n");
	return value.substr(begin, end - begin + 1);
}

pugi::string_t WitchcraftXmlFileBase::ToXmlString(const std::wstring& value)
{
#ifdef PUGIXML_WCHAR_MODE
	return value;
#else
	return SString::WstringToUTF8(value);
#endif
}

std::wstring WitchcraftXmlFileBase::FromXmlString(const pugi::char_t* value)
{
	if (value == nullptr)
		return L"";

#ifdef PUGIXML_WCHAR_MODE
	return value;
#else
	return SString::UTF8ToWstring(value);
#endif
}

void WitchcraftXmlFileBase::AppendTextNode(pugi::xml_node parent, const pugi::char_t* nodeName, const std::wstring& value)
{
	const pugi::string_t xmlValue = ToXmlString(value);
	parent.append_child(nodeName).text().set(xmlValue.c_str());
}

std::wstring WitchcraftXmlFileBase::ReadChildText(const pugi::xml_node& parent, const pugi::char_t* nodeName)
{
	return FromXmlString(parent.child(nodeName).text().as_string());
}

std::wstring WitchcraftXmlFileBase::SerializeDocumentToText() const
{
	pugi::xml_document document;
	BuildDocument(&document);

	std::ostringstream output;
	document.save(output, PUGIXML_TEXT("  "), pugi::format_default, pugi::encoding_utf8);
	return SString::UTF8ToWstring(output.str());
}

bool WitchcraftXmlFileBase::DeserializeDocumentFromText(const std::wstring& text)
{
	const std::wstring trimmedText = Trim(text);
	if (trimmedText.empty() || trimmedText[0] != L'<')
		return false;

	pugi::xml_document document;
	const pugi::string_t xmlText = ToXmlString(trimmedText);
	const pugi::xml_parse_result result = document.load_string(xmlText.c_str(), pugi::parse_default);
	if (!result)
		return false;

	return DeserializeDocument(document);
}

bool WitchcraftXmlFileBase::SaveDocumentToFile(const std::filesystem::path& path) const
{
	if (!path.parent_path().empty())
		std::filesystem::create_directories(path.parent_path());

	pugi::xml_document document;
	BuildDocument(&document);
	return document.save_file(path.c_str(), PUGIXML_TEXT("  "), pugi::format_default, pugi::encoding_utf8);
}

bool WitchcraftXmlFileBase::LoadDocumentFromFile(const std::filesystem::path& path)
{
	if (!std::filesystem::exists(path))
		return false;

	pugi::xml_document document;
	const pugi::xml_parse_result result = document.load_file(path.c_str(), pugi::parse_default, pugi::encoding_utf8);
	if (!result)
		return false;

	return DeserializeDocument(document);
}

void WitchcraftXmlFileBase::BuildDocument(pugi::xml_document* outDocument) const
{
	if (outDocument == nullptr)
		return;

	outDocument->reset();

	pugi::xml_node declaration = outDocument->append_child(pugi::node_declaration);
	declaration.append_attribute(PUGIXML_TEXT("version")) = PUGIXML_TEXT("1.0");
	declaration.append_attribute(PUGIXML_TEXT("encoding")) = PUGIXML_TEXT("utf-8");

	pugi::xml_node root = outDocument->append_child(GetRootNodeName());
	root.append_attribute(PUGIXML_TEXT("version")).set_value(GetFileVersion());
	BuildBody(root);
}

bool WitchcraftXmlFileBase::DeserializeDocument(const pugi::xml_document& document)
{
	const pugi::xml_node root = document.child(GetRootNodeName());
	if (!root)
		return false;

	const int version = root.attribute(PUGIXML_TEXT("version")).as_int(GetFileVersion());
	if (version != GetFileVersion())
		return false;

	return ReadBody(root);
}
