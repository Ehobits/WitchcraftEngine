#pragma once

#include <filesystem>
#include <string>

#include "System/Animation/Assets/SkinnedMeshAsset.h"
#include "WitchcraftXmlFileBase.h"

struct WSkinnedMeshFileData
{
	std::wstring Name;
	std::wstring SkeletonAssetPath;
	std::vector<Witchcraft::Animation::SkinnedVertex> Vertices;
	std::vector<std::uint32_t> Indices;
	std::vector<Witchcraft::Animation::SkinnedSubmeshDesc> Submeshes;
};

class WSkinnedMeshFile : public WitchcraftXmlFileBase
{
public:
	static constexpr const wchar_t* Extension = L".wskin";
	static constexpr const wchar_t* RootNodeName = L"WitchcraftSkinnedMesh";

	static std::wstring SerializeToText(const WSkinnedMeshFileData& data);
	static bool DeserializeFromText(const std::wstring& text, WSkinnedMeshFileData* outData);
	static bool SaveToFile(const std::filesystem::path& path, const WSkinnedMeshFileData& data);
	static bool LoadFromFile(const std::filesystem::path& path, WSkinnedMeshFileData* outData);

private:
	const wchar_t* GetRootNodeName() const override;
	void BuildBody(pugi::xml_node root) const override;
	bool ReadBody(const pugi::xml_node& root) override;

	WSkinnedMeshFileData m_data;
};
