#include "AssimpLoader.h"
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include "ECS/WitchcraECS.h"
#include "Engine/Engine.h"
#include "D3DWindow/D3DWindow.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"
#include "Editor/Window/ConsoleWindow.h"
#include "System/Assets.h"
#include "System/WitchcraftFile/WMaterialFile.h"
#include "System/WitchcraftFile/WModelFile.h"

namespace
{
	// 过滤文件系统和实体命名中不安全的字符，避免导入名直接参与资源命名时出错。
	std::wstring SanitizeName(std::wstring value)
	{
		if (value.empty())
			return L"Imported";

		for (wchar_t& ch : value)
		{
			switch (ch)
			{
			case L'\\':
			case L'/':
			case L':':
			case L'*':
			case L'?':
			case L'"':
			case L'<':
			case L'>':
			case L'|':
				ch = L'_';
				break;
			default:
				break;
			}
		}

		return value;
	}

	std::filesystem::path BuildImportedMaterialFilePath(
		const std::filesystem::path& materialsDir,
		UINT materialIndex,
		const std::wstring& materialName)
	{
		// 材质文件名带上原始材质索引，避免同名子材质在导入时互相覆盖。
		const std::wstring safeMaterialName = SanitizeName(materialName.empty() ? L"Material" : materialName);
		const std::wstring fileName =
			std::to_wstring(materialIndex) + L"_" + safeMaterialName + WMaterialFile::Extension;
		return materialsDir / fileName;
	}

	std::wstring GetSafeTextureFileName(const std::wstring& preferredName, const std::wstring& fallbackStem, const std::wstring& fallbackExtension)
	{
		std::filesystem::path filePath(preferredName);
		const std::wstring fileStem = SanitizeName(filePath.stem().wstring());
		std::wstring fileExtension = filePath.extension().wstring();
		if (!fileStem.empty())
		{
			if (fileExtension.empty())
				fileExtension = fallbackExtension;
			return fileStem + fileExtension;
		}

		return fallbackStem + fallbackExtension;
	}

	bool IsEmbeddedTextureToken(const std::wstring& textureReference)
	{
		if (textureReference.empty() || textureReference[0] != L'*')
			return false;

		for (size_t index = 1; index < textureReference.size(); ++index)
		{
			if (!iswdigit(textureReference[index]))
				return false;
		}

		return textureReference.size() > 1;
	}

	std::wstring ResolvePreferredTextureName(
		const aiString& texturePath,
		const aiTexture* embeddedTexture,
		const std::wstring& fallbackStem,
		const std::wstring& fallbackExtension)
	{
		const std::wstring textureReference = SString::UTF8ToWstring(texturePath.C_Str());
		if (!textureReference.empty() && !IsEmbeddedTextureToken(textureReference))
		{
			return GetSafeTextureFileName(textureReference, fallbackStem, fallbackExtension);
		}

		if (embeddedTexture != nullptr)
		{
			const std::wstring embeddedFileName = SString::UTF8ToWstring(embeddedTexture->mFilename.C_Str());
			if (!embeddedFileName.empty())
				return GetSafeTextureFileName(embeddedFileName, fallbackStem, fallbackExtension);
		}

		return fallbackStem + fallbackExtension;
	}

	std::filesystem::path MakeUniqueFilePath(const std::filesystem::path& directory, const std::wstring& preferredFileName)
	{
		const std::filesystem::path preferredPath = directory / preferredFileName;
		if (!std::filesystem::exists(preferredPath))
			return preferredPath;

		const std::filesystem::path preferredStem = std::filesystem::path(preferredFileName).stem();
		const std::filesystem::path preferredExtension = std::filesystem::path(preferredFileName).extension();
		for (UINT suffix = 1; ; ++suffix)
		{
			const std::filesystem::path candidate =
				directory / (preferredStem.wstring() + L"_" + std::to_wstring(suffix) + preferredExtension.wstring());
			if (!std::filesystem::exists(candidate))
				return candidate;
		}
	}

	std::filesystem::path CopyImportedTextureToProject(
		const std::filesystem::path& sourcePath,
		const std::filesystem::path& extractedTextureDir)
	{
		if (sourcePath.empty() || !std::filesystem::exists(sourcePath))
			return {};

		std::filesystem::create_directories(extractedTextureDir);

		const std::filesystem::path preferredOutputPath = extractedTextureDir / sourcePath.filename();
		std::error_code errorCode;
		if (std::filesystem::exists(preferredOutputPath))
		{
			if (std::filesystem::equivalent(sourcePath, preferredOutputPath, errorCode))
				return preferredOutputPath;

			errorCode.clear();
			return MakeUniqueFilePath(extractedTextureDir, sourcePath.filename().wstring());
		}

		return preferredOutputPath;
	}

	std::wstring DisplayValueOrPlaceholder(const std::wstring& value)
	{
		return value.empty() ? L"<empty>" : value;
	}

