#pragma once

#include <cstdint>
#include <filesystem>
#include <xstring>
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
	bool EnableVolumetric = true;
	float VolumetricIntensity = 1.0f;
	float VolumetricAttenuationDistance = 20.0f;
};

struct WSceneCameraData
{
	bool Primary = false;
	bool RenderEnabled = true;
	bool RenderToTextureEnabled = false;
	float NearZ = 1.0f;
	float FarZ = 1000.0f;
	float FovY = 0.25f;
	float ViewportScale = 1.0f;
	std::uint32_t OutputTargetId = 0;
};

struct WSceneRenderSettingsData
{
	float ShadowOpacity = 0.65f;
	float ShadowSoftness = 1.5f;
	bool AOEnabled = true;
	float AOStrength = 0.32f;
	float AORadius = 0.05f;
	float AOFadeStart = 0.2f;
	float AOFadeEnd = 2.0f;
	float AOSurfaceEpsilon = 0.02f;
	float AOBlurSigma = 2.5f;
	bool FXAAEnabled = true;
	float FXAAContrastThreshold = 0.0312f;
	float FXAARelativeThreshold = 0.125f;
	float FXAASpanMax = 8.0f;
	DirectX::XMFLOAT3 ColorAdjustWhiteBalance = { 1.0f, 1.0f, 1.0f };
	float ColorAdjustContrast = 1.0f;
	float ColorAdjustSaturation = 1.0f;
	float EnvironmentDiffuseIntensity = 1.0f;
	float EnvironmentSpecularIntensity = 1.0f;
	bool EnvironmentBrdfLutEnabled = false;
};

struct WSceneEnvironmentData
{
	bool HasAmbientLight = false;
	bool AmbientLightActive = true;
	WSceneLightData AmbientLight;
};

struct WSceneEntityTypeColorData
{
	std::wstring Type;
	DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

enum class WSceneAnimatorLayerBlendMode
{
	Override,
	Additive
};

struct WSceneAnimatorLayerData
{
	std::wstring Name;
	std::wstring ClipAssetPath;
	std::wstring MaskRootBoneName;
	float Weight = 1.0f;
	bool Loop = true;
	bool Enabled = true;
	WSceneAnimatorLayerBlendMode BlendMode = WSceneAnimatorLayerBlendMode::Override;
};

struct WSceneAnimatorData
{
	std::vector<WSceneAnimatorLayerData> Layers;
};

struct WSceneEntityData
{
	std::wstring Id;
	std::wstring Name;
	std::wstring EntityType;
	bool Active = true;
	std::wstring ParentId;
	Transform LocalTransform{};
	bool HasMesh = false;
	WSceneMeshData Mesh;
	bool HasLight = false;
	WSceneLightData Light;
	bool HasCamera = false;
	WSceneCameraData Camera;
	bool HasSkeleton = false;
	bool HasAnimator = false;
	WSceneAnimatorData Animator;
	bool HasSkinningRuntime = false;
};
struct WSceneFileData
{
	WSceneMetaData Meta;
	WSceneRenderSettingsData RenderSettings;
	WSceneEnvironmentData Environment;
	std::vector<WSceneEntityTypeColorData> EntityTypeColors;
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
