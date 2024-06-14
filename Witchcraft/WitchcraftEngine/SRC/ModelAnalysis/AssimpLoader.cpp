#include "AssimpLoader.h"
#include <filesystem>
#include "ServicesContainer/ServicesContainer.h"
#include "ServicesContainer/COMPONENT/GeneralComponent.h"
#include "ServicesContainer/COMPONENT/TransformComponent.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"
#include "System/Assets.h"

#define CUBE_MODEL L"\\DATA\\Models\\Cube.obj"
#define SPHERE_MODEL L"\\DATA\\Models\\Sphere.obj"
#define CAPSULE_MODEL L"\\DATA\\Models\\Capsule.obj"
#define PLANE_MODEL L"\\DATA\\Models\\Plane.obj"

static std::vector<Mesh> cube;
static std::vector<Mesh> sphere;
static std::vector<Mesh> capsule;
static std::vector<Mesh> plane;

void AssimpLoader::Create(D3DWindow* dx)
{
	m_dx = dx;
}

std::vector<Mesh> AssimpLoader::LoadRawModel(std::wstring path)
{
	assert(!path.empty());
	assert(std::filesystem::exists(path));

	std::vector<Mesh> buffer;

	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(SString::WstringToUTF8(path), aiProcess_Triangulate | aiProcess_ConvertToLeftHanded);

	if (pScene == nullptr)
		return buffer;

	ProcessRawNode(pScene->mRootNode, pScene, buffer);
	return buffer;
}

void AssimpLoader::ProcessRawNode(aiNode* node, const aiScene* scene, std::vector<Mesh>& arg)
{
	for (UINT i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		arg.push_back(ProcessRawMesh(node, mesh, scene));
	}

	for (UINT i = 0; i < node->mNumChildren; i++)
		ProcessRawNode(node->mChildren[i], scene, arg);
}

Mesh AssimpLoader::ProcessRawMesh(aiNode* node, aiMesh* mesh, const aiScene* scene)
{
	Mesh buffer;

	for (UINT i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;

		// positions
		vertex.Pos.x = mesh->mVertices[i].x;
		vertex.Pos.y = mesh->mVertices[i].y;
		vertex.Pos.z = mesh->mVertices[i].z;

		// color
		if (mesh->mColors[0])
		{
			vertex.Color.x = mesh->mColors[0][i].r;
			vertex.Color.y = mesh->mColors[0][i].g;
			vertex.Color.z = mesh->mColors[0][i].b;
			vertex.Color.w = mesh->mColors[0][i].a;
		}
		else
		{
			vertex.Color.x = 0.0f;
			vertex.Color.y = 0.0f;
			vertex.Color.z = 0.0f;
			vertex.Color.w = 1.0f;
		}

		// normals
		if (mesh->HasNormals())
		{
			vertex.Normal.x = mesh->mNormals[i].x;
			vertex.Normal.y = mesh->mNormals[i].y;
			vertex.Normal.z = mesh->mNormals[i].z;
		}

		// texture coordinates
		if (mesh->mTextureCoords[0])
		{
			vertex.TexC.x = mesh->mTextureCoords[0][i].x;
			vertex.TexC.y = mesh->mTextureCoords[0][i].y;
		}
		else
		{
			vertex.TexC = DirectX::XMFLOAT2(0.0f, 0.0f);
		}

		// Tangents
		if (mesh->HasTangentsAndBitangents())
		{
			vertex.TangentU.x = mesh->mTangents[i].x;
			vertex.TangentU.y = mesh->mTangents[i].y;
			vertex.TangentU.z = mesh->mTangents[i].z;
		}
		else
		{
			vertex.TangentU.x = -0.1f;
			vertex.TangentU.y = 0.0f;
			vertex.TangentU.z = +0.1f;
		}

		buffer.vertices.push_back(vertex);
	}

	for (UINT i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (UINT j = 0; j < face.mNumIndices; j++)
			buffer.indices32.push_back(face.mIndices[j]);
	}

	return buffer;
}

