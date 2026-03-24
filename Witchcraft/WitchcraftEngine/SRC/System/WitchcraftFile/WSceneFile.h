#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Common/TransformSharedTypes.h"
#include "WitchcraftXmlFileBase.h"

struct WSceneMetaData
{
	std::wstring Name;
	std::wstring CreatedAt;
	std::wstring UpdatedAt;
};

struct WSceneModelAssetData
{
	std::wstring Id;
	std::wstring Path;
};

struct WSceneMeshData
{
	std::wstring RenderSourceType;
	unsigned int RenderLayer = 1;
	std::wstring PrimitiveKind;
	std::wstring GeometryRef;
	std::wstring SkyTexturePath;
	int RawSubMeshIndex = -1;
	std::wstring ModelRef;
	std::wstring MeshName;
	std::wstring NodeId;
	std::wstring MaterialName;
	std::wstring MaterialFile;
};

struct WSceneLightData
{
	std::wstring Kind;
	float Type = 1.0f;
	DirectX::XMFLOAT3 Color = { 0.42f, 0.42f, 0.42f };
	float Power = 1.2f;
	bool CastShadow = true;
};

struct WSceneRenderSettingsData
{
	float ShadowOpacity = 0.65f;
	float ShadowSoftness = 1.5f;
};

struct WSceneEntityData
{
	std::wstring Id;
	std::wstring Name;
	bool Active = true;
	std::wstring ParentId;
	Transform LocalTransform{};
	bool HasMesh = false;
	WSceneMeshData Mesh;
	bool HasLight = false;
	WSceneLightData Light;
};

struct WSceneFileData
{
	WSceneMetaData Meta;
	WSceneRenderSettingsData RenderSettings;
	std::vector<WSceneModelAssetData> Models;
	std::vector<WSceneEntityData> Entities;
};

class WSceneFile : public WitchcraftXmlFileBase
{
public:
	static constexpr const wchar_t* Extension = L".wscene";
	static constexpr const wchar_t* RootNodeName = L"Scene";

	static std::wstring SerializeToText(const WSceneFileData& data);
	static bool DeserializeFromText(const std::wstring& text, WSceneFileData* outData);
	static bool SaveToFile(const std::filesystem::path& path, const WSceneFileData& data);
	static bool LoadFromFile(const std::filesystem::path& path, WSceneFileData* outData);

private:
	const wchar_t* GetRootNodeName() const override;
	void BuildBody(pugi::xml_node root) const override;
	bool ReadBody(const pugi::xml_node& root) override;

	WSceneFileData m_data;
};
