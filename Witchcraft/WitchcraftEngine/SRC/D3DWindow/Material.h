#pragma once

#include "D3D12_framework.h"
#include "D3DHelpers.h"

// 材质数据
struct MaterialConstants
{
	// PBR + Blinn-Phong 属性
	DirectX::XMFLOAT4 DiffuseAlbedo = { 0.0f, 0.0f, 0.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.04f, 0.04f, 0.04f };
	float Opacity = 1.0f;
	DirectX::XMFLOAT3 Transmission = { 1.0f, 1.0f, 1.0f };
	float PAD001 = 1.0f;
	DirectX::XMFLOAT3 Emissive = { 0.0f, 0.0f, 0.0f };
	float PAD002 = 1.0f;
	float Metallic = 0.0f;
	float Roughness = 0.0f;
	float ClearCoatThickness = 0.0f;
	float ClearCoatRoughness = 0.0f;
	float Anisotropy = 0.0f;
	float AnisotropyRotation = 0.0f;
	DirectX::XMFLOAT2 PAD003;
	UINT UseDiffuseTexture = 1;
	UINT UseNormalTexture = 1;
	UINT UseMetallicTexture = 0;
	UINT UseRoughnessTexture = 0;
	UINT UseSpecularTexture = 0;
	UINT ReflectionSource = 0;
	DirectX::XMFLOAT2 PAD005 = { 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 ReflectionViewProjTex = MathHelps::Identity;
};

class Texture;

enum class MaterialReflectionSource : std::uint32_t
{
	SkyIBL = 0,
	RenderToTexture = 1
};

class Material
{
public:
	Material();
	Material(std::wstring name);
	~Material();

	void Create(std::wstring name);

	void SetName(std::wstring name);
	std::wstring GetName();

	// 索引到与此材料对应的常量缓冲区中。
	int MatCBIndex = -1;

	// 存储漫反射贴图的指针
	Texture* DiffuseTexture = nullptr;
	// 如果非 0，漫反射贴图优先采样对应 RenderToTexture 的 SRV。
	// 该字段只保存稳定 id，不持有 D3D resource，也不负责创建相机/RenderToTexture。
	std::uint32_t DiffuseRenderToTextureId = 0;
	// 镜面反射设置。SkyIBL 使用天空纹理；RenderToTexture 使用绑定相机输出的 2D SRV。
	// 当前只保存来源类型和稳定输出 id，不持有相机实体或 D3D resource。
	bool EnableReflection = false;
	MaterialReflectionSource ReflectionSource = MaterialReflectionSource::SkyIBL;
	std::uint32_t ReflectionRenderToTextureId = 0;
	// 存储法线贴图的指针
	Texture* NormalTexture = nullptr;
	Texture* SpecularTexture = nullptr;
	Texture* MetallicTexture = nullptr;
	Texture* RoughnessTexture = nullptr;
	Texture* DisplacementTexture = nullptr;
	Texture* BumpTexture = nullptr;
	Texture* AmbientOcclusionTexture = nullptr;
	Texture* CavityTexture = nullptr;
	Texture* SheenTexture = nullptr;
	Texture* EmissiveTexture = nullptr;
	Texture* OpacityTexture = nullptr;

	//指示材料已更改的标志，我们需要更新常量缓冲区。
	//因为每个FrameResource都有一个材质常量缓冲区，所以我们必须将更新应用于每个FrameResource。 
	//因此，当我们修改材质时，我们应该设置NumFramesDirty = gNumFrameResources，
	//以便每个框架资源都可以更新。
	UINT NumFramesDirty = 3;

	DirectX::XMFLOAT4X4 MatTransform = MathHelps::Identity; // 材质变换

	MaterialConstants Properties;

private:
	// 用于查找的唯一材料名称。
	std::wstring Name;
};
