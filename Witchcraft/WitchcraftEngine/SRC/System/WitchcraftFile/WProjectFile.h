#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "WitchcraftXmlFileBase.h"

struct WProjectMetaData
{
	std::wstring Name;
	std::wstring CreatedAt;
	std::wstring UpdatedAt;
};

struct WProjectSceneData
{
	std::wstring Id;
	std::wstring Name;
	std::wstring Path;
	bool Entry = false;
};

struct WProjectSettingsData
{
	std::wstring DefaultScenePath;
	std::wstring CurrentScenePath;
};

struct WProjectEditorStateData
{
	std::wstring CurrentSceneId;
};

struct WProjectFileData
{
	WProjectMetaData Meta;
	WProjectSettingsData Settings;
	std::vector<WProjectSceneData> Scenes;
	WProjectEditorStateData EditorState;
};

class WProjectFile : public WitchcraftXmlFileBase
{
public:
	static constexpr const wchar_t* Extension = L".wproject";
	static constexpr const wchar_t* RootNodeName = L"Project";

	static std::wstring SerializeToText(const WProjectFileData& data);
	static bool DeserializeFromText(const std::wstring& text, WProjectFileData* outData);
	static bool SaveToFile(const std::filesystem::path& path, const WProjectFileData& data);
	static bool LoadFromFile(const std::filesystem::path& path, WProjectFileData* outData);

private:
	const wchar_t* GetRootNodeName() const override;
	void BuildBody(pugi::xml_node root) const override;
	bool ReadBody(const pugi::xml_node& root) override;

	WProjectFileData m_data;
};
