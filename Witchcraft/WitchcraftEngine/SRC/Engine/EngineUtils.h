#pragma once

#include <xstring>
#include <filesystem>

#define MAJOR 0
#define MINOR 0
#define PATCH 2

namespace EngineUtils
{
	inline std::filesystem::path& CurrentProjectDirPathStorage()
	{
		static std::filesystem::path currentProjectDirPath;
		return currentProjectDirPath;
	}

	inline std::wstring GetAppDirPath()
	{
		return std::filesystem::current_path().lexically_normal().wstring();
	}

	inline std::wstring GetDefaultProjectDirPath()
	{
		return (std::filesystem::path(GetAppDirPath()) / L"ASSETS").lexically_normal().wstring();
	}

	inline std::wstring GetEngineResourceDirPath()
	{
		return GetAppDirPath();
	}

	inline void SetProjectDirPath(const std::filesystem::path& path)
	{
		if (path.empty())
		{
			CurrentProjectDirPathStorage().clear();
			return;
		}

		CurrentProjectDirPathStorage() = path.lexically_normal();
	}

	inline void ClearProjectDirPath()
	{
		CurrentProjectDirPathStorage().clear();
	}

	inline std::wstring GetProjectDirPath()
	{
		const std::filesystem::path& currentProjectDirPath = CurrentProjectDirPathStorage();
		if (!currentProjectDirPath.empty())
			return currentProjectDirPath.wstring();

		return GetDefaultProjectDirPath();
	}

	inline std::filesystem::path ResolveProjectPath(const std::filesystem::path& path)
	{
		if (path.empty())
			return {};
		if (path.is_absolute())
			return path.lexically_normal();

		const std::filesystem::path projectCandidate = (std::filesystem::path(GetProjectDirPath()) / path).lexically_normal();
		std::error_code projectExistsError;
		if (std::filesystem::exists(projectCandidate, projectExistsError))
			return projectCandidate;

		const std::filesystem::path engineCandidate = (std::filesystem::path(GetEngineResourceDirPath()) / path).lexically_normal();
		std::error_code engineExistsError;
		if (std::filesystem::exists(engineCandidate, engineExistsError))
			return engineCandidate;

		return projectCandidate;
	}
}
