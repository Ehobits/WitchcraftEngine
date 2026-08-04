#pragma once

#include <filesystem>
#include <sstream>
#include <xstring>

#include "pugixml.hpp"
#include "String/SStringUtils.h"

class WitchcraftXmlFileBase
{
public:
	virtual ~WitchcraftXmlFileBase() = default;

	static std::wstring Trim(const std::wstring& value);
	static pugi::string_t ToXmlString(const std::wstring& value);
	static std::wstring FromXmlString(const pugi::char_t* value);
	static void AppendTextNode(pugi::xml_node parent, const pugi::char_t* nodeName, const std::wstring& value);
	static std::wstring ReadChildText(const pugi::xml_node& parent, const pugi::char_t* nodeName);

protected:
	std::wstring SerializeDocumentToText() const;
	bool DeserializeDocumentFromText(const std::wstring& text);
	bool SaveDocumentToFile(const std::filesystem::path& path) const;
	bool LoadDocumentFromFile(const std::filesystem::path& path);

	virtual const wchar_t* GetRootNodeName() const = 0;
	virtual int GetFileVersion() const
	{
		return 1;
	}

	virtual void BuildBody(pugi::xml_node root) const = 0;
	virtual bool ReadBody(const pugi::xml_node& root) = 0;

private:
	void BuildDocument(pugi::xml_document* outDocument) const;
	bool DeserializeDocument(const pugi::xml_document& document);
};
