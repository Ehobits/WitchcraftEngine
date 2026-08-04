#pragma once

#include <filesystem>
#include <xstring>
#include <vector>

#include <assimp\scene.h>
#include <assimp\Importer.hpp>
#include <assimp\material.h>
#include <assimp\postprocess.h>

#include "Common/MeshSharedTypes.h"
#include "Common/SceneEntityType.h"
#include "Common/TransformSharedTypes.h"
#include "Engine/EngineUtils.h"
#include "ImportedAssetTypes.h"

class Engine;
class WitchcraECS;
class ConsoleWindow;
class D3DWindow;
class SceneEntityBase;

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices32;

	// D3D 运行时部分仍有 16-bit 索引路径，这里按需缓存一份转换结果。
	std::vector<uint16_t>& GetIndices16()
	{
		if (mIndices16.empty())
		{
			mIndices16.resize(indices32.size());
			for (size_t i = 0; i < indices32.size(); ++i)
				mIndices16[i] = static_cast<uint16_t>(indices32[i]);
		}

		return mIndices16;
	}

private:
	std::vector<uint16_t> mIndices16;
};

// AssimpLoader 只负责模型导入/转换，不扮演 ECS 里的 MeshComponent。
class AssimpLoader
{
public:
	void Create(Engine* engine);
	void SetConsoleWindow(ConsoleWindow* consoleWindow);
	// 读取原始网格数据，不参与场景节点和材质实例化。
	std::vector<Mesh> LoadRawModel(std::wstring path);

	// 新导入流程：直接创建场景实体、子网格和材质。
	bool ImportModelToScene(const std::wstring& path, WitchcraECS* ecs, const std::wstring& rootName, const Transform& transform, SceneEntityType sceneType = SceneEntityType::StaticScenery);
	// 读取 .wmodel 并重建实体层级、网格与材质绑定。
	bool LoadWModelToScene(const std::wstring& path, WitchcraECS* ecs, const std::wstring& rootName, const Transform& transform, SceneEntityType sceneType = SceneEntityType::StaticScenery);
	// 导入骨架和动画资源；蒙皮信息改为直接保存在 .wmodel 中。
	bool ImportSkinnedModelAssets(
		const std::wstring& path,
		const std::wstring& rootName,
		std::filesystem::path* outSkeletonFilePath = nullptr,
		std::vector<std::filesystem::path>* outAnimationFilePaths = nullptr,
		std::filesystem::path* outSkinnedMeshFilePath = nullptr);
	// 从场景文件恢复 .wmodel 网格时复用导入时的 GPU 布局。
	// 普通的 MeshComponent 上传不含骨骼索引/权重。
	bool UploadInlineSkinnedMeshGeometry(
		const std::wstring& geometryName,
		const std::vector<Vertex>& vertices,
		const std::vector<Witchcraft::Animation::VertexBoneInfluence4>& skinning,
		const std::vector<std::uint32_t>& indices) const;
	// 上传 inline skinned geometry，并把结果绑定回指定 mesh entity。
	// 只负责 GPU geometry、external geometry、SkinnedMeshComponent 和可选包围盒重建；
	// 不创建实体，不推导 skeleton owner，不加载 skeleton/animation asset，也不修改 Animator/SkinningRuntime。
	bool UploadAndBindInlineSkinnedMeshGeometry(
		WitchcraECS* ecs,
		SceneEntityBase* entity,
		const std::wstring& geometryName,
		const std::vector<Vertex>& vertices,
		const std::vector<Witchcraft::Animation::VertexBoneInfluence4>& skinning,
		const std::vector<std::uint32_t>& indices,
		const std::wstring& skeletonAssetPath,
		bool rebuildBoundingBox) const;

private:
	static std::wstring SanitizeName(std::wstring value);
	static std::wstring BuildImportedModelFileName(const std::wstring& modelName);
	static std::wstring BuildModelMaterialId(UINT materialIndex);
	static std::wstring BuildModelMeshId(UINT meshIndex);
	static std::wstring BuildRelativeAssetPath(const std::filesystem::path& path, const std::filesystem::path& baseDirectory);
	static std::filesystem::path BuildImportedMaterialFilePath(
		const std::filesystem::path& materialsDir,
		const std::wstring& modelName,
		const std::wstring& materialName);
	static std::wstring ResolvePreferredTextureName(
		const aiString& texturePath,
		const aiTexture* embeddedTexture,
		const std::wstring& fallbackStem,
		const std::wstring& fallbackExtension);
	static std::filesystem::path CopyImportedTextureToProject(
		const std::filesystem::path& sourcePath,
		const std::filesystem::path& extractedTextureDir);
	static std::wstring ResolveModelNodeName(const aiNode* node, const std::wstring& fallbackName);
	static std::wstring ResolveModelMeshName(const aiMesh* mesh, UINT meshIndex, const std::wstring& fallbackPrefix);
	static std::wstring BuildImportedAnimationFileName(const std::wstring& rootName, const std::wstring& clipName);
	static std::wstring BuildImportedSkeletonFileName(const std::wstring& rootName);
	static std::wstring BuildImportedSkinnedMeshFileName(const std::wstring& rootName);
	static std::wstring NormalizeBindingName(const std::wstring& value);
	static int ComputeSkinnedBindingScore(
		const std::wstring& submeshName,
		const std::wstring& submeshMaterialName,
		std::uint32_t submeshIndexCount,
		const std::wstring& meshName,
		const std::wstring& entityName,
		const std::wstring& meshMaterialName,
		std::uint32_t meshIndexCount);

	bool ConvertModelToWModel(
		const std::wstring& path,
		const std::wstring& rootName,
		const std::filesystem::path* skeletonFilePath,
		std::filesystem::path& outModelFilePath);
	bool ExtractSkinnedModelBundle(const std::wstring& path, const std::wstring& rootName, ImportedSkinnedModelBundle* outBundle) const;
	bool SaveImportedSkinnedModelBundle(
		const ImportedSkinnedModelBundle& bundle,
		const std::wstring& rootName,
		std::filesystem::path* outSkeletonFilePath,
		std::vector<std::filesystem::path>* outAnimationFilePaths,
		std::filesystem::path* outSkinnedMeshFilePath) const;
	bool UploadSkinnedMeshGeometry(
		const std::wstring& geometryName,
		const ImportedSkinnedMeshData& skinnedMeshData) const;
	// 加工原始网格
	void ProcessRawNode(aiNode* node, const aiScene* scene, std::vector<Mesh>& arg);
	Mesh ProcessRawMesh(aiNode* node, aiMesh* mesh, const aiScene* scene) const;
	// 组装材质所需的颜色参数与贴图引用。
	ImportedMaterialInfo BuildImportedMaterial(aiMaterial* material, const aiScene* scene, const std::filesystem::path& modelPath, const std::filesystem::path& extractedTextureDir, unsigned int materialIndex) const;
	// 解析外部纹理路径或解包内嵌纹理到磁盘。
	ImportedTextureSource ResolveImportedTexture(const aiScene* scene, const std::filesystem::path& modelPath, const std::filesystem::path& extractedTextureDir, const aiString& texturePath, unsigned int materialIndex, const wchar_t* slotName) const;
	bool LoadWModelToSceneInternal(
		const std::wstring& path,
		WitchcraECS* ecs,
		const std::wstring& rootName,
		const Transform& transform,
		SceneEntityType sceneType,
		bool stripEmptyHierarchyEntities);

private:
	Engine* m_engine = nullptr;
	ConsoleWindow* m_consoleWindow = nullptr;
};
