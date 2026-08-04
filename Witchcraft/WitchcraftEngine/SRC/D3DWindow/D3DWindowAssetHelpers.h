#pragma once

#include "D3DWindow.h"

#include "HELPERS/Helpers.h"
#include "System/WitchcraftFile/WMaterialFile.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

// D3DWindow 资产/材质导入辅助定义。
// 推荐仅供 D3DWindow 调用；当前设计目标是由 D3DWindow.cpp 在包含 D3DWindow.h 之后引入，
// 不建议被其他模块作为通用头直接依赖。

struct D3DWindowAssetHelpers
{
	static TextureType ResolveTextureTypeFromPath(const std::wstring& path)
	{
		std::wstring extension = std::filesystem::path(path).extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
		return extension == L".dds" ? TextureType::DDS : TextureType::PNG;
	}

	static bool IsLikelyAlphaCarrierTexturePath(const std::wstring& path)
	{
		if (path.empty())
			return false;

		std::wstring extension = std::filesystem::path(path).extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);

		return extension == L".png" ||
			extension == L".tga" ||
			extension == L".tif" ||
			extension == L".tiff" ||
			extension == L".dds";
	}

	static std::wstring MakeUniqueName(
		const std::unordered_map<std::wstring, Material>& materials,
		const std::wstring& baseName)
	{
		if (materials.find(baseName) == materials.end())
			return baseName;

		UINT suffix = 1;
		while (true)
		{
			std::wstring candidate = baseName + L"_" + std::to_wstring(suffix);
			if (materials.find(candidate) == materials.end())
				return candidate;
			++suffix;
		}
	}

	static std::wstring NormalizeAssetPath(const std::wstring& path)
	{
		std::filesystem::path normalized(path);
		normalized.make_preferred();
		return normalized.wstring();
	}

	static ImportedTextureSource BuildImportedTextureSourceFromMaterialFile(
		const std::filesystem::path& materialFilePath,
		const std::wstring& textureName)
	{
		ImportedTextureSource source;
		if (textureName.empty())
			return source;

		std::filesystem::path texturePath(textureName);
		if (texturePath.is_relative())
		{
			const std::filesystem::path projectRoot = std::filesystem::path(EngineUtils::GetProjectDirPath());
			const std::filesystem::path projectRelativeCandidate = projectRoot / texturePath;
			if (!projectRoot.empty() && std::filesystem::exists(projectRelativeCandidate))
			{
				texturePath = projectRelativeCandidate;
			}
			else if (!texturePath.has_parent_path())
			{
				texturePath = materialFilePath.parent_path().parent_path() / L"Textures" / texturePath;
			}
			else
			{
				texturePath = materialFilePath.parent_path() / texturePath;
			}
		}

		source.Path = texturePath.lexically_normal().generic_wstring();
		source.AssetName = std::filesystem::path(textureName).filename().wstring();
		return source;
	}

	static ImportedMaterialInfo ConvertMaterialFileDataToImportedInfo(
		const WMaterialFileData& materialData,
		const std::filesystem::path& materialFilePath)
	{
		ImportedMaterialInfo info;
		info.Name = materialData.MaterialName.empty() ? materialFilePath.stem().wstring() : materialData.MaterialName;
		info.DiffuseColor = materialData.DiffuseColor;
		info.Emissive = materialData.Emissive;
		info.FresnelR0 = materialData.FresnelR0;
		info.Metallic = materialData.Metallic;
		info.Roughness = materialData.Roughness;
		info.Opacity = materialData.Opacity;
		info.DiffuseTexture = BuildImportedTextureSourceFromMaterialFile(materialFilePath, materialData.DiffuseTexture);
		info.NormalTexture = BuildImportedTextureSourceFromMaterialFile(materialFilePath, materialData.NormalTexture);
		info.MetallicTexture = BuildImportedTextureSourceFromMaterialFile(materialFilePath, materialData.MetallicTexture);
		info.RoughnessTexture = BuildImportedTextureSourceFromMaterialFile(materialFilePath, materialData.RoughnessTexture);
		info.OpacityTexture = BuildImportedTextureSourceFromMaterialFile(materialFilePath, materialData.OpacityTexture);
		info.UseOpacityTexture = materialData.UseOpacityTexture &&
			(!info.OpacityTexture.Path.empty() || !info.DiffuseTexture.Path.empty());
		if (info.UseOpacityTexture && info.OpacityTexture.Path.empty())
		{
			info.OpacityTexture = info.DiffuseTexture;
		}
		return info;
	}
};
