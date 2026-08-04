#pragma once

#include <cstdint>
#include <filesystem>
#include <xstring>
#include <vector>

#include "Common/MeshSharedTypes.h"
#include "Common/SkeletonSharedTypes.h"
#include "Common/SkinningSharedTypes.h"
#include "Common/TransformSharedTypes.h"
#include "WitchcraftXmlFileBase.h"

struct WModelMaterialRef
{
	std::wstring Id;
	std::wstring File;
};

struct WModelMaterialSlot
{
	unsigned int Index = 0;
	std::wstring MaterialRef;
};

struct WModelMeshData
{
	std::wstring Id;
	std::wstring Name;
	std::vector<Vertex> Vertices;
	std::vector<Witchcraft::Animation::VertexBoneInfluence4> Skinning;
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
	std::wstring SkeletonAsset;
	std::wstring SkeletonName;
	Witchcraft::Animation::SkeletonTopology Skeleton;
	WModelNodeData RootNode;
};

class WModelFile : public WitchcraftXmlFileBase
{
public:
	static constexpr const wchar_t* Extension = L".wmodel";
	static constexpr const wchar_t* RootNodeName = L"WitchcraftModel";

	static std::wstring SerializeToText(const WModelFileData& data);
	static bool DeserializeFromText(const std::wstring& text, WModelFileData* outData);

	static bool SaveToFile(const std::filesystem::path& path, const WModelFileData& data);
	static bool LoadFromFile(const std::filesystem::path& path, WModelFileData* outData);

private:
	const wchar_t* GetRootNodeName() const override;
	void BuildBody(pugi::xml_node root) const override;
	bool ReadBody(const pugi::xml_node& root) override;

	WModelFileData m_data;
};
