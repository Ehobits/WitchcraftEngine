#pragma once

#include <filesystem>
#include <xstring>

#include "System/Animation/Assets/SkeletonAsset.h"
#include "WitchcraftXmlFileBase.h"

struct WSkeletonFileData
{
	std::wstring Name;
	Witchcraft::Animation::SkeletonTopology Topology;
};

class WSkeletonFile : public WitchcraftXmlFileBase
{
public:
	static constexpr const wchar_t* Extension = L".wskeleton";
	static constexpr const wchar_t* RootNodeName = L"WitchcraftSkeleton";

	static std::wstring SerializeToText(const WSkeletonFileData& data);
	static bool DeserializeFromText(const std::wstring& text, WSkeletonFileData* outData);
	static bool SaveToFile(const std::filesystem::path& path, const WSkeletonFileData& data);
	static bool LoadFromFile(const std::filesystem::path& path, WSkeletonFileData* outData);

private:
	const wchar_t* GetRootNodeName() const override;
	void BuildBody(pugi::xml_node root) const override;
	bool ReadBody(const pugi::xml_node& root) override;

	WSkeletonFileData m_data;
};
