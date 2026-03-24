#pragma once

#include <string>

#include <Windows.h>
#include <DirectXMath.h>

struct ImportedTextureSource
{
	// 导入阶段解析出的最终纹理路径；用于运行时直接加载。
	std::wstring Path;
	// 保存到导入目录中的纹理文件名；用于 .wmat 等资源文件持久化。
	std::wstring AssetName;
};

struct ImportedMaterialInfo
{
	// 运行时材质需要的最小 PBR 信息集合。
	std::wstring Name;

	DirectX::XMFLOAT4 DiffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 Emissive = { 0.0f, 0.0f, 0.0f };
	float Metallic = 0.0f;
	float Roughness = 1.0f;
	float Opacity = 1.0f;

	ImportedTextureSource DiffuseTexture;
	ImportedTextureSource NormalTexture;
	ImportedTextureSource SpecularTexture;
	ImportedTextureSource MetallicTexture;
	ImportedTextureSource RoughnessTexture;
	ImportedTextureSource EmissiveTexture;
	ImportedTextureSource AmbientOcclusionTexture;
};
