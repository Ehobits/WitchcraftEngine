#pragma once

#include <xstring>
#include <vector>

#include <assimp\scene.h>
#include <assimp\Importer.hpp>
#include <assimp\postprocess.h>

#include "ServicesContainer/COMPONENT/MeshComponent.h"
#include "Engine/EngineUtils.h"
#include "ServicesContainer/Component/MeshComponent.h"
#include "../ServicesContainer/Component/BaseComponent.h"

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices32;

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

class AssimpLoader : public BaseComponent
{
public:
	void Create(D3DWindow* dx);
	// 加载原始模型
	std::vector<Mesh> LoadRawModel(std::wstring path);
	bool LoadModel(std::wstring path);

private:
	void ProcessRawNode(aiNode* node, const aiScene* scene, std::vector<Mesh>& arg);
	Mesh ProcessRawMesh(aiNode* node, aiMesh* mesh, const aiScene* scene);
	void ProcessNode(aiNode* node, const aiScene* scene, std::wstring path);
	void ProcessMesh(aiNode* node, aiMesh* mesh, const aiScene* scene, std::wstring path);

private:
	D3DWindow* m_dx = nullptr;
};