	std::wstring BuildAssimpTextureDebugSummary(
		const aiString& texturePath,
		const aiTexture* embeddedTexture)
	{
		const std::wstring textureReference = SString::UTF8ToWstring(texturePath.C_Str());
		std::wstring aiEmbeddedFileName;
		std::wstring preferredName;
		std::wstring formatHint;

		if (embeddedTexture != nullptr)
		{
			aiEmbeddedFileName = SString::UTF8ToWstring(embeddedTexture->mFilename.C_Str());
			formatHint = SString::UTF8ToWstring(embeddedTexture->achFormatHint);
			const std::wstring extension = formatHint.empty() ? L".png" : L"." + formatHint;
			preferredName = ResolvePreferredTextureName(
				texturePath,
				embeddedTexture,
				L"embedded_texture",
				extension);
		}

		return L"texturePath=" + DisplayValueOrPlaceholder(textureReference) +
			L" | aiTexture::mFilename=" + DisplayValueOrPlaceholder(aiEmbeddedFileName) +
			L" | formatHint=" + DisplayValueOrPlaceholder(formatHint) +
			L" | preferred=" + DisplayValueOrPlaceholder(preferredName);
	}

	std::wstring BuildTextureUsageSummary(const ImportedTextureSource& textureSource, const wchar_t* slotName)
	{
		if (textureSource.AssetName.empty() && textureSource.Path.empty())
			return L"";

		const std::wstring textureName = !textureSource.AssetName.empty()
			? textureSource.AssetName
			: std::filesystem::path(textureSource.Path).filename().wstring();
		return std::wstring(slotName) + L"=" + textureName;
	}

	std::wstring BuildImportedModelFileName(const std::wstring& modelName)
	{
		return SanitizeName(modelName.empty() ? L"Model" : modelName) + WModelFile::Extension;
	}

	std::wstring BuildModelMaterialId(UINT materialIndex)
	{
		return L"mat_" + std::to_wstring(materialIndex);
	}

	std::wstring BuildModelMeshId(UINT meshIndex)
	{
		return L"mesh_" + std::to_wstring(meshIndex);
	}

	std::wstring BuildRelativeAssetPath(const std::filesystem::path& path, const std::filesystem::path& baseDirectory)
	{
		std::error_code errorCode;
		const std::filesystem::path relativePath = std::filesystem::relative(path, baseDirectory, errorCode);
		if (!errorCode && !relativePath.empty())
			return relativePath.generic_wstring();

		const std::filesystem::path lexicalRelativePath = path.lexically_relative(baseDirectory);
		if (!lexicalRelativePath.empty())
			return lexicalRelativePath.generic_wstring();

		return path.generic_wstring();
	}

	Transform MakeIdentityTransform()
	{
		return Transform();
	}

	DirectX::XMFLOAT3 QuaternionToEulerDegrees(const aiQuaternion& rotation)
	{
		const double x = static_cast<double>(rotation.x);
		const double y = static_cast<double>(rotation.y);
		const double z = static_cast<double>(rotation.z);
		const double w = static_cast<double>(rotation.w);

		const double sinrCosp = 2.0 * (w * x + y * z);
		const double cosrCosp = 1.0 - 2.0 * (x * x + y * y);
		const double roll = std::atan2(sinrCosp, cosrCosp);

		const double sinp = 2.0 * (w * y - z * x);
		const double pitch = std::abs(sinp) >= 1.0 ? std::copysign(DirectX::XM_PIDIV2, sinp) : std::asin(sinp);

		const double sinyCosp = 2.0 * (w * z + x * y);
		const double cosyCosp = 1.0 - 2.0 * (y * y + z * z);
		const double yaw = std::atan2(sinyCosp, cosyCosp);

		return DirectX::XMFLOAT3(
			static_cast<float>(roll * 180.0 / DirectX::XM_PI),
			static_cast<float>(pitch * 180.0 / DirectX::XM_PI),
			static_cast<float>(yaw * 180.0 / DirectX::XM_PI));
	}

	Transform ConvertAiTransform(const aiMatrix4x4& transformMatrix)
	{
		aiVector3D scale;
		aiQuaternion rotation;
		aiVector3D position;
		transformMatrix.Decompose(scale, rotation, position);

		Transform transform;
		transform.position = DirectX::XMFLOAT3(position.x, position.y, position.z);
		transform.rotation = QuaternionToEulerDegrees(rotation);
		transform.scale = DirectX::XMFLOAT3(scale.x, scale.y, scale.z);
		return transform;
	}

	std::wstring ResolveModelNodeName(const aiNode* node, const std::wstring& fallbackName)
	{
		if (node != nullptr)
		{
			const std::wstring nodeName = SanitizeName(SString::UTF8ToWstring(node->mName.C_Str()));
			if (!nodeName.empty())
				return nodeName;
		}

		return SanitizeName(fallbackName.empty() ? L"Node" : fallbackName);
	}

	std::wstring ResolveModelMeshName(const aiMesh* mesh, UINT meshIndex, const std::wstring& fallbackPrefix)
	{
		if (mesh != nullptr)
		{
			const std::wstring meshName = SanitizeName(SString::UTF8ToWstring(mesh->mName.C_Str()));
			if (!meshName.empty())
				return meshName;
		}

		return SanitizeName(fallbackPrefix) + L"_Mesh_" + std::to_wstring(meshIndex);
	}

