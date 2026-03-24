#pragma once

#include <filesystem>
#include <Windows.h>
#include <xstring>
#include <DirectXMath.h>

#include "ModelAnalysis/ImportedAssetTypes.h"
#include "WitchcraftXmlFileBase.h"

struct WMaterialFileData
{
	std::wstring MaterialName;

	DirectX::XMFLOAT4 DiffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 Emissive = { 0.0f, 0.0f, 0.0f };

	bool UseNormalTexture = false;
	bool UseMetallicTexture = false;
	bool UseRoughnessTexture = false;
	bool UseOpacityTexture = false;

	float Metallic = 0.0f;
	float Roughness = 1.0f;
	float Opacity = 1.0f;

	std::wstring DiffuseTexture;
	std::wstring NormalTexture;
	std::wstring MetallicTexture;
	std::wstring RoughnessTexture;
	std::wstring OpacityTexture;
};

class WMaterialFile : public WitchcraftXmlFileBase
{
public:
	static constexpr const wchar_t* Extension = L".wmat";
	static constexpr const wchar_t* RootNodeName = L"WitchcraftMaterial";

	static std::wstring SerializeToText(const WMaterialFileData& data);
	static bool DeserializeFromText(const std::wstring& text, WMaterialFileData* outData);

	static bool SaveToFile(const std::filesystem::path& path, const WMaterialFileData& data);
	static bool LoadFromFile(const std::filesystem::path& path, WMaterialFileData* outData);

	static WMaterialFileData FromImportedMaterial(const ImportedMaterialInfo& materialInfo);

private:
	const wchar_t* GetRootNodeName() const override;
	void BuildBody(pugi::xml_node root) const override;
	bool ReadBody(const pugi::xml_node& root) override;

	WMaterialFileData m_data;
};