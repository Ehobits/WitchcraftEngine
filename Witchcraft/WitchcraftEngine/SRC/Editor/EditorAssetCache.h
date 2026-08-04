#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "Engine/EngineUtils.h"
#include "System/WitchcraftFile/WMaterialFile.h"

namespace EditorAssetCache
{
	struct MaterialFileEntry
	{
		std::wstring path;
		std::wstring displayName;
		std::wstring relativePath;
	};

	inline std::vector<MaterialFileEntry>& MaterialCacheStorage()
	{
		static std::vector<MaterialFileEntry> cache;
		return cache;
	}

	inline bool& MaterialCacheDirtyFlag()
	{
		static bool dirty = true;
		return dirty;
	}

	inline std::vector<std::wstring>& SkyTextureCacheStorage()
	{
		static std::vector<std::wstring> cache;
		return cache;
	}

	inline bool& SkyTextureCacheDirtyFlag()
	{
		static bool dirty = true;
		return dirty;
	}

	inline std::filesystem::path FindSkyTextureDirectory()
	{
		std::filesystem::path probe = std::filesystem::current_path();
		while (!probe.empty())
		{
			const std::filesystem::path candidate = probe / L"DATA" / L"HDRIs";
			if (std::filesystem::exists(candidate))
				return candidate;

			const std::filesystem::path parent = probe.parent_path();
			if (parent == probe)
				break;
			probe = parent;
		}

		return {};
	}

	inline void MarkMaterialFilesDirty()
	{
		MaterialCacheDirtyFlag() = true;
	}

	inline void MarkSkyTexturesDirty()
	{
		SkyTextureCacheDirtyFlag() = true;
	}

	inline void RefreshMaterialFilesIfNeeded()
	{
		if (!MaterialCacheDirtyFlag())
			return;

		std::vector<MaterialFileEntry>& cache = MaterialCacheStorage();
		cache.clear();

		const std::filesystem::path importedAssetsDir =
			std::filesystem::path(EngineUtils::GetProjectDirPath()) / L"ImportedAssets";
		if (importedAssetsDir.empty() || !std::filesystem::exists(importedAssetsDir))
		{
			MaterialCacheDirtyFlag() = false;
			return;
		}

		for (const auto& entry : std::filesystem::recursive_directory_iterator(importedAssetsDir))
		{
			if (!entry.is_regular_file())
				continue;

			std::wstring extension = entry.path().extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
			if (extension != L".wmat")
				continue;

			WMaterialFileData materialData;
			std::wstring displayName = entry.path().stem().wstring();
			if (WMaterialFile::LoadFromFile(entry.path(), &materialData) && !materialData.MaterialName.empty())
				displayName = materialData.MaterialName;

			std::error_code relativeError;
			std::filesystem::path relativePath = std::filesystem::relative(entry.path(), importedAssetsDir, relativeError);
			const std::wstring relativePathText = relativeError
				? entry.path().filename().wstring()
				: relativePath.generic_wstring();

			cache.push_back({ entry.path().wstring(), displayName, relativePathText });
		}

		std::sort(cache.begin(), cache.end(),
			[](const MaterialFileEntry& lhs, const MaterialFileEntry& rhs)
			{
				if (lhs.displayName == rhs.displayName)
					return lhs.path < rhs.path;
				return lhs.displayName < rhs.displayName;
			});

		MaterialCacheDirtyFlag() = false;
	}

	inline void RefreshSkyTexturesIfNeeded()
	{
		if (!SkyTextureCacheDirtyFlag())
			return;

		std::vector<std::wstring>& cache = SkyTextureCacheStorage();
		cache.clear();

		const std::filesystem::path skyTextureDir = FindSkyTextureDirectory();
		if (skyTextureDir.empty() || !std::filesystem::exists(skyTextureDir))
		{
			SkyTextureCacheDirtyFlag() = false;
			return;
		}

		const std::filesystem::path projectRoot = skyTextureDir.parent_path().parent_path();
		for (const auto& entry : std::filesystem::directory_iterator(skyTextureDir))
		{
			if (!entry.is_regular_file())
				continue;

			std::wstring extension = entry.path().extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
			if (extension != L".png")
				continue;

			cache.push_back(std::filesystem::relative(entry.path(), projectRoot).generic_wstring());
		}

		std::sort(cache.begin(), cache.end());
		SkyTextureCacheDirtyFlag() = false;
	}

	inline const std::vector<MaterialFileEntry>& GetMaterialFiles()
	{
		RefreshMaterialFilesIfNeeded();
		return MaterialCacheStorage();
	}

	inline const std::vector<std::wstring>& GetSkyTextures()
	{
		RefreshSkyTexturesIfNeeded();
		return SkyTextureCacheStorage();
	}
}