	WModelNodeData BuildModelHierarchyNode(
		aiNode* node,
		const aiScene* scene,
		const std::vector<std::wstring>& meshIds,
		const std::vector<std::wstring>& materialIds,
		UINT& nodeCounter,
		const std::wstring& fallbackName)
	{
		WModelNodeData nodeData;
		nodeData.Id = L"node_" + std::to_wstring(nodeCounter++);
		nodeData.Name = ResolveModelNodeName(node, fallbackName);
		nodeData.Type = WModelNodeType::Empty;
		nodeData.LocalTransform = node != nullptr ? ConvertAiTransform(node->mTransformation) : MakeIdentityTransform();

		if (node != nullptr)
		{
			for (UINT localMeshIndex = 0; localMeshIndex < node->mNumMeshes; ++localMeshIndex)
			{
				const UINT meshIndex = node->mMeshes[localMeshIndex];
				if (meshIndex >= meshIds.size() || meshIndex >= scene->mNumMeshes)
					continue;

				aiMesh* mesh = scene->mMeshes[meshIndex];
				WModelNodeData meshNode;
				meshNode.Id = nodeData.Id + L"_mesh_" + std::to_wstring(localMeshIndex);
				meshNode.Name = ResolveModelMeshName(mesh, meshIndex, nodeData.Name);
				meshNode.Type = WModelNodeType::Mesh;
				meshNode.LocalTransform = MakeIdentityTransform();
				meshNode.MeshRef = meshIds[meshIndex];

				if (mesh != nullptr && mesh->mMaterialIndex < materialIds.size())
				{
					WModelMaterialSlot slot;
					slot.Index = 0;
					slot.MaterialRef = materialIds[mesh->mMaterialIndex];
					meshNode.MaterialSlots.push_back(std::move(slot));
				}

				nodeData.Children.push_back(std::move(meshNode));
			}

			for (UINT childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
			{
				const std::wstring childFallbackName = nodeData.Name + L"_Child_" + std::to_wstring(childIndex);
				nodeData.Children.push_back(
					BuildModelHierarchyNode(node->mChildren[childIndex], scene, meshIds, materialIds, nodeCounter, childFallbackName));
			}
		}

		return nodeData;
	}

	Transform CombineTransforms(const Transform& parentTransform, const Transform& localTransform)
	{
		Transform combined = localTransform;
		combined.position.x += parentTransform.position.x;
		combined.position.y += parentTransform.position.y;
		combined.position.z += parentTransform.position.z;
		combined.rotation.x += parentTransform.rotation.x;
		combined.rotation.y += parentTransform.rotation.y;
		combined.rotation.z += parentTransform.rotation.z;
		combined.scale.x *= parentTransform.scale.x;
		combined.scale.y *= parentTransform.scale.y;
		combined.scale.z *= parentTransform.scale.z;
		return combined;
	}

	bool CanCollapseLeadingNode(const WModelNodeData& nodeData)
	{
		return nodeData.Type == WModelNodeType::Empty &&
			nodeData.MeshRef.empty() &&
			nodeData.MaterialSlots.empty() &&
			nodeData.Children.size() == 1;
	}

	WModelNodeData CollapseLeadingWrapperNodes(WModelNodeData rootNodeData)
	{
		while (CanCollapseLeadingNode(rootNodeData))
		{
			WModelNodeData childNode = std::move(rootNodeData.Children.front());
			childNode.LocalTransform = CombineTransforms(rootNodeData.LocalTransform, childNode.LocalTransform);

			if (childNode.Name.empty())
				childNode.Name = rootNodeData.Name;
			if (childNode.Id.empty())
				childNode.Id = rootNodeData.Id;

			rootNodeData = std::move(childNode);
		}

		return rootNodeData;
	}

	std::filesystem::path ResolveReferencedPath(const std::filesystem::path& basePath, const std::wstring& referencedPath)
	{
		if (referencedPath.empty())
			return {};

		std::filesystem::path resolvedPath(referencedPath);
		if (resolvedPath.is_relative())
			resolvedPath = basePath / resolvedPath;

		return resolvedPath.lexically_normal();
	}

	ImportedTextureSource BuildImportedTextureSource(const std::filesystem::path& materialFilePath, const std::wstring& textureName)
	{
		ImportedTextureSource source;
		if (textureName.empty())
			return source;

		std::filesystem::path texturePath(textureName);
		if (texturePath.is_relative())
		{
			if (!texturePath.has_parent_path())
				texturePath = materialFilePath.parent_path().parent_path() / L"Textures" / texturePath;
			else
				texturePath = materialFilePath.parent_path() / texturePath;
		}

		source.Path = texturePath.lexically_normal().wstring();
		source.AssetName = std::filesystem::path(textureName).filename().wstring();
		return source;
	}

	ImportedMaterialInfo ConvertMaterialFileDataToImportedInfo(const WMaterialFileData& materialData, const std::filesystem::path& materialFilePath)
	{
		ImportedMaterialInfo info;
		info.Name = materialData.MaterialName.empty() ? materialFilePath.stem().wstring() : materialData.MaterialName;
		info.DiffuseColor = materialData.DiffuseColor;
		info.Emissive = materialData.Emissive;
		info.Metallic = materialData.Metallic;
		info.Roughness = materialData.Roughness;
		info.Opacity = materialData.Opacity;
		info.DiffuseColor.w = materialData.Opacity;
		info.DiffuseTexture = BuildImportedTextureSource(materialFilePath, materialData.DiffuseTexture);
		info.NormalTexture = BuildImportedTextureSource(materialFilePath, materialData.NormalTexture);
		info.MetallicTexture = BuildImportedTextureSource(materialFilePath, materialData.MetallicTexture);
		info.RoughnessTexture = BuildImportedTextureSource(materialFilePath, materialData.RoughnessTexture);
		return info;
	}

	SceneEntityBase* CreateEntityForModelNode(
		WitchcraECS* ecs,
		const WModelNodeData& nodeData,
		const std::wstring& entityName,
		SceneEntityBase* parentEntity,
		const std::wstring& filePath,
		const std::wstring& MeshName,
		const WModelMeshData* meshData)
	{
		if (ecs == nullptr)
			return nullptr;

		const bool isMeshNode = nodeData.Type == WModelNodeType::Mesh && meshData != nullptr;
		SceneEntityBase* entity = isMeshNode
			? ecs->CreateMeshEntity(entityName, parentEntity)
			: ecs->CreateBasicEntity(entityName, parentEntity, ComponentType::Co_Unk);
		if (entity == nullptr)
			return nullptr;

		if (isMeshNode)
		{
			if (!ecs->ConfigureMeshEntity(
				entity,
				nullptr,
				entityName,
				filePath,
				MeshName,
				不透明物体渲染项目,
				L""))
			{
				return entity;
			}

			// 这里先只回填 CPU 侧网格数据，真正的 GPU 资源创建放在后面的 SetupMesh 中完成。
			ecs->AppendMeshEntityVertices(entity, meshData->Vertices);
			ecs->AppendMeshEntityIndices(entity, meshData->Indices);
		}

		return entity;
	}
}

void AssimpLoader::Create(Engine* engine)
{
	m_engine = engine;
}

void AssimpLoader::SetConsoleWindow(ConsoleWindow* consoleWindow)
{
	m_consoleWindow = consoleWindow;
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

	// 将整棵节点树中的子网格拍平到一个数组，便于后续构建 Mesh。
	ProcessRawNode(pScene->mRootNode, pScene, buffer);
	return buffer;
}

void AssimpLoader::ProcessRawNode(aiNode* node, const aiScene* scene, std::vector<Mesh>& arg)
{
	// 当前节点引用的每个 aiMesh 都独立转成一个 Mesh，保留子网格边界。
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

	// 原始导入阶段只做数据搬运，不在这里推导材质或场景对象。
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

		// Tangents Bitangent
		if (mesh->HasTangentsAndBitangents())
		{
			vertex.Tangent.x = mesh->mTangents[i].x;
			vertex.Tangent.y = mesh->mTangents[i].y;
			vertex.Tangent.z = mesh->mTangents[i].z;
			vertex.Bitangent.x= mesh->mBitangents[i].x;
			vertex.Bitangent.y= mesh->mBitangents[i].y;
			vertex.Bitangent.z= mesh->mBitangents[i].z;
		}
		else
		{
			vertex.Tangent.x = -0.1f;
			vertex.Tangent.y = 0.0f;
			vertex.Tangent.z = +0.1f;
			vertex.Bitangent.x = -0.1f;
			vertex.Bitangent.y = 0.0f;
			vertex.Bitangent.z = +0.1f;
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

ImportedTextureSource AssimpLoader::ResolveImportedTexture(
	const aiScene* scene,
	const std::filesystem::path& modelPath,
	const std::filesystem::path& extractedTextureDir,
	const aiString& texturePath,
	UINT materialIndex,
	const wchar_t* slotName) const
{
	ImportedTextureSource result;

	if (texturePath.length == 0)
		return result;

	// 先尝试按 Assimp 的内嵌纹理表解析，适配 glb / fbx 等打包纹理场景。
	const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(texturePath.C_Str());
	if (embeddedTexture != nullptr)
	{
		std::filesystem::create_directories(extractedTextureDir);

		std::string formatHint = embeddedTexture->achFormatHint;
		if (formatHint.empty())
			formatHint = "png";

		std::wstring extension = L"." + SString::UTF8ToWstring(formatHint);
		const std::wstring preferredFileName = ResolvePreferredTextureName(
			texturePath,
			embeddedTexture,
			L"mat_" + std::to_wstring(materialIndex) + L"_" + slotName,
			extension);
		std::filesystem::path outputPath = MakeUniqueFilePath(extractedTextureDir, preferredFileName);

		if (embeddedTexture->mHeight == 0)
		{
			std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
			if (output.is_open())
			{
				output.write(reinterpret_cast<const char*>(embeddedTexture->pcData), embeddedTexture->mWidth);
				output.close();
				result.Path = outputPath.wstring();
				result.AssetName = outputPath.filename().wstring();
			}
		}

		return result;
	}

	// 否则按模型目录下的相对路径解析外部贴图。
	std::filesystem::path resolvedPath = SString::UTF8ToWstring(texturePath.C_Str());
	if (resolvedPath.is_relative())
		resolvedPath = modelPath.parent_path() / resolvedPath;

	resolvedPath = resolvedPath.lexically_normal();
	if (std::filesystem::exists(resolvedPath))
	{
		const std::filesystem::path copiedPath = CopyImportedTextureToProject(resolvedPath, extractedTextureDir);
		if (!copiedPath.empty())
		{
			std::error_code errorCode;
			if (!std::filesystem::exists(copiedPath))
				std::filesystem::copy_file(resolvedPath, copiedPath, std::filesystem::copy_options::overwrite_existing, errorCode);

			if (!errorCode)
			{
				result.Path = copiedPath.wstring();
				result.AssetName = copiedPath.filename().wstring();
			}
		}
	}

	return result;
}

ImportedMaterialInfo AssimpLoader::BuildImportedMaterial(
	aiMaterial* material,
	const aiScene* scene,
	const std::filesystem::path& modelPath,
	const std::filesystem::path& extractedTextureDir,
	UINT materialIndex) const
{
	ImportedMaterialInfo info;
	info.Name = SanitizeName(SString::UTF8ToWstring(material->GetName().C_Str()));
	if (info.Name.empty())
		info.Name = L"Material_" + std::to_wstring(materialIndex);

	// 优先读取 PBR BaseColor，但在引擎侧统一按 Diffuse 颜色处理。
	aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
	if (material->Get(AI_MATKEY_BASE_COLOR, baseColor) != AI_SUCCESS)
		material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);

	info.DiffuseColor = DirectX::XMFLOAT4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);

	aiColor3D emissiveColor(0.0f, 0.0f, 0.0f);
	if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS)
		info.Emissive = DirectX::XMFLOAT3(emissiveColor.r, emissiveColor.g, emissiveColor.b);

