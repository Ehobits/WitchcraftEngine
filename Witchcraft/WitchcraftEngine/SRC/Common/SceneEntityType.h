#pragma once

#include <cstdint>
#include <cwctype>
#include <string>

enum class SceneEntityType : std::uint32_t
{
	Sky = 0,
	Ground = 1,
	StaticScenery = 2,
	DynamicScenery = 3,
	Interactive = 4,
	Count = 5
};

inline const wchar_t* SceneEntityTypeToKey(SceneEntityType type)
{
	switch (type)
	{
	case SceneEntityType::Sky:
		return L"Sky";
	case SceneEntityType::Ground:
		return L"Ground";
	case SceneEntityType::StaticScenery:
		return L"StaticScenery";
	case SceneEntityType::DynamicScenery:
		return L"DynamicScenery";
	case SceneEntityType::Interactive:
		return L"Interactive";
	default:
		return L"StaticScenery";
	}
}

inline const wchar_t* SceneEntityTypeToDisplayName(SceneEntityType type)
{
	switch (type)
	{
	case SceneEntityType::Sky:
		return L"天空";
	case SceneEntityType::Ground:
		return L"地面";
	case SceneEntityType::StaticScenery:
		return L"固定景物";
	case SceneEntityType::DynamicScenery:
		return L"可变化景物";
	case SceneEntityType::Interactive:
		return L"互动实体";
	default:
		return L"固定景物";
	}
}

inline SceneEntityType SanitizeSceneEntityType(std::uint32_t rawValue)
{
	if (rawValue >= static_cast<std::uint32_t>(SceneEntityType::Count))
		return SceneEntityType::StaticScenery;

	return static_cast<SceneEntityType>(rawValue);
}

inline std::wstring SceneEntityTypeToLowerCopy(std::wstring value)
{
	for (wchar_t& ch : value)
		ch = static_cast<wchar_t>(std::towlower(ch));

	return value;
}

inline bool TryParseSceneEntityType(const std::wstring& value, SceneEntityType* outType)
{
	if (outType == nullptr)
		return false;

	const std::wstring normalized = SceneEntityTypeToLowerCopy(value);
	if (normalized == L"sky" || normalized == L"天空")
	{
		*outType = SceneEntityType::Sky;
		return true;
	}
	if (normalized == L"ground" || normalized == L"地面")
	{
		*outType = SceneEntityType::Ground;
		return true;
	}
	if (normalized == L"staticscenery" || normalized == L"固定景物")
	{
		*outType = SceneEntityType::StaticScenery;
		return true;
	}
	if (normalized == L"dynamicscenery" || normalized == L"可变化景物")
	{
		*outType = SceneEntityType::DynamicScenery;
		return true;
	}
	if (normalized == L"interactive" || normalized == L"互动实体")
	{
		*outType = SceneEntityType::Interactive;
		return true;
	}

	return false;
}
