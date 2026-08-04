#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>

#include "WModelFile.h"

namespace Witchcraft::WModelRuntime
{
	// .wmodel 在导入或场景恢复时使用的 CPU 侧运行时视图。
	// MeshesById 保存指向 Data.Meshes 的指针；替换 Data 后必须重建该表。
	struct WModelRuntimeAsset
	{
		std::filesystem::path ModelPath;
		std::filesystem::path ModelDirectory;
		WModelFileData Data;
		std::unordered_map<std::wstring, const WModelMeshData*> MeshesById;
		std::filesystem::path SkeletonAssetPath;
	};

	// 从 WModelNodeData 组装出的 CPU 侧 mesh payload。
	// 这里只合并数据，不上传 GPU 资源，也不修改 ECS 组件。
	struct WModelMeshPayload
	{
		std::vector<Vertex> Vertices;
		std::vector<std::uint32_t> Indices;
		std::vector<Witchcraft::Animation::VertexBoneInfluence4> Skinning;
		std::vector<const WModelNodeData*> AbsorbedMeshChildren;
		std::wstring PrimaryMaterialRef;
		bool ContainsSkinning = false;
		bool ContainsNonSkinned = false;
		bool HasCompleteSkinning = false;
	};

	// 解析 .wmodel/.wmat 中保存的相对引用路径。
	std::filesystem::path ResolveWModelReferencedPath(
		const std::filesystem::path& basePath,
		const std::wstring& referencedPath);

	// 解析 .wmodel 引用的骨架路径；没有显式引用时使用约定的 Animation 目录。
	std::filesystem::path BuildWModelSkeletonAssetPath(
		const std::filesystem::path& modelFilePath,
		const WModelFileData& modelFileData);

	// 按稳定的 .wmodel node id 查找节点。
	const WModelNodeData* FindWModelNodeById(
		const WModelNodeData& rootNodeData,
		const std::wstring& nodeId);

	// 查找第一个直接持有 mesh 引用的后代节点。
	const WModelNodeData* FindFirstWModelMeshDescendant(
		const WModelNodeData& rootNodeData);

	// 折叠没有 payload 且只有一个子节点的导入包装节点。
	WModelNodeData CollapseLeadingWModelWrapperNodes(
		WModelNodeData rootNodeData);

	// 读取 .wmodel 并构建运行时查找表。
	bool LoadWModelRuntimeAsset(
		const std::filesystem::path& modelFilePath,
		WModelRuntimeAsset* outAsset);

	// 从一个节点组装 vertices / indices / skinning / material。
	// allowAbsorbIdentityMeshChildren=true 时，可把同材质、identity transform 的 mesh 子节点吸收到空父节点；
	// 被吸收的子节点会返回给调用方，用于后续遍历时跳过，避免重复建实体。
	// 不读取磁盘资源，不创建材质，不上传 GPU，不修改 ECS；调用方负责根据 payload 绑定材质和运行时组件。
	bool BuildWModelMeshPayload(
		const WModelRuntimeAsset& asset,
		const WModelNodeData& nodeData,
		bool allowAbsorbIdentityMeshChildren,
		const DirectX::XMFLOAT4* overrideVertexColor,
		WModelMeshPayload* outPayload);
}