	float metallic = 0.0f;
	if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
		info.Metallic = metallic;

	float roughness = 1.0f;
	if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
		info.Roughness = roughness;

	float opacity = info.DiffuseColor.w;
	if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
		info.Opacity = opacity;
	info.DiffuseColor.w = opacity;

	// 只抽取材质系统当前会使用的贴图槽。
	aiString texturePath;
	if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath) == AI_SUCCESS ||
		material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS)
	{
		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddDebugMessage(
				L"[Import][TexMeta] 材质[%u] Diffuse | %s",
				materialIndex,
				BuildAssimpTextureDebugSummary(texturePath, scene->GetEmbeddedTexture(texturePath.C_Str())).c_str());
		}
		info.DiffuseTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"diffuse");
	}

	if (material->GetTexture(aiTextureType_NORMALS, 0, &texturePath) == AI_SUCCESS ||
		material->GetTexture(aiTextureType_HEIGHT, 0, &texturePath) == AI_SUCCESS)
	{
		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddDebugMessage(
				L"[Import][TexMeta] 材质[%u] Normal | %s",
				materialIndex,
				BuildAssimpTextureDebugSummary(texturePath, scene->GetEmbeddedTexture(texturePath.C_Str())).c_str());
		}
		info.NormalTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"normal");
	}

	if (material->GetTexture(aiTextureType_SPECULAR, 0, &texturePath) == AI_SUCCESS)
	{
		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddDebugMessage(
				L"[Import][TexMeta] 材质[%u] Specular | %s",
				materialIndex,
				BuildAssimpTextureDebugSummary(texturePath, scene->GetEmbeddedTexture(texturePath.C_Str())).c_str());
		}
		info.SpecularTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"specular");
	}

	if (material->GetTexture(aiTextureType_METALNESS, 0, &texturePath) == AI_SUCCESS)
	{
		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddDebugMessage(
				L"[Import][TexMeta] 材质[%u] Metallic | %s",
				materialIndex,
				BuildAssimpTextureDebugSummary(texturePath, scene->GetEmbeddedTexture(texturePath.C_Str())).c_str());
		}
		info.MetallicTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"metallic");
	}

	if (material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texturePath) == AI_SUCCESS)
	{
		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddDebugMessage(
				L"[Import][TexMeta] 材质[%u] Roughness | %s",
				materialIndex,
				BuildAssimpTextureDebugSummary(texturePath, scene->GetEmbeddedTexture(texturePath.C_Str())).c_str());
		}
		info.RoughnessTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"roughness");
	}

	if (material->GetTexture(aiTextureType_EMISSIVE, 0, &texturePath) == AI_SUCCESS)
	{
		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddDebugMessage(
				L"[Import][TexMeta] 材质[%u] Emissive | %s",
				materialIndex,
				BuildAssimpTextureDebugSummary(texturePath, scene->GetEmbeddedTexture(texturePath.C_Str())).c_str());
		}
		info.EmissiveTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"emissive");
	}

	if (material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texturePath) == AI_SUCCESS)
	{
		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddDebugMessage(
				L"[Import][TexMeta] 材质[%u] AO | %s",
				materialIndex,
				BuildAssimpTextureDebugSummary(texturePath, scene->GetEmbeddedTexture(texturePath.C_Str())).c_str());
		}
		info.AmbientOcclusionTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"ao");
	}

	return info;
}

