#pragma once

#include <filesystem>
#include <xstring>
#include <vector>

#include <assimp\scene.h>
#include <assimp\Importer.hpp>
#include <assimp\material.h>
#include <assimp\postprocess.h>

#include "Common/MeshSharedTypes.h"
#include "ECS/ServicesContainer/ServicesContainer.h"
#include "Engine/EngineUtils.h"
#include "ImportedAssetTypes.h"

class D3DWindow;
class WitchcraECS;
class ConsoleWindow;

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

// AssimpLoader 只负责模型导入/转换，不再扮演 ECS 里的 MeshComponent。
class AssimpLoader
{
public:
	void Create(D3DWindow* dx);
	void SetConsoleWindow(ConsoleWindow* consoleWindow);
	// 读取原始网格数据，不参与场景节点和材质实例化。
	std::vector<Mesh> LoadRawModel(std::wstring path);

	// 新导入流程：直接创建场景实体、子网格和材质。
	bool ImportModelToScene(const std::wstring& path, WitchcraECS* ecs, const std::wstring& rootName, const Transform& transform);
	// 读取 .wmodel 并重建实体层级、网格与材质绑定。
	bool LoadWModelToScene(const std::wstring& path, WitchcraECS* ecs, const std::wstring& rootName, const Transform& transform);

private:
	bool ConvertModelToWModel(const std::wstring& path, const std::wstring& rootName, std::filesystem::path& outModelFilePath);
	void ProcessRawNode(aiNode* node, const aiScene* scene, std::vector<Mesh>& arg);
	Mesh ProcessRawMesh(aiNode* node, aiMesh* mesh, const aiScene* scene);
	// 组装材质所需的颜色参数与贴图引用。
	ImportedMaterialInfo BuildImportedMaterial(aiMaterial* material, const aiScene* scene, const std::filesystem::path& modelPath, const std::filesystem::path& extractedTextureDir, UINT materialIndex) const;
	// 解析外部纹理路径或解包内嵌纹理到磁盘。
	ImportedTextureSource ResolveImportedTexture(const aiScene* scene, const std::filesystem::path& modelPath, const std::filesystem::path& extractedTextureDir, const aiString& texturePath, UINT materialIndex, const wchar_t* slotName) const;

private:
	D3DWindow* m_dx = nullptr;
	ConsoleWindow* m_consoleWindow = nullptr;
};
