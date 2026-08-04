#pragma once

#include <string>
#include <vector>

#include <Windows.h>
#include <DirectXMath.h>

#include "Common/AnimationSharedTypes.h"
#include "Common/SkeletonSharedTypes.h"
#include "Common/SkinningSharedTypes.h"
#include "Common/MeshSharedTypes.h"

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
	DirectX::XMFLOAT3 FresnelR0 = { 0.04f, 0.04f, 0.04f };
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
	ImportedTextureSource OpacityTexture;
	bool UseOpacityTexture = false;
};

struct ImportedBoneWeight
{
	std::uint32_t VertexIndex = 0;
	std::int32_t BoneIndex = -1;
	float Weight = 0.0f;
};

struct ImportedSkeletonData
{
	std::wstring Name;
	Witchcraft::Animation::SkeletonTopology Topology;
};

struct ImportedAnimationClipData
{
	Witchcraft::Animation::AnimationClipDesc Clip;
};

struct ImportedSkinnedSubmeshData
{
	std::wstring Name;
	std::wstring MaterialSlotName;
	std::uint32_t IndexStart = 0;
	std::uint32_t IndexCount = 0;
	std::uint32_t BaseVertex = 0;
};

struct ImportedSkinnedMeshData
{
	std::wstring Name;
	std::wstring SkeletonName;
	std::vector<Witchcraft::Animation::SkinnedVertex> Vertices;
	std::vector<std::uint32_t> Indices;
	std::vector<ImportedSkinnedSubmeshData> Submeshes;
};

struct ImportedSkinnedModelBundle
{
	ImportedSkeletonData Skeleton;
	std::vector<ImportedAnimationClipData> Animations;
	ImportedSkinnedMeshData SkinnedMesh;
	std::vector<ImportedMaterialInfo> Materials;
};
