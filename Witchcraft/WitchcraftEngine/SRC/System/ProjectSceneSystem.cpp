#include "ProjectSceneSystem.h"

#include "ECS/WitchcraECS.h"
#include "ECS/Component/LightComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ENGINE/EngineUtils.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "String/SStringUtils.h"
#include "System/WitchcraftFile/WModelFile.h"
#include "System/WitchcraftFile/WSceneFile.h"

#include <chrono>
#include <commdlg.h>
#include <cwctype>
#include <DirectXMath.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace
{
	constexpr UINT kSkyRenderLayer = 0;
	const wchar_t* kDefaultSkyGeometryRef = L"shapeGeo";
	const wchar_t* kDefaultSkyTexturePath = L"DATA/HDRIs/scythian_tombs_2_4k.png";

	std::wstring BuildCurrentTimestampText()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
		std::tm localTime{};
		localtime_s(&localTime, &nowTime);

		std::wostringstream stream;
		stream << std::put_time(&localTime, L"%Y-%m-%dT%H:%M:%S");
		return stream.str();
	}

	std::wstring SanitizeSceneStem(std::wstring value)
	{
		if (value.empty())
			value = L"UntitledScene";

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

	std::wstring BuildEntityIdText(const SceneEntityBase* entity)
	{
		if (entity == nullptr)
			return L"";

		return std::to_wstring(static_cast<unsigned long long>(entity->entity));
	}

	const wchar_t* DefaultSceneName()
	{
		return L"UntitledScene";
	}

	std::wstring BuildSceneStateToken(const WSceneFileData& sceneFileData)
	{
		WSceneFileData normalizedData = sceneFileData;
		normalizedData.Meta.UpdatedAt.clear();
		return WSceneFile::SerializeToText(normalizedData);
	}

	WSceneFileData BuildBlankSceneFileData(const std::wstring& sceneName, const std::wstring& createdAt)
	{
		WSceneFileData sceneFileData;
		sceneFileData.Meta.Name = sceneName.empty() ? DefaultSceneName() : sceneName;
		sceneFileData.Meta.CreatedAt = createdAt.empty() ? BuildCurrentTimestampText() : createdAt;
		return sceneFileData;
	}

	bool TrySelectSceneFilePath(HWND ownerWindow, std::filesystem::path* outPath)
	{
		if (outPath == nullptr)
			return false;

		wchar_t fileBuffer[MAX_PATH] = {};

		OPENFILENAME ofn{};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = ownerWindow;
		ofn.lpstrFile = fileBuffer;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrFilter = L"Witchcraft Scene (*.wscene)\0*.wscene\0All Files (*.*)\0*.*\0\0";
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
		ofn.lpstrDefExt = L"wscene";
		ofn.lpstrTitle = L"Open Scene";

		if (!GetOpenFileName(&ofn))
			return false;

		*outPath = std::filesystem::path(fileBuffer);
		return true;
	}

	std::wstring TryExtractModelNodeId(const std::wstring& meshName)
	{
		if (meshName.empty())
			return L"";

		const size_t markerPos = meshName.rfind(L"_node_");
		if (markerPos != std::wstring::npos)
			return meshName.substr(markerPos + 1);

		if (meshName.rfind(L"node_", 0) == 0)
			return meshName;

		return L"";
	}

	std::wstring ToLowerCopy(std::wstring value)
	{
		for (wchar_t& ch : value)
			ch = static_cast<wchar_t>(std::towlower(ch));

		return value;
	}

	std::wstring DetectPrimitiveKindFromModelPath(const std::wstring& modelPath)
	{
		if (modelPath.empty())
			return L"";

		const std::wstring normalizedPath = ToLowerCopy(std::filesystem::path(modelPath).generic_wstring());
		if (normalizedPath.find(L"data/models/") == std::wstring::npos)
			return L"";

		const std::wstring stem = ToLowerCopy(std::filesystem::path(modelPath).stem().wstring());
		if (stem == L"cube")
			return L"Box";
		if (stem == L"sphere")
			return L"Sphere";
		if (stem == L"capsule")
			return L"Capsule";
		if (stem == L"plane")
			return L"Plane";

		return L"";
	}

	std::wstring BuildPrimitiveModelPath(const std::wstring& primitiveKind)
	{
		auto resolveDataModelPath = [](const std::wstring& fileName) -> std::wstring
		{
			if (fileName.empty())
				return L"";

			std::filesystem::path probe = std::filesystem::current_path();
			while (!probe.empty())
			{
				const std::filesystem::path candidate = probe / L"DATA" / L"Models" / fileName;
				if (std::filesystem::exists(candidate))
					return candidate.lexically_normal().wstring();

				const std::filesystem::path parent = probe.parent_path();
				if (parent == probe)
					break;
				probe = parent;
			}

			return (std::filesystem::path(L"DATA") / L"Models" / fileName).wstring();
		};

		const std::wstring normalizedKind = ToLowerCopy(primitiveKind);
		if (normalizedKind == L"box")
			return resolveDataModelPath(L"Cube.obj");
		if (normalizedKind == L"sphere")
			return resolveDataModelPath(L"Sphere.obj");
		if (normalizedKind == L"capsule")
			return resolveDataModelPath(L"Capsule.obj");
		if (normalizedKind == L"plane")
			return resolveDataModelPath(L"Plane.obj");

		return L"";
	}

	std::wstring ResolveRenderSourceType(
		MeshComponent& meshComponent,
		D3DWindow* dx,
		std::wstring* outPrimitiveKind,
		std::wstring* outSkyTexturePath)
	{
		if (outPrimitiveKind != nullptr)
			outPrimitiveKind->clear();
		if (outSkyTexturePath != nullptr)
			outSkyTexturePath->clear();

		const std::wstring primitiveKind = DetectPrimitiveKindFromModelPath(meshComponent.GetFileName());
		const std::wstring skyTexturePath =
			dx != nullptr && !meshComponent.GetDefaultMaterialName().empty()
			? dx->GetSkyTexturePathByRuntimeMaterialName(meshComponent.GetDefaultMaterialName())
			: L"";

		if (outPrimitiveKind != nullptr)
			*outPrimitiveKind = primitiveKind;
		if (outSkyTexturePath != nullptr)
			*outSkyTexturePath = skyTexturePath;

		if (meshComponent.GetRenderLayerIndex() == kSkyRenderLayer || !skyTexturePath.empty())
			return L"Sky";
		if (!primitiveKind.empty())
			return L"PrimitiveBuiltin";
		if (!meshComponent.OwnsGeometry() && !meshComponent.GetGeometryName().empty())
			return L"ExternalGeometry";
		if (!meshComponent.GetFileName().empty())
			return L"ModelFile";
		if (!meshComponent.GetGeometryName().empty())
			return L"ExternalGeometry";

		return L"";
	}

	bool AreVerticesExactlyEqual(const Vertex& lhs, const Vertex& rhs)
	{
		return lhs.Pos.x == rhs.Pos.x && lhs.Pos.y == rhs.Pos.y && lhs.Pos.z == rhs.Pos.z &&
			lhs.Color.x == rhs.Color.x && lhs.Color.y == rhs.Color.y && lhs.Color.z == rhs.Color.z && lhs.Color.w == rhs.Color.w &&
			lhs.Normal.x == rhs.Normal.x && lhs.Normal.y == rhs.Normal.y && lhs.Normal.z == rhs.Normal.z &&
			lhs.TexC.x == rhs.TexC.x && lhs.TexC.y == rhs.TexC.y &&
			lhs.Tangent.x == rhs.Tangent.x && lhs.Tangent.y == rhs.Tangent.y && lhs.Tangent.z == rhs.Tangent.z &&
			lhs.Bitangent.x == rhs.Bitangent.x && lhs.Bitangent.y == rhs.Bitangent.y && lhs.Bitangent.z == rhs.Bitangent.z;
	}

	bool AreVertexArraysExactlyEqual(const std::vector<Vertex>& lhs, const std::vector<Vertex>& rhs)
	{
		if (lhs.size() != rhs.size())
			return false;

		for (size_t index = 0; index < lhs.size(); ++index)
		{
			if (!AreVerticesExactlyEqual(lhs[index], rhs[index]))
				return false;
		}

		return true;
	}

	bool AreIndexArraysExactlyEqual(const std::vector<std::uint32_t>& lhs, const std::vector<std::uint32_t>& rhs)
	{
		if (lhs.size() != rhs.size())
			return false;

		for (size_t index = 0; index < lhs.size(); ++index)
		{
			if (lhs[index] != rhs[index])
				return false;
		}

		return true;
	}

	int FindMatchingRawSubMeshIndex(MeshComponent& meshComponent, const std::vector<Mesh>& rawMeshes)
	{
		const std::vector<Vertex>& componentVertices = meshComponent.GetVertices();
		const std::vector<std::uint32_t>& componentIndices = meshComponent.GetIndices();

		for (size_t meshIndex = 0; meshIndex < rawMeshes.size(); ++meshIndex)
		{
			const Mesh& rawMesh = rawMeshes[meshIndex];
			if (!AreVertexArraysExactlyEqual(componentVertices, rawMesh.vertices))
				continue;
			if (!AreIndexArraysExactlyEqual(componentIndices, rawMesh.indices32))
				continue;

			return static_cast<int>(meshIndex);
		}

		return -1;
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

	const WModelNodeData* FindWModelNodeById(const WModelNodeData& nodeData, const std::wstring& nodeId)
	{
		if (!nodeId.empty() && nodeData.Id == nodeId)
			return &nodeData;

		for (const WModelNodeData& childNode : nodeData.Children)
		{
			if (const WModelNodeData* foundNode = FindWModelNodeById(childNode, nodeId))
				return foundNode;
		}

		return nullptr;
	}

	const WModelNodeData* FindFirstMeshNodeByName(const WModelNodeData& nodeData, const std::wstring& nodeName)
	{
		if (nodeData.Type == WModelNodeType::Mesh && !nodeName.empty() && nodeData.Name == nodeName)
			return &nodeData;

		for (const WModelNodeData& childNode : nodeData.Children)
		{
			if (const WModelNodeData* foundNode = FindFirstMeshNodeByName(childNode, nodeName))
				return foundNode;
		}

		return nullptr;
	}

	struct LoadedWModelCache
	{
		bool Attempted = false;
		bool Loaded = false;
		WModelFileData Data;
		std::unordered_map<std::wstring, const WModelMeshData*> MeshesById;
		std::unordered_map<std::wstring, std::wstring> RuntimeMaterialByRef;
	};

	struct LoadedRawModelCache
	{
		bool Attempted = false;
		std::vector<Mesh> Meshes;
	};

	std::wstring InferRenderSourceType(const WSceneMeshData& meshData)
	{
		if (!meshData.RenderSourceType.empty())
			return meshData.RenderSourceType;
		if (meshData.RenderLayer == kSkyRenderLayer || !meshData.SkyTexturePath.empty())
			return L"Sky";
		if (!meshData.PrimitiveKind.empty())
			return L"PrimitiveBuiltin";
		if (!meshData.GeometryRef.empty() && meshData.ModelRef.empty())
			return L"ExternalGeometry";
		if (!meshData.ModelRef.empty())
			return L"ModelFile";

		return L"";
	}

	std::wstring SerializeSceneLightKind(LightKind kind)
	{
		switch (kind)
		{
		case LightKind::Ambient:
			return L"Ambient";
		case LightKind::Directional:
			return L"Directional";
		case LightKind::Spot:
			return L"Spot";
		case LightKind::Point:
			return L"Point";
		default:
			return L"Directional";
		}
	}

	LightKind DeserializeSceneLightKind(const std::wstring& kindText)
	{
		const std::wstring normalizedKind = ToLowerCopy(kindText);
		if (normalizedKind == L"ambient")
			return LightKind::Ambient;
		if (normalizedKind == L"spot")
			return LightKind::Spot;
		if (normalizedKind == L"point")
			return LightKind::Point;
		return LightKind::Directional;
	}

}

void ProjectSceneSystem::Init(D3DWindow* dx, WitchcraECS* ecs, Engine* engine)
{
	m_dx = dx;
	m_ecs = ecs;
	m_engine = engine;
	ClearScene(DefaultSceneName());
	m_hasCommittedSceneState = false;
	m_committedSceneStateToken.clear();
}

void ProjectSceneSystem::ClearScene(std::wstring _name)
{
	sceneName = _name;
	m_sceneCreatedAt = BuildCurrentTimestampText();
	m_sceneFilePath.clear();
}

std::wstring ProjectSceneSystem::GetSceneNmae()
{
	return sceneName;
}

bool ProjectSceneSystem::NewScene(std::wstring _name)
{
	if (!ConfirmSceneSwitchIfNeeded())
		return false;

	if (m_ecs != nullptr)
		m_ecs->Clear();
	if (m_dx != nullptr && m_ecs != nullptr)
		m_dx->RebuildRenderItemsFromEntities(m_ecs);
	if (m_dx != nullptr)
	{
		const WSceneRenderSettingsData defaultRenderSettings;
		m_dx->SetShadowOpacity(defaultRenderSettings.ShadowOpacity);
		m_dx->SetShadowSoftness(defaultRenderSettings.ShadowSoftness);
	}

	ClearScene(_name);
	CommitCurrentSceneState();
	return true;
}

bool ProjectSceneSystem::SaveScene()
{
	return SaveSceneInternal();
}

bool ProjectSceneSystem::SaveSceneInternal()
{
	if (m_ecs == nullptr)
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> SaveScene failed: ECS is null.");
		return false;
	}

	WSceneFileData sceneFileData;
	if (!BuildSceneFileData(&sceneFileData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> SaveScene failed: could not build scene data.");
		return false;
	}

	const std::filesystem::path sceneFilePath = BuildDefaultSceneFilePath();
	if (!WSceneFile::SaveToFile(sceneFilePath, sceneFileData))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> SaveScene failed: " + sceneFilePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	sceneName = sceneFileData.Meta.Name;
	m_sceneCreatedAt = sceneFileData.Meta.CreatedAt;
	m_sceneFilePath = sceneFilePath;
	CommitSceneStateFromData(sceneFileData);

	const std::wstring logText = L"[ProjectSceneSystem] -> Scene saved: " + sceneFilePath.wstring();
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

void ProjectSceneSystem::OpenProject()
{
	EngineHelpers::OpenFileDialog(m_dx->GethWnd(), L"", L"", L"Open Project");
}

void ProjectSceneSystem::SaveProject()
{
}

bool ProjectSceneSystem::OpenScene()
{
	if (!ConfirmSceneSwitchIfNeeded())
		return false;

	std::filesystem::path sceneFilePath;
	if (!TrySelectSceneFilePath(m_dx != nullptr ? m_dx->GethWnd() : nullptr, &sceneFilePath))
		return false;

	if (!LoadSceneFileData(sceneFilePath))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> OpenScene failed: " + sceneFilePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	return true;
}

bool ProjectSceneSystem::ConfirmSceneSwitchIfNeeded()
{
	if (!IsCurrentSceneDirty())
		return true;

	const HWND ownerWindow = m_dx != nullptr ? m_dx->GethWnd() : nullptr;
	const int dialogResult = MessageBoxW(
		ownerWindow,
		L"当前场景有未保存修改，是否先保存当前场景？",
		L"场景未保存",
		MB_ICONQUESTION | MB_YESNOCANCEL);

	if (dialogResult == IDYES)
		return SaveSceneInternal();
	if (dialogResult == IDNO)
		return true;

	return false;
}

bool ProjectSceneSystem::IsCurrentSceneDirty() const
{
	std::wstring currentSceneStateToken;
	if (!BuildCurrentSceneStateToken(&currentSceneStateToken))
		return false;

	if (m_hasCommittedSceneState)
		return currentSceneStateToken != m_committedSceneStateToken;

	const WSceneFileData blankSceneData = BuildBlankSceneFileData(sceneName, m_sceneCreatedAt);
	return currentSceneStateToken != BuildSceneStateToken(blankSceneData);
}

bool ProjectSceneSystem::BuildCurrentSceneStateToken(std::wstring* outToken) const
{
	if (outToken == nullptr)
		return false;

	WSceneFileData sceneFileData;
	if (!BuildSceneFileData(&sceneFileData))
		return false;

	*outToken = BuildSceneStateToken(sceneFileData);
	return true;
}

void ProjectSceneSystem::CommitSceneStateFromData(const WSceneFileData& sceneFileData)
{
	m_committedSceneStateToken = BuildSceneStateToken(sceneFileData);
	m_hasCommittedSceneState = true;
}

void ProjectSceneSystem::CommitCurrentSceneState()
{
	std::wstring currentSceneStateToken;
	if (!BuildCurrentSceneStateToken(&currentSceneStateToken))
	{
		m_hasCommittedSceneState = false;
		m_committedSceneStateToken.clear();
		return;
	}

	m_committedSceneStateToken = std::move(currentSceneStateToken);
	m_hasCommittedSceneState = true;
}

bool ProjectSceneSystem::BuildSceneFileData(WSceneFileData* outData) const
{
	if (outData == nullptr || m_ecs == nullptr)
		return false;

	WSceneFileData sceneFileData;
	sceneFileData.Meta.Name = sceneName.empty() ? DefaultSceneName() : sceneName;
	sceneFileData.Meta.CreatedAt = m_sceneCreatedAt.empty() ? BuildCurrentTimestampText() : m_sceneCreatedAt;
	sceneFileData.Meta.UpdatedAt = BuildCurrentTimestampText();
	if (m_dx != nullptr)
	{
		sceneFileData.RenderSettings.ShadowOpacity = m_dx->GetShadowOpacity();
		sceneFileData.RenderSettings.ShadowSoftness = m_dx->GetShadowSoftness();
	}

	std::unordered_map<std::wstring, std::wstring> modelIdByPath;
	std::uint32_t nextModelIndex = 1;

	for (SceneEntityBase* rootEntity : m_ecs->GetSceneRootEntities())
		AppendSceneEntityData(rootEntity, nullptr, &sceneFileData, &modelIdByPath, &nextModelIndex);

	*outData = std::move(sceneFileData);
	return true;
}

void ProjectSceneSystem::AppendSceneEntityData(
	SceneEntityBase* entity,
	SceneEntityBase* parent,
	WSceneFileData* outData,
	std::unordered_map<std::wstring, std::wstring>* modelIdByPath,
	std::uint32_t* nextModelIndex) const
{
	if (entity == nullptr || outData == nullptr || modelIdByPath == nullptr || nextModelIndex == nullptr || m_ecs == nullptr)
		return;

	WSceneEntityData entityData;
	entityData.Id = BuildEntityIdText(entity);
	entityData.Name = m_ecs->GetEntityName(entity);
	entityData.Active = m_ecs->IsEntityVisible(entity);
	entityData.ParentId = BuildEntityIdText(parent);
	m_ecs->GetEntityEditableLocalTransform(entity, &entityData.LocalTransform);

	EntityLightComponentData lightSnapshot;
	if (m_ecs->GetEntityLightSnapshot(entity, &lightSnapshot))
	{
		entityData.HasLight = true;
		entityData.Light.Kind = SerializeSceneLightKind(static_cast<LightKind>(lightSnapshot.kind));
		entityData.Light.Type = lightSnapshot.type;
		entityData.Light.Color = lightSnapshot.color;
		entityData.Light.Power = lightSnapshot.power;
		entityData.Light.CastShadow = static_cast<LightKind>(lightSnapshot.kind) != LightKind::Ambient && lightSnapshot.castShadow;
	}

	if (MeshComponent* meshComponent = m_ecs->GetComponent<MeshComponent>(entity))
	{
		WSceneMeshData meshData;
		meshData.RenderLayer = meshComponent->GetRenderLayerIndex();
		meshData.MeshName = meshComponent->GetMeshName();
		meshData.NodeId = TryExtractModelNodeId(meshData.MeshName);
		meshData.GeometryRef = meshComponent->GetGeometryName();
		meshData.MaterialName = meshComponent->GetDefaultMaterialName();
		if (m_dx != nullptr && !meshData.MaterialName.empty())
		{
			meshData.MaterialFile = m_dx->GetMaterialFilePathByRuntimeMaterialName(meshData.MaterialName);
			meshData.SkyTexturePath = m_dx->GetSkyTexturePathByRuntimeMaterialName(meshData.MaterialName);
		}

		meshData.RenderSourceType = ResolveRenderSourceType(
			*meshComponent,
			m_dx,
			&meshData.PrimitiveKind,
			&meshData.SkyTexturePath);

		const std::wstring modelPath = meshComponent->GetFileName();
		if (!modelPath.empty())
		{
			auto modelIdIt = modelIdByPath->find(modelPath);
			if (modelIdIt == modelIdByPath->end())
			{
				const std::wstring modelId = L"model_" + std::to_wstring((*nextModelIndex)++);
				modelIdIt = modelIdByPath->emplace(modelPath, modelId).first;

				WSceneModelAssetData modelAssetData;
				modelAssetData.Id = modelId;
				modelAssetData.Path = modelPath;
				outData->Models.push_back(std::move(modelAssetData));
			}

			meshData.ModelRef = modelIdIt->second;
		}

		if ((meshData.RenderSourceType == L"ModelFile" || meshData.RenderSourceType == L"PrimitiveBuiltin") &&
			!modelPath.empty() &&
			_wcsicmp(std::filesystem::path(modelPath).extension().c_str(), WModelFile::Extension) != 0)
		{
			AssimpLoader rawModelLoader;
			const std::vector<Mesh> rawMeshes = rawModelLoader.LoadRawModel(modelPath);
			meshData.RawSubMeshIndex = FindMatchingRawSubMeshIndex(*meshComponent, rawMeshes);
		}

		if (!meshData.ModelRef.empty() || !meshData.MeshName.empty() || !meshData.MaterialName.empty())
		{
			entityData.HasMesh = true;
			entityData.Mesh = std::move(meshData);
		}
	}

	outData->Entities.push_back(std::move(entityData));

	for (SceneEntityBase* childEntity : m_ecs->GetSceneChildren(entity))
		AppendSceneEntityData(childEntity, entity, outData, modelIdByPath, nextModelIndex);
}

bool ProjectSceneSystem::LoadSceneFileData(const std::filesystem::path& path)
{
	WSceneFileData sceneFileData;
	if (!WSceneFile::LoadFromFile(path, &sceneFileData))
		return false;

	if (!ApplySceneFileData(sceneFileData))
		return false;

	m_sceneFilePath = path;
	CommitSceneStateFromData(sceneFileData);
	return true;
}

bool ProjectSceneSystem::ApplySceneFileData(const WSceneFileData& sceneFileData)
{
	if (m_ecs == nullptr)
		return false;

	m_ecs->Clear();

	sceneName = sceneFileData.Meta.Name.empty() ? DefaultSceneName() : sceneFileData.Meta.Name;
	m_sceneCreatedAt = sceneFileData.Meta.CreatedAt;
	if (m_dx != nullptr)
	{
		m_dx->SetShadowOpacity(sceneFileData.RenderSettings.ShadowOpacity);
		m_dx->SetShadowSoftness(sceneFileData.RenderSettings.ShadowSoftness);
	}

	std::unordered_map<std::wstring, std::wstring> modelPathById;
	for (const WSceneModelAssetData& modelData : sceneFileData.Models)
	{
		if (!modelData.Id.empty())
			modelPathById[modelData.Id] = modelData.Path;
	}

	std::vector<std::pair<SceneEntityBase*, const WSceneEntityData*>> meshEntitiesToRestore;
	std::unordered_map<std::wstring, SceneEntityBase*> createdEntities;
	for (const WSceneEntityData& entityData : sceneFileData.Entities)
	{
		SceneEntityBase* parentEntity = nullptr;
		if (!entityData.ParentId.empty())
		{
			auto parentIt = createdEntities.find(entityData.ParentId);
			if (parentIt != createdEntities.end())
				parentEntity = parentIt->second;
		}

		const std::wstring entityName = entityData.Name.empty() ? L"Entity" : entityData.Name;
		SceneEntityBase* entity = nullptr;
		if (entityData.HasMesh)
			entity = m_ecs->CreateMeshEntity(entityName, parentEntity);
		else if (entityData.HasLight)
			entity = m_ecs->CreateLightEntity(entityName, parentEntity);
		else
			entity = m_ecs->CreateBasicEntity(entityName, parentEntity, ComponentType::Co_Unk);
		if (entity == nullptr)
			continue;

		m_ecs->SetEntityEditableLocalTransform(entity, entityData.LocalTransform);
		m_ecs->SetEntityVisible(entity, entityData.Active);

		if (entityData.HasLight)
		{
			EntityLightComponentData lightSnapshot;
			lightSnapshot.kind = static_cast<std::uint32_t>(DeserializeSceneLightKind(entityData.Light.Kind));
			lightSnapshot.type = entityData.Light.Type;
			lightSnapshot.color = entityData.Light.Color;
			lightSnapshot.power = entityData.Light.Power;
			lightSnapshot.castShadow = static_cast<LightKind>(lightSnapshot.kind) != LightKind::Ambient && entityData.Light.CastShadow;
			m_ecs->SetEntityLightSnapshot(entity, lightSnapshot);
		}

		if (entityData.HasMesh)
		{
			if (MeshComponent* meshComponent = m_ecs->GetComponent<MeshComponent>(entity))
			{
				meshComponent->SetEngine(m_engine);
				meshComponent->SetRenderLayerIndex(entityData.Mesh.RenderLayer);
				auto modelPathIt = modelPathById.find(entityData.Mesh.ModelRef);
				if (modelPathIt != modelPathById.end())
					meshComponent->SetFileName(modelPathIt->second);
				meshComponent->SetMeshName(entityData.Mesh.MeshName);
				if (!entityData.Mesh.GeometryRef.empty())
					meshComponent->SetGeometryName(entityData.Mesh.GeometryRef);
				meshComponent->SetDefaultMaterialName(entityData.Mesh.MaterialName);
			}

			meshEntitiesToRestore.push_back({ entity, &entityData });
		}

		if (!entityData.Id.empty())
			createdEntities[entityData.Id] = entity;
	}

	std::unordered_map<std::wstring, LoadedWModelCache> wmodelCacheByPath;
	std::unordered_map<std::wstring, LoadedRawModelCache> rawModelCacheByPath;
	for (const auto& meshRestoreItem : meshEntitiesToRestore)
	{
		SceneEntityBase* entity = meshRestoreItem.first;
		const WSceneEntityData& entityData = *meshRestoreItem.second;
		MeshComponent* meshComponent = m_ecs->GetComponent<MeshComponent>(entity);
		if (entity == nullptr || meshComponent == nullptr)
			continue;

		const std::wstring renderSourceType = InferRenderSourceType(entityData.Mesh);
		const UINT renderLayer = entityData.Mesh.RenderLayer;

		std::wstring modelPath;
		auto modelPathIt = modelPathById.find(entityData.Mesh.ModelRef);
		if (modelPathIt != modelPathById.end())
			modelPath = modelPathIt->second;

		if (renderSourceType == L"PrimitiveBuiltin" && modelPath.empty())
			modelPath = BuildPrimitiveModelPath(entityData.Mesh.PrimitiveKind);

		std::wstring resolvedMaterialName = entityData.Mesh.MaterialName;
		if (renderSourceType == L"Sky" && m_dx != nullptr)
		{
			const std::wstring skyTexturePath = entityData.Mesh.SkyTexturePath.empty()
				? std::wstring(kDefaultSkyTexturePath)
				: entityData.Mesh.SkyTexturePath;
			const std::wstring runtimeSkyMaterialName = m_dx->GetOrCreateSkyMaterial(skyTexturePath);
			if (!runtimeSkyMaterialName.empty())
				resolvedMaterialName = runtimeSkyMaterialName;
		}

		if (m_dx != nullptr && !entityData.Mesh.MaterialFile.empty())
		{
			const std::filesystem::path materialFilePath(entityData.Mesh.MaterialFile);
			if (std::filesystem::exists(materialFilePath))
			{
				const std::wstring runtimeMaterialName = m_dx->GetOrCreateMaterialFromWMaterialFile(materialFilePath);
				if (!runtimeMaterialName.empty())
					resolvedMaterialName = runtimeMaterialName;
			}
		}

		bool restoredGeometry = false;
		if ((renderSourceType == L"Sky" || renderSourceType == L"ExternalGeometry") && m_dx != nullptr)
		{
			const std::wstring geometryRef = entityData.Mesh.GeometryRef.empty()
				? (renderSourceType == L"Sky" ? std::wstring(kDefaultSkyGeometryRef) : std::wstring())
				: entityData.Mesh.GeometryRef;
			AggregateGraphicObj* aggregateGraphicObj = m_dx->GetAggregateGraphicObj(geometryRef);
			if (aggregateGraphicObj != nullptr)
			{
				m_ecs->ConfigureMeshEntity(
					entity,
					m_engine,
					entityData.Name.empty() ? L"Entity" : entityData.Name,
					L"",
					entityData.Mesh.MeshName,
					renderSourceType == L"Sky" ? kSkyRenderLayer : renderLayer,
					resolvedMaterialName);
				if (m_ecs->SetMeshEntityExternalGeometry(entity, geometryRef, aggregateGraphicObj))
					restoredGeometry = true;
			}
		}
		else if ((renderSourceType == L"PrimitiveBuiltin" || renderSourceType == L"ModelFile") && !modelPath.empty())
		{
			const std::filesystem::path modelFilePath(modelPath);
			if (!std::filesystem::exists(modelFilePath))
			{
				restoredGeometry = false;
			}
			else if (_wcsicmp(modelFilePath.extension().c_str(), WModelFile::Extension) == 0)
			{
				LoadedWModelCache& wmodelCache = wmodelCacheByPath[modelPath];
				if (!wmodelCache.Attempted)
				{
					wmodelCache.Attempted = true;
					wmodelCache.Loaded = WModelFile::LoadFromFile(modelFilePath, &wmodelCache.Data);
					if (wmodelCache.Loaded)
					{
						for (const WModelMeshData& meshData : wmodelCache.Data.Meshes)
						{
							if (!meshData.Id.empty())
								wmodelCache.MeshesById[meshData.Id] = &meshData;
						}

						if (m_dx != nullptr)
						{
							const std::filesystem::path modelDirectory = modelFilePath.parent_path();
							for (const WModelMaterialRef& materialRef : wmodelCache.Data.Materials)
							{
								const std::filesystem::path materialPath = ResolveReferencedPath(modelDirectory, materialRef.File);
								if (!std::filesystem::exists(materialPath))
									continue;

								const std::wstring runtimeMaterialName = m_dx->GetOrCreateMaterialFromWMaterialFile(materialPath);
								if (!runtimeMaterialName.empty())
									wmodelCache.RuntimeMaterialByRef[materialRef.Id] = runtimeMaterialName;
							}
						}
					}
				}

				if (wmodelCache.Loaded)
				{
					const std::wstring nodeId = entityData.Mesh.NodeId.empty()
						? TryExtractModelNodeId(entityData.Mesh.MeshName)
						: entityData.Mesh.NodeId;

					const WModelNodeData* nodeData = FindWModelNodeById(wmodelCache.Data.RootNode, nodeId);
					if (nodeData == nullptr)
						nodeData = FindFirstMeshNodeByName(wmodelCache.Data.RootNode, entityData.Name);

					if (nodeData != nullptr && !nodeData->MeshRef.empty())
					{
						const auto meshDataIt = wmodelCache.MeshesById.find(nodeData->MeshRef);
						if (meshDataIt != wmodelCache.MeshesById.end() && meshDataIt->second != nullptr)
						{
							if ((resolvedMaterialName.empty() || (m_dx != nullptr && m_dx->GetMaterialByRuntimeMaterialName(resolvedMaterialName) == nullptr))
								&& !nodeData->MaterialSlots.empty())
							{
								const auto materialIt = wmodelCache.RuntimeMaterialByRef.find(nodeData->MaterialSlots.front().MaterialRef);
								if (materialIt != wmodelCache.RuntimeMaterialByRef.end())
									resolvedMaterialName = materialIt->second;
							}

							m_ecs->ConfigureMeshEntity(
								entity,
								m_engine,
								entityData.Name.empty() ? L"Entity" : entityData.Name,
								modelPath,
								entityData.Mesh.MeshName,
								renderLayer,
								resolvedMaterialName);
							m_ecs->AppendMeshEntityVertices(entity, meshDataIt->second->Vertices);
							m_ecs->AppendMeshEntityIndices(entity, meshDataIt->second->Indices);
							if (m_dx != nullptr)
								restoredGeometry = m_ecs->SetupMeshEntity(entity, m_dx);
						}
					}
				}
			}
			else
			{
				LoadedRawModelCache& rawModelCache = rawModelCacheByPath[modelPath];
				if (!rawModelCache.Attempted)
				{
					rawModelCache.Attempted = true;
					AssimpLoader rawModelLoader;
					rawModelCache.Meshes = rawModelLoader.LoadRawModel(modelPath);
				}

				const std::vector<Mesh>& rawMeshes = rawModelCache.Meshes;
				int rawSubMeshIndex = entityData.Mesh.RawSubMeshIndex;
				if (rawSubMeshIndex < 0 && rawMeshes.size() == 1)
					rawSubMeshIndex = 0;

				if (rawSubMeshIndex >= 0 && static_cast<size_t>(rawSubMeshIndex) < rawMeshes.size())
				{
					const Mesh& rawMesh = rawMeshes[rawSubMeshIndex];
					m_ecs->ConfigureMeshEntity(
						entity,
						m_engine,
						entityData.Name.empty() ? L"Entity" : entityData.Name,
						modelPath,
						entityData.Mesh.MeshName,
						renderLayer,
						resolvedMaterialName);
					m_ecs->AppendMeshEntityVertices(entity, rawMesh.vertices);
					m_ecs->AppendMeshEntityIndices(entity, rawMesh.indices32);
					if (m_dx != nullptr)
						restoredGeometry = m_ecs->SetupMeshEntity(entity, m_dx);
				}
			}
		}

		if (!restoredGeometry)
		{
			meshComponent->SetFileName(modelPath);
			meshComponent->SetMeshName(entityData.Mesh.MeshName);
			meshComponent->SetRenderLayerIndex(renderLayer);
			meshComponent->SetDefaultMaterialName(resolvedMaterialName);
		}
	}

	if (m_dx != nullptr)
		m_dx->RebuildRenderItemsFromEntities(m_ecs);

	return true;
}

std::filesystem::path ProjectSceneSystem::BuildDefaultSceneFilePath() const
{
	if (!m_sceneFilePath.empty())
		return m_sceneFilePath;

	const std::wstring sceneStem = SanitizeSceneStem(sceneName);
	return std::filesystem::path(L"DATA") / L"Scenes" / (sceneStem + WSceneFile::Extension);
}
