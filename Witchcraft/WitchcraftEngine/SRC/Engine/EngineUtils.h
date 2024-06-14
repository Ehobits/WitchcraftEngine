#pragma once

#include <xstring>
#include <filesystem>

#define MAJOR 0
#define MINOR 0
#define PATCH 2

#define FOLDER L"\\ASSETS"

namespace EngineUtils
{
	static std::wstring GetAppDirPath()
	{
		return std::filesystem::current_path().wstring();
	}

	static std::wstring GetProjectDirPath()
	{
		return GetAppDirPath() + FOLDER;
	}
}