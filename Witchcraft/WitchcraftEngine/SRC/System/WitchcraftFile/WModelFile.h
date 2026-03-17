#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/ServicesContainer/ServicesContainer.h"

struct WModelMaterialRef
{
	std::wstring Id;
	std::wstring File;
};

struct WModelMaterialSlot
{
	UINT Index = 0;
	std::wstring MaterialRef;
};

struct WModelMeshData
{
	std::wstring Id;
	std::wstring Name;
	std::vector<Vertex> Vertices;
	std::vector<std::uint32_t> Indices;
};

enum class WModelNodeType
{
	Empty,
	Mesh,
};

struct WModelNodeData
{
	std::wstring Id;
	std::wstring Name;
	WModelNodeType Type = WModelNodeType::Empty;
	Transform LocalTransform;

	std::wstring MeshRef;
	std::vector<WModelMaterialSlot> MaterialSlots;
	std::vector<WModelNodeData> Children;
};

struct WModelFileData
{
	std::wstring Name;
	std::wstring SourceFile;
	std::vector<WModelMaterialRef> Materials;
	std::vector<WModelMeshData> Meshes;
	WModelNodeData RootNode;
};

class WModelFile
{
public:
	static constexpr const wchar_t* Extension = L".wmodel";
	static constexpr const wchar_t* RootNodeName = L"WitchcraftModel";

	static std::wstring SerializeToText(const WModelFileData& data);
	static bool DeserializeFromText(const std::wstring& text, WModelFileData* outData);

	static bool SaveToFile(const std::filesystem::path& path, const WModelFileData& data);
	static bool LoadFromFile(const std::filesystem::path& path, WModelFileData* outData);
};
