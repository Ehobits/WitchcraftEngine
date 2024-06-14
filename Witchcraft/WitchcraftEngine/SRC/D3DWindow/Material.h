#pragma once

#include "D3D12_framework.h"
#include "D3DHelpers.h"

// 材质数据
struct MaterialConstants
{
	float Metallic = 0.0f; // 金属度
	float Roughness = 0.25f; // 粗糙度
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	DirectX::XMFLOAT3 Luminescence = { 0.0f, 0.0f, 0.0f };
	float Opacity = 0.0f;

	// 用于纹理映射。
	DirectX::XMFLOAT4X4 MatTransform = MathHelps::Identity;

	UINT SkyMapIndex = 0;
	UINT DiffuseMapIndex = 0;
	UINT NormalMapIndex = 0;
	UINT SpecularMapIndex = 0;
	UINT MetallicMapIndex = 0;
	UINT RoughnessMapIndex = 0;
	UINT FresnelR0MapIndex = 0;
	UINT DisplacementMapIndex = 0;
	UINT BumpMapIndex = 0;
	UINT AmbientOcclusionMapIndex = 0;
	UINT CavityMapIndex = 0;
	UINT SheenMapIndex = 0;
	UINT EmissiveMapIndex = 0;
	DirectX::XMFLOAT2 SkyTexC = { 0.0f, 0.0f };

	//UINT MaterialPad1;
	//UINT MaterialPad2;
};

class Texture;

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
	// 存储漫法线贴图的指针
	Texture* NormalTexture = nullptr;
	Texture* SpecularTexture = nullptr;
	Texture* MetallicTexture = nullptr;
	Texture* RoughnessTexture = nullptr;
	Texture* FresnelR0Texture = nullptr;
	Texture* DisplacementTexture = nullptr;
	Texture* BumpTexture = nullptr;
	Texture* AmbientOcclusionTexture = nullptr;
	Texture* CavityTexture = nullptr;
	Texture* SheenTexture = nullptr;
	Texture* EmissiveTexture = nullptr;

	//指示材料已更改的标志，我们需要更新常量缓冲区。
	//因为每个FrameResource都有一个材质常量缓冲区，所以我们必须将更新应用于每个FrameResource。 
	//因此，当我们修改材质时，我们应该设置NumFramesDirty = gNumFrameResources，
	//以便每个框架资源都可以更新。
	UINT NumFramesDirty = 3;

	//用于着色的材质常量缓冲区数据。
	float Metallic = .0f; // 金属度
	float Roughness = .25f; // 粗糙度
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f }; // 菲涅尔R0
	DirectX::XMFLOAT4X4 MatTransform = MathHelps::Identity; // 材质变换

private:
	// 用于查找的唯一材料名称。
	std::wstring Name;

};