/* ------------------------------------ */

bool AssimpLoader::LoadModel(std::wstring path)
{
	Assimp::Importer importer;
	importer.SetPropertyInteger(AI_CONFIG_FBX_CONVERT_TO_M, FALSE);
	const aiScene* pScene = importer.ReadFile(SString::WstringToUTF8(path), aiProcess_Triangulate | aiProcess_ConvertToLeftHanded);
	if (pScene == nullptr) return false;

	if (pScene->HasMaterials())
	{
		std::wstring buffer = EngineUtils::GetProjectDirPath() + L"\\" + std::filesystem::path(path).stem().wstring().c_str();
		//assetsWindow->CreateDir(buffer);

		for (unsigned int i = 0; i < pScene->mNumMaterials; ++i)
		{
			aiMaterial* material = pScene->mMaterials[i];
			aiString       mat_name = material->GetName();
			std::wstring    full_path = buffer + L"\\" + SString::UTF8ToWstring(mat_name.C_Str());
			aiString       diff_name;
			material->GetTexture(aiTextureType_DIFFUSE, NULL, &diff_name);
			MaterialBuffer mat_buff;
			if (!std::string(diff_name.C_Str()).empty())
				mat_buff.DiffusePath = std::wstring(L"../") + SString::UTF8ToWstring(diff_name.C_Str());
			//consoleWindow->AddDebugMessage(L"Creating material... %s%s", full_path.c_str(), MAT);
			//assetsWindow->SaveMaterialFile(full_path, mat_buff);
		}
	}

	ProcessNode(pScene->mRootNode, pScene, path);
	return true;
}

void AssimpLoader::ProcessNode(aiNode* node, const aiScene* scene, std::wstring path)
{
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		ProcessMesh(node, mesh, scene, path);
	}
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene, path);
	}
}

void AssimpLoader::ProcessMesh(aiNode* node, aiMesh* mesh, const aiScene* scene, std::wstring path)
{
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;

		// positions
		vertex.Pos.x = mesh->mVertices[i].x;
		vertex.Pos.y = mesh->mVertices[i].y;
		vertex.Pos.z = mesh->mVertices[i].z;

		// color
		if (mesh->mColors[0])
		{
			vertex.Color.x = mesh->mColors[0][i].r;
			vertex.Color.y = mesh->mColors[0][i].g;
			vertex.Color.z = mesh->mColors[0][i].b;
			vertex.Color.w = mesh->mColors[0][i].a;
		}
		else
		{
			vertex.Color.x = 0.0f;
			vertex.Color.y = 0.0f;
			vertex.Color.z = 0.0f;
			vertex.Color.w = 1.0f;
		}

		// normals
		if (mesh->HasNormals())
		{
			vertex.Normal.x = mesh->mNormals[i].x;
			vertex.Normal.y = mesh->mNormals[i].y;
			vertex.Normal.z = mesh->mNormals[i].z;
		}

		// texture coordinates
		if (mesh->mTextureCoords[0])
		{
			vertex.TexC.x = mesh->mTextureCoords[0][i].x;
			vertex.TexC.y = mesh->mTextureCoords[0][i].y;
		}
		else
		{
			vertex.TexC = DirectX::XMFLOAT2(0.0f, 0.0f);
		}

		//mesh_comp.AddVertices(vertex);
	}

	unsigned int i = 0, j = 0;
	for (i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		//for (j = 0; j < face.mNumIndices; j++)
			//mesh_comp.AddIndices(face.mIndices[j]);
	}

	//mesh_comp.SetupMesh(m_ComponentServices, m_dx, mesh_comp.GetIndexCount(), mesh_comp.GetVertexCount()); /* create mesh */

	aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
	std::wstring buffer = std::filesystem::path(path).parent_path().wstring() + L"\\" + std::filesystem::path(path).stem().wstring() + L"\\" + SString::UTF8ToWstring(material->GetName().C_Str()) + L".mat";
	//mesh_comp.BindingMaterial(m_dx, buffer);
}