/* ------------------------------------ */

bool AssimpLoader::ConvertModelToWModel(const std::wstring& path, const std::wstring& rootName, std::filesystem::path& outModelFilePath)
{
	outModelFilePath.clear();
	if (path.empty() || !std::filesystem::exists(path))
		return false;

	Assimp::Importer hierarchyImporter;
	hierarchyImporter.SetPropertyInteger(AI_CONFIG_FBX_CONVERT_TO_M, FALSE);

	const aiScene* hierarchyScene = hierarchyImporter.ReadFile(
		SString::WstringToUTF8(path),
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_CalcTangentSpace |
		aiProcess_GenSmoothNormals);

	if (hierarchyScene == nullptr || hierarchyScene->mNumMeshes == 0 || hierarchyScene->mRootNode == nullptr)
		return false;

	const std::filesystem::path modelPath(path);
	const std::wstring importBaseName = SanitizeName(rootName.empty() ? modelPath.stem().wstring() : rootName);
	const std::filesystem::path importedAssetDir =
		std::filesystem::path(EngineUtils::GetProjectDirPath()) /
		L"ImportedAssets" /
		SanitizeName(modelPath.stem().wstring());
	const std::filesystem::path extractedTextureDir = importedAssetDir / L"Textures";
	const std::filesystem::path importedMaterialDir = importedAssetDir / L"Materials";

	if (m_consoleWindow != nullptr)
	{
		m_consoleWindow->AddInfoMessage(L"[Import] 读取模型：%s", modelPath.filename().c_str());
		m_consoleWindow->AddDebugMessage(L"[Import] 模型路径：%s", modelPath.wstring().c_str());
		m_consoleWindow->AddInfoMessage(L"[Import] 对象数量：%u，材质数量：%u",
			hierarchyScene->mNumMeshes, hierarchyScene->mNumMaterials);
	}

	std::vector<std::wstring> modelMaterialIds(hierarchyScene->mNumMaterials);
	std::vector<std::filesystem::path> materialFilePaths(hierarchyScene->mNumMaterials);
	for (UINT materialIndex = 0; materialIndex < hierarchyScene->mNumMaterials; ++materialIndex)
	{
		ImportedMaterialInfo materialInfo = BuildImportedMaterial(
			hierarchyScene->mMaterials[materialIndex],
			hierarchyScene,
			modelPath,
			extractedTextureDir,
			materialIndex);

		if (m_consoleWindow != nullptr)
		{
			std::vector<std::wstring> textureUsages;
			auto pushTextureUsage = [&](const ImportedTextureSource& source, const wchar_t* slotName)
			{
				const std::wstring usage = BuildTextureUsageSummary(source, slotName);
				if (!usage.empty())
					textureUsages.push_back(usage);
			};

			pushTextureUsage(materialInfo.DiffuseTexture, L"Diffuse");
			pushTextureUsage(materialInfo.NormalTexture, L"Normal");
			pushTextureUsage(materialInfo.SpecularTexture, L"Specular");
			pushTextureUsage(materialInfo.MetallicTexture, L"Metallic");
			pushTextureUsage(materialInfo.RoughnessTexture, L"Roughness");
			pushTextureUsage(materialInfo.EmissiveTexture, L"Emissive");
			pushTextureUsage(materialInfo.AmbientOcclusionTexture, L"AO");

			std::wstring textureSummary = L"无贴图";
			if (!textureUsages.empty())
			{
				textureSummary.clear();
				for (size_t textureIndex = 0; textureIndex < textureUsages.size(); ++textureIndex)
				{
					if (textureIndex > 0)
						textureSummary += L", ";
					textureSummary += textureUsages[textureIndex];
				}
			}

			m_consoleWindow->AddInfoMessage(
				L"[Import] 材质编号[%u] 材质名：%s | 贴图摘要：%s",
				materialIndex,
				materialInfo.Name.c_str(),
				textureSummary.c_str());
		}

		const WMaterialFileData materialFileData = WMaterialFile::FromImportedMaterial(materialInfo);
		const std::filesystem::path materialFilePath =
			BuildImportedMaterialFilePath(importedMaterialDir, materialIndex, materialFileData.MaterialName);
		if (!WMaterialFile::SaveToFile(materialFilePath, materialFileData))
		{
			if (m_consoleWindow != nullptr)
				m_consoleWindow->AddWarningMessage(L"[Import] 材质文件保存失败：%s", materialFilePath.wstring().c_str());
			return false;
		}

		materialFilePaths[materialIndex] = materialFilePath;
		modelMaterialIds[materialIndex] = BuildModelMaterialId(materialIndex);
	}

	WModelFileData modelFileData;
	modelFileData.Name = importBaseName;
	modelFileData.SourceFile = modelPath.wstring();

	for (UINT materialIndex = 0; materialIndex < materialFilePaths.size(); ++materialIndex)
	{
		WModelMaterialRef materialRef;
		materialRef.Id = modelMaterialIds[materialIndex];
		materialRef.File = BuildRelativeAssetPath(materialFilePaths[materialIndex], importedAssetDir);
		modelFileData.Materials.push_back(std::move(materialRef));
	}

	std::vector<std::wstring> meshIds(hierarchyScene->mNumMeshes);
	for (UINT meshIndex = 0; meshIndex < hierarchyScene->mNumMeshes; ++meshIndex)
	{
		meshIds[meshIndex] = BuildModelMeshId(meshIndex);

		aiMesh* importedMesh = hierarchyScene->mMeshes[meshIndex];
		Mesh meshData = ProcessRawMesh(hierarchyScene->mRootNode, importedMesh, hierarchyScene);
		if (meshData.vertices.empty() || meshData.indices32.empty())
			continue;

		WModelMeshData modelMeshData;
		modelMeshData.Id = meshIds[meshIndex];
		modelMeshData.Name = ResolveModelMeshName(importedMesh, meshIndex, L"Mesh");
		modelMeshData.Vertices = std::move(meshData.vertices);
		modelMeshData.Indices = std::move(meshData.indices32);
		modelFileData.Meshes.push_back(std::move(modelMeshData));
	}

	UINT modelNodeCounter = 0;
	modelFileData.RootNode = BuildModelHierarchyNode(
		hierarchyScene->mRootNode,
		hierarchyScene,
		meshIds,
		modelMaterialIds,
		modelNodeCounter,
		importBaseName);

	outModelFilePath = importedAssetDir / BuildImportedModelFileName(importBaseName);
	if (!WModelFile::SaveToFile(outModelFilePath, modelFileData))
	{
		if (m_consoleWindow != nullptr)
			m_consoleWindow->AddWarningMessage(L"[Import] .wmodel 文件保存失败：%s", outModelFilePath.wstring().c_str());
		outModelFilePath.clear();
		return false;
	}

	if (m_consoleWindow != nullptr)
	{
		m_consoleWindow->AddInfoMessage(L"[Import] 已生成模型文件：%s", outModelFilePath.filename().c_str());
		m_consoleWindow->AddDebugMessage(L"[Import] 模型文件路径：%s", outModelFilePath.wstring().c_str());
	}

	return true;
}

