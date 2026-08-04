#pragma once

#include <filesystem>
#include <xstring>

#include "System/Animation/Assets/AnimationClipAsset.h"
#include "WitchcraftXmlFileBase.h"

struct WAnimationFileData
{
	Witchcraft::Animation::AnimationClipDesc Clip;
};

class WAnimationFile : public WitchcraftXmlFileBase
{
public:
	static constexpr const wchar_t* Extension = L".wanim";
	static constexpr const wchar_t* RootNodeName = L"WitchcraftAnimation";

	static std::wstring SerializeToText(const WAnimationFileData& data);
	static bool DeserializeFromText(const std::wstring& text, WAnimationFileData* outData);
	static bool SaveToFile(const std::filesystem::path& path, const WAnimationFileData& data);
	static bool LoadFromFile(const std::filesystem::path& path, WAnimationFileData* outData);

private:
	const wchar_t* GetRootNodeName() const override;
	void BuildBody(pugi::xml_node root) const override;
	bool ReadBody(const pugi::xml_node& root) override;

	void AppendMatrixKeyAttributes(pugi::xml_node node, float time, const DirectX::XMFLOAT4X4& value) const;
	void AppendFloat3KeyAttributes(pugi::xml_node node, float time, const DirectX::XMFLOAT3& value) const;
	void AppendFloat4KeyAttributes(pugi::xml_node node, float time, const DirectX::XMFLOAT4& value) const;

	WAnimationFileData m_data;
};
