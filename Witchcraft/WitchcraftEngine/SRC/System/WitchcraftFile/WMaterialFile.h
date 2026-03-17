#pragma once

#include <filesystem>
#include <Windows.h>
#include <xstring>
#include <DirectXMath.h>

#include "ModelAnalysis/ImportedAssetTypes.h"

// .wmat XML 材质文件的内存表示。
struct WMaterialFileData
{
	std::wstring MaterialName;

	DirectX::XMFLOAT4 DiffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 Emissive = { 0.0f, 0.0f, 0.0f };

	bool UseMetallicTexture = false;
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

class WMaterialFile
{
public:
	static constexpr const wchar_t* Extension = L".wmat";
	static constexpr const wchar_t* RootNodeName = L"WitchcraftMaterial";

	// 序列化为 UTF-8 XML 文本。
	static std::wstring SerializeToText(const WMaterialFileData& data);
	// 从 UTF-8 XML 文本反序列化。
	static bool DeserializeFromText(const std::wstring& text, WMaterialFileData* outData);

	// 使用 UTF-8 读取和保存文件。
	static bool SaveToFile(const std::filesystem::path& path, const WMaterialFileData& data);
	static bool LoadFromFile(const std::filesystem::path& path, WMaterialFileData* outData);

	// 供后续模型导入流程直接生成 .wmat 使用。
	static WMaterialFileData FromImportedMaterial(const ImportedMaterialInfo& materialInfo);
};