bool AssimpLoader::ImportModelToScene(const std::wstring& path, WitchcraECS* ecs, const std::wstring& rootName, const Transform& transform)
{
	D3DWindow* dx = m_engine != nullptr ? m_engine->GetD3DWindow() : nullptr;
	if (dx == nullptr || ecs == nullptr || path.empty() || !std::filesystem::exists(path))
		return false;

	// 内部统一走 .wmodel 加载链路：
	// 外部模型先转换成 .wmodel，再用同一套逻辑创建实体/材质/网格。
	if (_wcsicmp(std::filesystem::path(path).extension().c_str(), WModelFile::Extension) == 0)
		return LoadWModelToScene(path, ecs, rootName, transform);

	std::filesystem::path modelFilePath;
	if (!ConvertModelToWModel(path, rootName, modelFilePath))
		return false;

	if (m_consoleWindow != nullptr)
		m_consoleWindow->AddInfoMessage(L"[Import] 转换完成，将改用 .wmodel 流程加载。");

	return LoadWModelToScene(modelFilePath.wstring(), ecs, rootName, transform);
}

bool AssimpLoader::LoadWModelToScene(const std::wstring& path, WitchcraECS* ecs, const std::wstring& rootName, const Transform& transform)
{
	D3DWindow* dx = m_engine != nullptr ? m_engine->GetD3DWindow() : nullptr;
	if (dx == nullptr || ecs == nullptr || path.empty() || !std::filesystem::exists(path))
		return false;

	WModelFileData modelFileData;
	if (!WModelFile::LoadFromFile(path, &modelFileData))
		return false;

	const std::filesystem::path modelPath(path);
	const std::filesystem::path modelDirectory = modelPath.parent_path();
	const std::wstring importBaseName = SanitizeName(rootName.empty() ? (modelFileData.Name.empty() ? modelPath.stem().wstring() : modelFileData.Name) : rootName);

	if (m_consoleWindow != nullptr)
	{
		m_consoleWindow->AddInfoMessage(
			L"[WModel] 开始读取模型：%s",
			modelPath.filename().c_str());
		m_consoleWindow->AddDebugMessage(
			L"[WModel] 模型路径：%s",
			modelPath.wstring().c_str());
		m_consoleWindow->AddInfoMessage(
			L"[WModel] 网格数量：%u，材质引用数量：%u",
			static_cast<UINT>(modelFileData.Meshes.size()),
			static_cast<UINT>(modelFileData.Materials.size()));
	}

	std::unordered_map<std::wstring, const WModelMeshData*> meshLookup;
	for (const WModelMeshData& meshData : modelFileData.Meshes)
	{
		meshLookup[meshData.Id] = &meshData;
		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddInfoMessage(
				L"[WModel] 网格：%s | 顶点数：%u | 索引数：%u",
				meshData.Name.c_str(),
				static_cast<UINT>(meshData.Vertices.size()),
				static_cast<UINT>(meshData.Indices.size()));
		}
	}

	std::unordered_map<std::wstring, std::wstring> MaterialNames;
	for (const WModelMaterialRef& materialRef : modelFileData.Materials)
	{
		// 先读取 .wmat，再转换成运行时材质对象，并记录 “材质引用ID -> 运行时材质名” 映射。
		const std::filesystem::path materialPath = ResolveReferencedPath(modelDirectory, materialRef.File);
		WMaterialFileData materialFileData;
		if (!WMaterialFile::LoadFromFile(materialPath, &materialFileData))
		{
			if (m_consoleWindow != nullptr)
			{
				m_consoleWindow->AddWarningMessage(
					L"[WModel] 无法读取材质：%s",
					materialPath.wstring().c_str());
			}
			continue;
		}

		const ImportedMaterialInfo importedMaterialInfo = ConvertMaterialFileDataToImportedInfo(materialFileData, materialPath);
		const std::wstring MaterialBaseName = importBaseName + L"_" + importedMaterialInfo.Name;
		MaterialNames[materialRef.Id] = dx->CreateMaterialFromImport(MaterialBaseName, importedMaterialInfo);

		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddInfoMessage(
				L"[WModel] 材质引用：%s -> %s",
				materialRef.Id.c_str(),
				materialPath.filename().c_str());
		}
	}

	// 递归恢复 wmodel 中保存的层级结构。
	std::function<SceneEntityBase*(const WModelNodeData&, SceneEntityBase*, bool)> createNodeRecursive =
		[&](const WModelNodeData& nodeData, SceneEntityBase* parentEntity, bool isRootNode) -> SceneEntityBase*
	{
		const std::wstring desiredName = isRootNode
			? importBaseName
			: SanitizeName(nodeData.Name.empty() ? L"Node" : nodeData.Name);
		const std::wstring entityName = ecs->GetUniqueEntityName(desiredName, parentEntity);

		const WModelMeshData* meshData = nullptr;
		if (!nodeData.MeshRef.empty())
		{
			auto meshIt = meshLookup.find(nodeData.MeshRef);
			if (meshIt != meshLookup.end())
				meshData = meshIt->second;
		}

		const std::wstring MeshName = importBaseName + L"_" + nodeData.Id;
		SceneEntityBase* entity = CreateEntityForModelNode(ecs, nodeData, entityName, parentEntity, path, MeshName, meshData);
		if (entity == nullptr)
			return nullptr;

		ecs->SetEntityEditableLocalTransform(entity, nodeData.LocalTransform);

		if (nodeData.Type == WModelNodeType::Mesh && meshData != nullptr)
		{
			if (ecs->ConfigureMeshEntity(
				entity,
				m_engine,
				entityName,
				path,
				MeshName,
				不透明物体渲染项目,
				L""))
			{
				// 顺序很重要：
				// 1. 创建 GPU 网格
				// 2. 绑定材质
				// 3. 最后统一由 ECS -> D3DWindow 汇总渲染项
				ecs->SetupMeshEntity(entity, dx);

				if (!nodeData.MaterialSlots.empty())
				{
					// 当前节点先使用第一个材质槽作为默认材质。
					const auto materialIt = MaterialNames.find(nodeData.MaterialSlots[0].MaterialRef);
					if (materialIt != MaterialNames.end() && !materialIt->second.empty())
						ecs->SetMeshEntityMaterial(entity, materialIt->second);
				}
			}
		}

		if (m_consoleWindow != nullptr)
		{
			std::wstring meshRefSummary;
			if (!nodeData.MeshRef.empty())
				meshRefSummary = L" | MeshRef=" + nodeData.MeshRef;

			m_consoleWindow->AddInfoMessage(
				L"[WModel] 节点：%s | 类型=%s%s",
				entityName.c_str(),
				nodeData.Type == WModelNodeType::Mesh ? L"Mesh" : L"Empty",
				meshRefSummary.c_str());
		}

		for (const WModelNodeData& childNode : nodeData.Children)
			createNodeRecursive(childNode, entity, false);

		return entity;
	};

	WModelNodeData rootNodeData = CollapseLeadingWrapperNodes(modelFileData.RootNode);
	// 导入时传入的 transform 作为根节点附加变换，叠加到最终保留的根节点本地变换上。
	rootNodeData.LocalTransform = CombineTransforms(transform, rootNodeData.LocalTransform);
	SceneEntityBase* importedRoot = createNodeRecursive(rootNodeData, nullptr, true);

	ecs->SelectEntityForHierarchy(importedRoot);
	// 导入完成后只把新导入的根节点子树挂入渲染缓存，
	// 其 world/local transform 仍然由 ECS 同步后统一参与最终矩阵计算。
	if (importedRoot != nullptr)
		dx->AddRenderItemsFromEntity(importedRoot, ecs);

	if (m_consoleWindow != nullptr)
	{
		m_consoleWindow->AddInfoMessage(
			L"[WModel] 导入完成：根实体=%s",
			importedRoot != nullptr ? ecs->GetEntityName(importedRoot).c_str() : L"<null>");
	}

	return importedRoot != nullptr;
}
