#include "ProjectSceneSystem.h"

#include "ECS/WitchcraECS.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/LightComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ENGINE/EngineUtils.h"
#include "Helpers/Helpers.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "String/SStringUtils.h"
#include "System/WitchcraftFile/WModelFile.h"
#include "System/WitchcraftFile/WModelRuntimeHelpers.h"
#include "System/WitchcraftFile/WSceneFile.h"
#include "System/WitchcraftFile/WSkeletonFile.h"

#include "ECS/Component/SkeletonComponent.h"
#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/SkinnedMeshComponent.h"
#include "ECS/Component/SkinningRuntimeComponent.h"
#include <DirectXMath.h>
#include <set>

namespace ProjectSceneSystemDetail
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

	std::filesystem::path ResolveProjectAssetPath(const std::filesystem::path& path)
	{
		if (path.empty())
			return {};
		if (path.is_absolute())
			return path.lexically_normal();

		return (std::filesystem::path(EngineUtils::GetProjectDirPath()) / path).lexically_normal();
	}

	std::wstring BuildRelativeAssetPath(const std::filesystem::path& path, const std::filesystem::path& baseDirectory)
	{
		if (path.empty())
			return L"";

		std::error_code relativeError;
		const std::filesystem::path relativePath = std::filesystem::relative(path, baseDirectory, relativeError);
		if (!relativeError && !relativePath.empty())
			return relativePath.lexically_normal().wstring();

		return path.lexically_normal().wstring();
	}

	void CollectReferencedWModelPathsRecursive(
		WitchcraECS* ecs,
		SceneEntityBase* entity,
		std::set<std::wstring>* outModelPaths)
	{
		if (ecs == nullptr || entity == nullptr || outModelPaths == nullptr)
			return;

		if (MeshComponent* meshComponent = ecs->GetComponent<MeshComponent>(entity))
		{
			const std::wstring fileName = meshComponent->GetFileName();
			if (!fileName.empty())
			{
				const std::filesystem::path resolvedPath = ResolveProjectAssetPath(fileName);
				if (_wcsicmp(resolvedPath.extension().c_str(), WModelFile::Extension) == 0)
					outModelPaths->insert(resolvedPath.wstring());
			}
		}

		for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
			CollectReferencedWModelPathsRecursive(ecs, childEntity, outModelPaths);
	}

	SceneEntityBase* ResolveSkeletonOwnerEntityForSave(WitchcraECS* ecs, SceneEntityBase* entity)
	{
		if (ecs == nullptr || entity == nullptr || !ecs->HasEntity(entity))
			return nullptr;

		if (ecs->GetSkeletonData(entity) != nullptr)
			return entity;

		SceneEntityBase* ownerEntity = nullptr;
		if (ecs->TryGetSkeletonHierarchyBinding(entity, &ownerEntity, nullptr, nullptr) &&
			ownerEntity != nullptr &&
			ecs->GetSkeletonData(ownerEntity) != nullptr)
		{
			return ownerEntity;
		}

		for (SceneEntityBase* current = ecs->GetParentEntity(entity);
			current != nullptr;
			current = ecs->GetParentEntity(current))
		{
			if (ecs->GetSkeletonData(current) != nullptr)
				return current;
		}

		return entity;
	}

	// 生成“场景状态令牌”：
	// - 先把当前场景整理成 WSceneFileData
	// - 再序列化为稳定文本
	// - 去掉 UpdatedAt 这类天然会变化但不代表用户内容改动的字段
	//
	// ProjectSceneSystem 目前用“全量状态快照比较”来判定是否 dirty，
	// 而不是依赖各编辑操作手动打 bool 标记。
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
		sceneFileData.Environment.HasAmbientLight = true;
		sceneFileData.Environment.AmbientLightActive = true;
		sceneFileData.Environment.AmbientLight.Kind = L"Ambient";
		sceneFileData.Environment.AmbientLight.Type = 0.0f;
		sceneFileData.Environment.AmbientLight.Color = { 0.45f, 0.45f, 0.45f };
		sceneFileData.Environment.AmbientLight.Power = 0.35f;
		sceneFileData.Environment.AmbientLight.CastShadow = false;
		const auto defaultTypeColors = WitchcraECS::BuildDefaultSceneEntityTypeColors();
		for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
		{
			const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
			WSceneEntityTypeColorData typeColorData;
			typeColorData.Type = SceneEntityTypeToKey(sceneType);
			typeColorData.Color = defaultTypeColors[typeIndex];
			sceneFileData.EntityTypeColors.push_back(std::move(typeColorData));
		}
		return sceneFileData;
	}

	SceneEntityType InferSceneEntityTypeFromEntityData(const WSceneEntityData& entityData)
	{
		SceneEntityType parsedType = SceneEntityType::StaticScenery;
		if (TryParseSceneEntityType(entityData.EntityType, &parsedType))
			return parsedType;

		if (!entityData.HasMesh)
			return SceneEntityType::Interactive;

		std::wstring renderSourceType = entityData.Mesh.RenderSourceType;
		if (renderSourceType.empty())
		{
			if (entityData.Mesh.RenderLayer == kSkyRenderLayer || !entityData.Mesh.SkyTexturePath.empty())
				renderSourceType = L"Sky";
			else if (!entityData.Mesh.PrimitiveKind.empty())
				renderSourceType = L"PrimitiveBuiltin";
			else if (!entityData.Mesh.ModelRef.empty())
				renderSourceType = L"ModelFile";
		}
		if (renderSourceType == L"Sky")
			return SceneEntityType::Sky;

		std::wstring primitiveKind = entityData.Mesh.PrimitiveKind;
		std::transform(primitiveKind.begin(), primitiveKind.end(), primitiveKind.begin(),
			[](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
		if (primitiveKind == L"plane")
			return SceneEntityType::Ground;

		return SceneEntityType::StaticScenery;
	}

	bool TrySelectSceneFilePath(HWND ownerWindow, std::filesystem::path* outPath)
	{
		if (outPath == nullptr)
			return false;

		std::wstring selectedPath;
		if (!EngineHelpers::TryOpenFileDialog(
			ownerWindow,
			L"",
			L"Witchcraft Scene (*.wscene)\0*.wscene\0All Files (*.*)\0*.*\0\0",
			L"Open Scene",
			&selectedPath))
			return false;

		*outPath = std::filesystem::path(selectedPath);
		return true;
	}

	std::wstring TryExtractModelNodeId(const std::wstring& meshName)
	{
		if (meshName.empty())
			return L"";

	const size_t markerPos = meshName.rfind(L"_node_");
	if (markerPos != std::wstring::npos)
	{
		const size_t nodeIdStart = markerPos + 1;
		const size_t instanceMarkerPos = meshName.find(L"_entity_", nodeIdStart);
		return instanceMarkerPos == std::wstring::npos
			? meshName.substr(nodeIdStart)
			: meshName.substr(nodeIdStart, instanceMarkerPos - nodeIdStart);
	}

	if (meshName.rfind(L"node_", 0) == 0)
	{
		const size_t instanceMarkerPos = meshName.find(L"_entity_");
		return instanceMarkerPos == std::wstring::npos
			? meshName
			: meshName.substr(0, instanceMarkerPos);
	}

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
			? dx->GetSkyTexturePathByMaterialName(meshComponent.GetDefaultMaterialName())
			: L"";

		if (outPrimitiveKind != nullptr)
			*outPrimitiveKind = primitiveKind;
		if (outSkyTexturePath != nullptr)
			*outSkyTexturePath = skyTexturePath;

		if (meshComponent.GetRenderLayerIndex() == kSkyRenderLayer || !skyTexturePath.empty())
			return L"Sky";
	if (!primitiveKind.empty())
		return L"PrimitiveBuiltin";
	// 导入的 .wmodel 蒙皮网格会暂时引用运行时创建的外置几何，但该几何
	// 在重开场景前已经释放。只要存在模型文件，就必须保存为 ModelFile，
	// 以便场景加载时回读模型资产，而不是引用失效的 GPU geometry 名称。
	if (!meshComponent.GetFileName().empty())
		return L"ModelFile";
	if (!meshComponent.OwnsGeometry() && !meshComponent.GetGeometryName().empty())
		return L"ExternalGeometry";
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

	SceneEntityBase* FindLoadedSkeletonOwnerForMesh(WitchcraECS* ecs, SceneEntityBase* entity)
	{
		if (ecs == nullptr)
			return nullptr;

		for (SceneEntityBase* current = entity;
			current != nullptr;
			current = ecs->GetParentEntity(current))
		{
			if (ecs->GetComponent<SkeletonComponent>(current) != nullptr)
				return current;
		}

		return nullptr;
	}

	bool IsSerializedSkeletonHelperEntity(WitchcraECS* ecs, SceneEntityBase* entity)
	{
		if (ecs == nullptr || entity == nullptr)
			return false;
		if (ecs->IsSkeletonHierarchyEntity(entity))
			return true;

		auto subtreeContainsMesh = [ecs](SceneEntityBase* rootEntity)
		{
			if (rootEntity == nullptr)
				return false;
			std::vector<SceneEntityBase*> pendingEntities{ rootEntity };
			while (!pendingEntities.empty())
			{
				SceneEntityBase* currentEntity = pendingEntities.back();
				pendingEntities.pop_back();
				if (ecs->GetComponent<MeshComponent>(currentEntity) != nullptr)
					return true;
				const std::vector<SceneEntityBase*>& children = ecs->GetHierarchyChildren(currentEntity);
				pendingEntities.insert(pendingEntities.end(), children.begin(), children.end());
			}
			return false;
		};
		for (SceneEntityBase* current = entity; current != nullptr; current = ecs->GetParentEntity(current))
		{
			SceneEntityBase* ownerEntity = ecs->GetParentEntity(current);
			if (ownerEntity != nullptr && ecs->GetComponent<SkeletonComponent>(ownerEntity) != nullptr && !subtreeContainsMesh(current))
				return true;
		}

		// 排除骨架绑定之前保存的场景的兼容性：
		// 生成的“骨骼”及其所有后代被序列化为普通实体。
		// 它们在当前会话中仍然可用，但不能写入下一个场景快照。
		for (SceneEntityBase* current = entity;
			current != nullptr;
			current = ecs->GetParentEntity(current))
		{
			if (ecs->GetEntityName(current) != L"骨骼")
				continue;

			SceneEntityBase* ownerEntity = ecs->GetParentEntity(current);
			if (ownerEntity != nullptr && ecs->GetComponent<SkeletonComponent>(ownerEntity) != nullptr)
				return true;
		}

		return false;
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
		Witchcraft::WModelRuntime::WModelRuntimeAsset Asset;
		std::unordered_map<std::wstring, std::wstring> MaterialByRef;
		std::set<SceneEntityBase*> InitializedSkeletonOwners;
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

	// LightKind 与 shader type 历史上可能出现不一致（旧场景遗留数据）。
	// 这里统一收敛映射，加载时做一次规范化，避免点光被当成聚光等问题。
	float ResolveSceneLightTypeFromKind(LightKind kind, float fallbackType)
	{
		switch (kind)
		{
		case LightKind::Directional:
			return 0.0f;
		case LightKind::Point:
			return 1.0f;
		case LightKind::Spot:
			return 2.0f;
		case LightKind::Ambient:
			return 0.0f;
		default:
			return fallbackType;
		}
	}

	LightKind ResolveSceneLightKindFromType(float type, LightKind fallbackKind)
	{
		const int roundedType = static_cast<int>(type >= 0.0f ? type + 0.5f : type - 0.5f);
		if (roundedType == 0)
			return LightKind::Directional;
		if (roundedType == 1)
			return LightKind::Point;
		if (roundedType == 2)
			return LightKind::Spot;
		return fallbackKind;
	}

	LightKind ResolveNormalizedSceneLightKind(const WSceneLightData& lightData)
	{
		const LightKind kindFromText = DeserializeSceneLightKind(lightData.Kind);
		if (kindFromText == LightKind::Ambient)
			return LightKind::Ambient;

		// 优先兼容历史 type 值，修正 kind/type 不一致导致的运行时灯型错误。
		return ResolveSceneLightKindFromType(lightData.Type, kindFromText);
	}

}

using namespace ProjectSceneSystemDetail;

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

	// 切场景前先等待 GPU 完成上一轮绘制。
	// 否则旧场景的网格/材质资源可能仍被工作线程命令列表引用，
	// 随后清空 ECS 时释放资源会触发 OBJECT_DELETED_WHILE_STILL_IN_USE。
	if (m_dx != nullptr)
		m_dx->FlushCommandQueue();
	if (m_dx != nullptr)
		m_dx->ResetSceneRuntimeRenderState();

	if (m_ecs != nullptr)
		m_ecs->Clear();
	if (m_ecs != nullptr)
		m_ecs->EnsureEnvironmentEntity();
	if (m_ecs != nullptr)
		m_ecs->EnsureDefaultAmbientLightEntity();
	if (m_ecs != nullptr)
		m_ecs->ResetEntitySceneTypeVertexColorsToDefault(false);
	if (m_dx != nullptr && m_ecs != nullptr)
		m_dx->RebuildRenderItemsFromEntities(m_ecs);
	if (m_dx != nullptr)
	{
		const WSceneRenderSettingsData defaultRenderSettings;
		m_dx->SetShadowOpacity(defaultRenderSettings.ShadowOpacity);
		m_dx->SetShadowSoftness(defaultRenderSettings.ShadowSoftness);
		m_dx->SetAOEnabled(defaultRenderSettings.AOEnabled);
		m_dx->SetAOStrength(defaultRenderSettings.AOStrength);
		m_dx->SetAORadius(defaultRenderSettings.AORadius);
		m_dx->SetAOFadeStart(defaultRenderSettings.AOFadeStart);
		m_dx->SetAOFadeEnd(defaultRenderSettings.AOFadeEnd);
		m_dx->SetAOSurfaceEpsilon(defaultRenderSettings.AOSurfaceEpsilon);
		m_dx->SetAOBlurSigma(defaultRenderSettings.AOBlurSigma);
		m_dx->SetFXAAEnabled(defaultRenderSettings.FXAAEnabled);
		m_dx->SetFXAAContrastThreshold(defaultRenderSettings.FXAAContrastThreshold);
		m_dx->SetFXAARelativeThreshold(defaultRenderSettings.FXAARelativeThreshold);
		m_dx->SetFXAASpanMax(defaultRenderSettings.FXAASpanMax);
	}

	ClearScene(_name);
	CommitCurrentSceneState();
	return true;
}

bool ProjectSceneSystem::SaveScene()
{
	return SaveSceneInternal();
}

bool ProjectSceneSystem::SaveSkeletonToModel(SceneEntityBase* entity)
{
	if (m_ecs == nullptr)
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: ECS is null.");
		return false;
	}
	SceneEntityBase* ownerEntity = ResolveSkeletonOwnerEntityForSave(m_ecs, entity);
	if (ownerEntity == nullptr || !m_ecs->HasEntity(ownerEntity))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: entity is invalid.");
		return false;
	}

	const Witchcraft::Animation::SkeletonData* skeletonData = m_ecs->GetSkeletonData(ownerEntity);
	if (skeletonData == nullptr || skeletonData->Topology.Bones.empty())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: entity has no skeleton data.");
		return false;
	}

	std::set<std::wstring> referencedModelPathStrings;
	CollectReferencedWModelPathsRecursive(m_ecs, ownerEntity, &referencedModelPathStrings);
	if (referencedModelPathStrings.empty())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: no owning .wmodel found.");
		return false;
	}

	std::set<std::filesystem::path> ownerModelPaths;
	for (const std::wstring& modelPathString : referencedModelPathStrings)
		ownerModelPaths.insert(std::filesystem::path(modelPathString).lexically_normal());

	const std::filesystem::path projectDirectory = std::filesystem::path(EngineUtils::GetProjectDirPath());
	std::set<std::filesystem::path> oldSkeletonPaths;
	std::unordered_map<std::wstring, WModelFileData> loadedOwnerModelData;
	for (const std::filesystem::path& modelFilePath : ownerModelPaths)
	{
		WModelFileData modelFileData;
		if (!WModelFile::LoadFromFile(modelFilePath, &modelFileData))
		{
			const std::wstring logText =
				L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: could not load " + modelFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}

		loadedOwnerModelData.emplace(modelFilePath.wstring(), modelFileData);
		if (!modelFileData.SkeletonAsset.empty())
		{
			const std::filesystem::path oldSkeletonPath =
				ResolveProjectAssetPath(modelFilePath.parent_path() / std::filesystem::path(modelFileData.SkeletonAsset));
			if (!oldSkeletonPath.empty())
				oldSkeletonPaths.insert(oldSkeletonPath);
		}
	}

	const std::wstring ownerSkeletonAssetPath = skeletonData->SkeletonAssetPath;
	if (!ownerSkeletonAssetPath.empty())
	{
		const std::filesystem::path ownerSkeletonPath = ResolveProjectAssetPath(ownerSkeletonAssetPath);
		if (!ownerSkeletonPath.empty())
			oldSkeletonPaths.insert(ownerSkeletonPath);
	}

	const std::filesystem::path primaryModelPath = *ownerModelPaths.begin();
	const auto primaryModelDataIt = loadedOwnerModelData.find(primaryModelPath.wstring());
	if (primaryModelDataIt == loadedOwnerModelData.end())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: missing primary model data.");
		return false;
	}

	const WModelFileData& primaryModelData = primaryModelDataIt->second;
	std::filesystem::path skeletonFilePath;
	if (!oldSkeletonPaths.empty())
		skeletonFilePath = *oldSkeletonPaths.begin();
	if (skeletonFilePath.empty())
		skeletonFilePath = Witchcraft::WModelRuntime::BuildWModelSkeletonAssetPath(primaryModelPath, primaryModelData);

	std::error_code createDirError;
	std::filesystem::create_directories(skeletonFilePath.parent_path(), createDirError);
	if (createDirError)
	{
		const std::wstring logText =
			L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: could not create directory " + skeletonFilePath.parent_path().wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	WSkeletonFileData skeletonFileData;
	skeletonFileData.Name = primaryModelData.Name.empty() ? primaryModelPath.stem().wstring() : primaryModelData.Name;
	skeletonFileData.Topology = skeletonData->Topology;
	if (!WSkeletonFile::SaveToFile(skeletonFilePath, skeletonFileData))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: could not save " + skeletonFilePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	std::set<std::filesystem::path> modelsToUpdate = ownerModelPaths;
	// 先走“当前骨骼 owner 关联模型”快速路径，避免保存动作扫描全工程造成明显卡顿。
	// 若未来需要恢复“全项目回填引用”，可把该开关改为 true 或迁移到项目设置。
	constexpr bool kEnableProjectWideModelReferenceScan = false;
	if (kEnableProjectWideModelReferenceScan && !oldSkeletonPaths.empty())
	{
		std::error_code iterateError;
		std::filesystem::recursive_directory_iterator iterator(
			projectDirectory,
			std::filesystem::directory_options::skip_permission_denied,
			iterateError);
		std::filesystem::recursive_directory_iterator iteratorEnd;
		for (; iterator != iteratorEnd; iterator.increment(iterateError))
		{
			if (iterateError)
			{
				iterateError.clear();
				continue;
			}

			const std::filesystem::path entryPath = iterator->path();
			if (iterator->is_directory())
			{
				const std::wstring directoryName = entryPath.filename().wstring();
				if (_wcsicmp(directoryName.c_str(), L".git") == 0 ||
					_wcsicmp(directoryName.c_str(), L".vs") == 0 ||
					_wcsicmp(directoryName.c_str(), L"x64") == 0 ||
					_wcsicmp(directoryName.c_str(), L"build") == 0 ||
					_wcsicmp(directoryName.c_str(), L"Binaries") == 0 ||
					_wcsicmp(directoryName.c_str(), L"Intermediate") == 0)
				{
					iterator.disable_recursion_pending();
				}
				continue;
			}

			if (!iterator->is_regular_file())
				continue;

			const std::filesystem::path modelFilePath = entryPath.lexically_normal();
			if (_wcsicmp(modelFilePath.extension().c_str(), WModelFile::Extension) != 0)
				continue;

			WModelFileData modelFileData;
			if (!WModelFile::LoadFromFile(modelFilePath, &modelFileData))
				continue;
			if (modelFileData.SkeletonAsset.empty())
				continue;

			const std::filesystem::path resolvedSkeletonPath =
				ResolveProjectAssetPath(modelFilePath.parent_path() / std::filesystem::path(modelFileData.SkeletonAsset));
			if (oldSkeletonPaths.find(resolvedSkeletonPath) != oldSkeletonPaths.end())
				modelsToUpdate.insert(modelFilePath);
		}
	}

	UINT updatedModelCount = 0;
	for (const std::filesystem::path& modelFilePath : modelsToUpdate)
	{
		WModelFileData modelFileData;
		if (!WModelFile::LoadFromFile(modelFilePath, &modelFileData))
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: could not load " + modelFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}

		modelFileData.SkeletonAsset = BuildRelativeAssetPath(skeletonFilePath, modelFilePath.parent_path());
		modelFileData.SkeletonName = skeletonFileData.Name;
		modelFileData.Skeleton = {};
		if (!WModelFile::SaveToFile(modelFilePath, modelFileData))
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> SaveSkeletonToModel failed: could not update " + modelFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}
		++updatedModelCount;
	}

	const std::wstring projectRelativeSkeletonPath = BuildRelativeAssetPath(skeletonFilePath, projectDirectory);
	(void)m_ecs->SetEntitySkeletonAssetPath(ownerEntity, projectRelativeSkeletonPath);

	const std::wstring logText =
		L"[ProjectSceneSystem] -> Skeleton saved: skeleton=" + skeletonFilePath.wstring() +
		L", updatedModels=" + std::to_wstring(updatedModelCount);
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::ConfirmLeaveCurrentSceneIfNeeded()
{
	return ConfirmSceneSwitchIfNeeded();
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
	std::wstring projectPath;
	EngineHelpers::TryOpenFileDialog(m_dx->GetHwnd(), L"", L"", L"Open Project", &projectPath);
}

void ProjectSceneSystem::SaveProject()
{
}

bool ProjectSceneSystem::OpenScene()
{
	if (!ConfirmSceneSwitchIfNeeded())
		return false;

	std::filesystem::path sceneFilePath;
	if (!TrySelectSceneFilePath(m_dx != nullptr ? m_dx->GetHwnd() : nullptr, &sceneFilePath))
		return false;

	if (!LoadSceneFileData(sceneFilePath))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> OpenScene failed: " + sceneFilePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	return true;
}

bool ProjectSceneSystem::ReloadCurrentScene()
{
	WSceneFileData sceneFileData;
	if (!BuildSceneFileData(&sceneFileData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> ReloadCurrentScene failed: could not build scene data.");
		return false;
	}

	if (!ApplySceneFileData(sceneFileData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> ReloadCurrentScene failed: could not apply scene data.");
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// 场景切换前确认 / dirty 判定
// ---------------------------------------------------------------------------

// 在切场景前统一检查未保存修改：
// - 未 dirty：直接允许切换
// - dirty：询问是否先保存
// 这是当前“新建/打开场景前阻止误丢失修改”的统一入口。
bool ProjectSceneSystem::ConfirmSceneSwitchIfNeeded()
{
	if (!IsCurrentSceneDirty())
		return true;

	const HWND ownerWindow = m_dx != nullptr ? m_dx->GetHwnd() : nullptr;
	const int dialogResult = EngineHelpers::ShowMessageBox(
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

// 判断当前场景是否有未保存修改。
//
// 现阶段策略：
// 1. 把当前 ECS / 场景配置整理成一份规范化场景快照
// 2. 生成当前 state token
// 3. 与“最近一次提交（保存/打开/新建后确认）的 token”比较
//
// 优点是不会依赖每个编辑操作都记得手动打 dirty 标记；
// 代价是每次检查都需要做一次全量快照与序列化。
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

// ---------------------------------------------------------------------------
// 场景状态 token 捕获 / committed state 提交
// ---------------------------------------------------------------------------

// 把“当前运行中场景”转成可比较的状态令牌。
// 这里不直接比较内存对象，而是复用场景序列化输出，确保比较口径与存盘结构一致。
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

// 提交一份“已保存/已确认”的场景状态。
// 调用时机通常是：
// - 保存成功后
// - 打开场景成功后
// - 新建空场景并初始化完成后
void ProjectSceneSystem::CommitSceneStateFromData(const WSceneFileData& sceneFileData)
{
	m_committedSceneStateToken = BuildSceneStateToken(sceneFileData);
	m_hasCommittedSceneState = true;
}

// 直接从当前运行时场景重新生成 committed token。
// 适合“新建默认空场景后立刻视为干净状态”这类场景。
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
		sceneFileData.RenderSettings.AOEnabled = m_dx->IsAOEnabled();
		sceneFileData.RenderSettings.AOStrength = m_dx->GetAOStrength();
		sceneFileData.RenderSettings.AORadius = m_dx->GetAORadius();
		sceneFileData.RenderSettings.AOFadeStart = m_dx->GetAOFadeStart();
		sceneFileData.RenderSettings.AOFadeEnd = m_dx->GetAOFadeEnd();
		sceneFileData.RenderSettings.AOSurfaceEpsilon = m_dx->GetAOSurfaceEpsilon();
		sceneFileData.RenderSettings.AOBlurSigma = m_dx->GetAOBlurSigma();
		sceneFileData.RenderSettings.FXAAEnabled = m_dx->IsFXAAEnabled();
		sceneFileData.RenderSettings.FXAAContrastThreshold = m_dx->GetFXAAContrastThreshold();
		sceneFileData.RenderSettings.FXAARelativeThreshold = m_dx->GetFXAARelativeThreshold();
		sceneFileData.RenderSettings.FXAASpanMax = m_dx->GetFXAASpanMax();
	}

	if (SceneEntityBase* ambientLightEntity = m_ecs->EnsureDefaultAmbientLightEntity())
	{
		EntityLightComponentData ambientSnapshot{};
		if (m_ecs->GetEntityLightSnapshot(ambientLightEntity, &ambientSnapshot))
		{
			const LightKind ambientKind = static_cast<LightKind>(ambientSnapshot.kind);
			sceneFileData.Environment.HasAmbientLight = true;
			sceneFileData.Environment.AmbientLightActive = m_ecs->IsEntitySelfVisible(ambientLightEntity);
			sceneFileData.Environment.AmbientLight.Kind =
				SerializeSceneLightKind(ambientKind);
			sceneFileData.Environment.AmbientLight.Type =
				ResolveSceneLightTypeFromKind(ambientKind, ambientSnapshot.type);
			sceneFileData.Environment.AmbientLight.Color = ambientSnapshot.color;
			sceneFileData.Environment.AmbientLight.Power = ambientSnapshot.power;
			sceneFileData.Environment.AmbientLight.CastShadow = false;
			sceneFileData.Environment.AmbientLight.EnableVolumetric = false;
			sceneFileData.Environment.AmbientLight.VolumetricIntensity = 0.0f;
			sceneFileData.Environment.AmbientLight.VolumetricAttenuationDistance = 0.0f;
		}
	}

	for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
	{
		const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
		WSceneEntityTypeColorData typeColorData;
		typeColorData.Type = SceneEntityTypeToKey(sceneType);
		typeColorData.Color = m_ecs->GetEntitySceneTypeVertexColor(sceneType);
		sceneFileData.EntityTypeColors.push_back(std::move(typeColorData));
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
	if (m_ecs->IsEnvironmentEntity(entity) || m_ecs->IsAmbientLightEntity(entity))
		return;
	if (IsSerializedSkeletonHelperEntity(m_ecs, entity))
		return;

	WSceneEntityData entityData;
	entityData.Id = BuildEntityIdText(entity);
	entityData.Name = m_ecs->GetEntityName(entity);
	SceneEntityType sceneType = SceneEntityType::StaticScenery;
	if (m_ecs->GetEntitySceneType(entity, &sceneType))
		entityData.EntityType = SceneEntityTypeToKey(sceneType);
	entityData.Active = m_ecs->IsEntitySelfVisible(entity);
	entityData.ParentId = BuildEntityIdText(parent);
	m_ecs->GetEntityEditableLocalTransform(entity, &entityData.LocalTransform);

	EntityLightComponentData lightSnapshot;
	if (m_ecs->GetEntityLightSnapshot(entity, &lightSnapshot))
	{
		const LightKind lightKind = static_cast<LightKind>(lightSnapshot.kind);
		entityData.HasLight = true;
		entityData.Light.Kind = SerializeSceneLightKind(lightKind);
		entityData.Light.Type = ResolveSceneLightTypeFromKind(lightKind, lightSnapshot.type);
		entityData.Light.Color = lightSnapshot.color;
		entityData.Light.Power = lightSnapshot.power;
		entityData.Light.CastShadow = lightKind != LightKind::Ambient && lightSnapshot.castShadow;
		entityData.Light.EnableVolumetric = lightKind != LightKind::Ambient && lightSnapshot.enableVolumetric;
		entityData.Light.VolumetricIntensity = lightKind != LightKind::Ambient ? lightSnapshot.volumetricIntensity : 0.0f;
		entityData.Light.VolumetricAttenuationDistance = lightKind != LightKind::Ambient ? lightSnapshot.volumetricAttenuationDistance : 0.0f;
	}

	EntityCameraComponentData cameraSnapshot;
	if (m_ecs->GetEntityCameraSnapshot(entity, &cameraSnapshot))
	{
		entityData.HasCamera = true;
		entityData.Camera.Primary = cameraSnapshot.primary;
		entityData.Camera.RenderEnabled = cameraSnapshot.renderEnabled;
		entityData.Camera.RenderToTextureEnabled = cameraSnapshot.renderToTextureEnabled;
		entityData.Camera.NearZ = cameraSnapshot.nearZ;
		entityData.Camera.FarZ = cameraSnapshot.farZ;
		entityData.Camera.FovY = cameraSnapshot.fovY;
		entityData.Camera.ViewportScale = cameraSnapshot.viewportScale;
		entityData.Camera.OutputTargetId = cameraSnapshot.outputTargetId;
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
			meshData.MaterialFile = m_dx->GetMaterialFilePathByMaterialName(meshData.MaterialName);
			meshData.SkyTexturePath = m_dx->GetSkyTexturePathByMaterialName(meshData.MaterialName);
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


	{
		SkeletonComponent* skelComp = m_ecs->GetComponent<SkeletonComponent>(entity);
		AnimatorComponent* animComp = m_ecs->GetComponent<AnimatorComponent>(entity);
		SkinningRuntimeComponent* skinComp = m_ecs->GetComponent<SkinningRuntimeComponent>(entity);
		entityData.HasSkeleton = (skelComp != nullptr);
		entityData.HasAnimator = (animComp != nullptr);
		entityData.HasSkinningRuntime = (skinComp != nullptr);
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

	// 打开场景文件前先确保上一场景对应的命令列表已经彻底执行完成。
	// 这样后续 m_ecs->Clear() 中销毁 MeshComponent / Geometry 时，
	// 就不会删除仍被 normalThreadCommandLists 等列表引用的资源。
	if (m_dx != nullptr)
		m_dx->FlushCommandQueue();
	if (m_dx != nullptr)
		m_dx->ResetSceneRuntimeRenderState();

	m_ecs->Clear();
	if (m_dx != nullptr)
		m_dx->RebuildRenderItemsFromEntities(m_ecs);

	sceneName = sceneFileData.Meta.Name.empty() ? DefaultSceneName() : sceneFileData.Meta.Name;
	m_sceneCreatedAt = sceneFileData.Meta.CreatedAt;
	if (m_dx != nullptr)
	{
		m_dx->SetShadowOpacity(sceneFileData.RenderSettings.ShadowOpacity);
		m_dx->SetShadowSoftness(sceneFileData.RenderSettings.ShadowSoftness);
		m_dx->SetAOEnabled(sceneFileData.RenderSettings.AOEnabled);
		m_dx->SetAOStrength(sceneFileData.RenderSettings.AOStrength);
		m_dx->SetAORadius(sceneFileData.RenderSettings.AORadius);
		m_dx->SetAOFadeStart(sceneFileData.RenderSettings.AOFadeStart);
		m_dx->SetAOFadeEnd(sceneFileData.RenderSettings.AOFadeEnd);
		m_dx->SetAOSurfaceEpsilon(sceneFileData.RenderSettings.AOSurfaceEpsilon);
		m_dx->SetAOBlurSigma(sceneFileData.RenderSettings.AOBlurSigma);
		m_dx->SetFXAAEnabled(sceneFileData.RenderSettings.FXAAEnabled);
		m_dx->SetFXAAContrastThreshold(sceneFileData.RenderSettings.FXAAContrastThreshold);
		m_dx->SetFXAARelativeThreshold(sceneFileData.RenderSettings.FXAARelativeThreshold);
		m_dx->SetFXAASpanMax(sceneFileData.RenderSettings.FXAASpanMax);
	}

	m_ecs->ResetEntitySceneTypeVertexColorsToDefault(false);
	for (const WSceneEntityTypeColorData& typeColorData : sceneFileData.EntityTypeColors)
	{
		SceneEntityType sceneType = SceneEntityType::StaticScenery;
		if (!TryParseSceneEntityType(typeColorData.Type, &sceneType))
			continue;

		m_ecs->SetEntitySceneTypeVertexColor(sceneType, typeColorData.Color, false);
	}

	std::unordered_map<std::wstring, std::wstring> modelPathById;
	for (const WSceneModelAssetData& modelData : sceneFileData.Models)
	{
		if (!modelData.Id.empty())
			modelPathById[modelData.Id] = modelData.Path;
	}

	std::vector<std::pair<SceneEntityBase*, const WSceneEntityData*>> meshEntitiesToRestore;
	std::unordered_map<std::wstring, SceneEntityBase*> createdEntities;
	m_ecs->EnsureEnvironmentEntity();
	SceneEntityBase* ambientLightEntity = m_ecs->EnsureDefaultAmbientLightEntity();

	if (sceneFileData.Environment.HasAmbientLight && ambientLightEntity != nullptr)
	{
		EntityLightComponentData lightSnapshot{};
		lightSnapshot.kind = static_cast<std::uint32_t>(LightKind::Ambient);
		lightSnapshot.type = ResolveSceneLightTypeFromKind(LightKind::Ambient, sceneFileData.Environment.AmbientLight.Type);
		lightSnapshot.color = sceneFileData.Environment.AmbientLight.Color;
		lightSnapshot.power = sceneFileData.Environment.AmbientLight.Power;
		lightSnapshot.castShadow = false;
		lightSnapshot.enableVolumetric = false;
		lightSnapshot.volumetricIntensity = 0.0f;
		lightSnapshot.volumetricAttenuationDistance = 0.0f;
		m_ecs->SetEntityLightSnapshot(ambientLightEntity, lightSnapshot);
		m_ecs->SetEntityVisible(ambientLightEntity, sceneFileData.Environment.AmbientLightActive);
	}

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
		const LightKind sceneLightKind = entityData.HasLight
			? ResolveNormalizedSceneLightKind(entityData.Light)
			: LightKind::Directional;
		if (parentEntity != nullptr && m_ecs->IsEnvironmentEntity(parentEntity))
		{
			parentEntity = nullptr;
		}

		SceneEntityBase* entity = nullptr;
		if (entityData.HasMesh)
			entity = m_ecs->CreateMeshEntity(entityName, parentEntity);
		else if (entityData.HasLight)
			entity = m_ecs->CreateLightEntity(entityName, parentEntity);
		else if (entityData.HasCamera)
			entity = m_ecs->CreateCameraEntity(entityName, parentEntity);
		else
			entity = m_ecs->CreateBasicEntity(entityName, parentEntity, ComponentType::Co_Unk);
		if (entity == nullptr)
			continue;

		const SceneEntityType entitySceneType = InferSceneEntityTypeFromEntityData(entityData);
		m_ecs->SetEntitySceneType(entity, entitySceneType, false);

		m_ecs->SetEntityEditableLocalTransform(entity, entityData.LocalTransform);
		m_ecs->SetEntityVisible(entity, entityData.Active);

		if (entityData.HasLight)
		{
			EntityLightComponentData lightSnapshot;
			lightSnapshot.kind = static_cast<std::uint32_t>(sceneLightKind);
			lightSnapshot.type = ResolveSceneLightTypeFromKind(sceneLightKind, entityData.Light.Type);
			lightSnapshot.color = entityData.Light.Color;
			lightSnapshot.power = entityData.Light.Power;
			lightSnapshot.castShadow = static_cast<LightKind>(lightSnapshot.kind) != LightKind::Ambient && entityData.Light.CastShadow;
			lightSnapshot.enableVolumetric = static_cast<LightKind>(lightSnapshot.kind) != LightKind::Ambient && entityData.Light.EnableVolumetric;
			lightSnapshot.volumetricIntensity = static_cast<LightKind>(lightSnapshot.kind) != LightKind::Ambient ? entityData.Light.VolumetricIntensity : 0.0f;
			lightSnapshot.volumetricAttenuationDistance = static_cast<LightKind>(lightSnapshot.kind) != LightKind::Ambient ? entityData.Light.VolumetricAttenuationDistance : 0.0f;
			m_ecs->SetEntityLightSnapshot(entity, lightSnapshot);
		}

		if (entityData.HasCamera)
		{
			EntityCameraComponentData cameraSnapshot;
			cameraSnapshot.primary = entityData.Camera.Primary;
			cameraSnapshot.renderEnabled = entityData.Camera.RenderEnabled;
			cameraSnapshot.renderToTextureEnabled = entityData.Camera.RenderToTextureEnabled;
			cameraSnapshot.nearZ = entityData.Camera.NearZ;
			cameraSnapshot.farZ = entityData.Camera.FarZ;
			cameraSnapshot.fovY = entityData.Camera.FovY;
			cameraSnapshot.viewportScale = entityData.Camera.ViewportScale;
			cameraSnapshot.outputTargetId = entityData.Camera.OutputTargetId;
			m_ecs->SetEntityCameraSnapshot(entity, cameraSnapshot);
			m_ecs->SetCameraEntityEngine(entity, m_engine);
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


		if (entityData.HasSkeleton)
			m_ecs->AddComponent<SkeletonComponent>(entity);
		if (entityData.HasAnimator)
			m_ecs->AddComponent<AnimatorComponent>(entity);
		if (entityData.HasSkinningRuntime)
			m_ecs->AddComponent<SkinningRuntimeComponent>(entity);
		if (!entityData.Id.empty())
			createdEntities[entityData.Id] = entity;
	}

	m_ecs->EnsureEnvironmentEntity();
	m_ecs->EnsureDefaultAmbientLightEntity();

	std::unordered_map<std::wstring, LoadedWModelCache> wmodelCacheByPath;
	std::unordered_map<std::wstring, LoadedRawModelCache> rawModelCacheByPath;
	for (const auto& meshRestoreItem : meshEntitiesToRestore)
	{
		SceneEntityBase* entity = meshRestoreItem.first;
		const WSceneEntityData& entityData = *meshRestoreItem.second;
		const SceneEntityType entitySceneType = InferSceneEntityTypeFromEntityData(entityData);
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
			const std::wstring SkyMaterialName = m_dx->GetOrCreateSkyMaterial(skyTexturePath);
			if (!SkyMaterialName.empty())
				resolvedMaterialName = SkyMaterialName;
		}

		if (m_dx != nullptr && !entityData.Mesh.MaterialFile.empty())
		{
			const std::filesystem::path materialFilePath(entityData.Mesh.MaterialFile);
			if (std::filesystem::exists(materialFilePath))
			{
				const std::wstring MaterialName = m_dx->GetOrCreateMaterialFromWMaterialFile(materialFilePath);
				if (!MaterialName.empty())
					resolvedMaterialName = MaterialName;
			}
		}

		bool restoredGeometry = false;
		// ExternalGeometry is only stable for scene-owned geometry without a source
		// model. Imported .wmodel meshes used to serialize their transient GPU name
		// here; after an ECS clear that name can still resolve to a stale D3D entry.
		// A model-backed entity must always rebuild from its asset instead.
		const bool canReuseExternalGeometry =
			renderSourceType == L"Sky" ||
			(renderSourceType == L"ExternalGeometry" && modelPath.empty());
		if (canReuseExternalGeometry && m_dx != nullptr)
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
		// 兼容旧场景：早期保存会把带运行时外置几何的 .wmodel 标为
		// ExternalGeometry。场景重开后该 GPU 几何不存在，应回退到模型文件。
		if (!restoredGeometry &&
			(renderSourceType == L"PrimitiveBuiltin" ||
				renderSourceType == L"ModelFile" ||
				(renderSourceType == L"ExternalGeometry" && !modelPath.empty())) &&
			!modelPath.empty())
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
					wmodelCache.Loaded = Witchcraft::WModelRuntime::LoadWModelRuntimeAsset(modelFilePath, &wmodelCache.Asset);
					if (wmodelCache.Loaded)
					{
						if (m_dx != nullptr)
						{
							for (const WModelMaterialRef& materialRef : wmodelCache.Asset.Data.Materials)
							{
								const std::filesystem::path materialPath = Witchcraft::WModelRuntime::ResolveWModelReferencedPath(wmodelCache.Asset.ModelDirectory, materialRef.File);
								if (!std::filesystem::exists(materialPath))
									continue;

								const std::wstring MaterialName = m_dx->GetOrCreateMaterialFromWMaterialFile(materialPath);
								if (!MaterialName.empty())
									wmodelCache.MaterialByRef[materialRef.Id] = MaterialName;
							}
						}
					}
				}

				if (wmodelCache.Loaded)
				{
					SceneEntityBase* skeletonOwnerEntity = FindLoadedSkeletonOwnerForMesh(m_ecs, entity);
					if (skeletonOwnerEntity != nullptr &&
						wmodelCache.InitializedSkeletonOwners.insert(skeletonOwnerEntity).second)
					{
						const std::filesystem::path skeletonAssetPath =
							wmodelCache.Asset.SkeletonAssetPath;
						if (!skeletonAssetPath.empty() && std::filesystem::exists(skeletonAssetPath))
						{
							const std::filesystem::path projectDirectory(EngineUtils::GetProjectDirPath());
							std::wstring runtimeSkeletonAssetPath =
								BuildRelativeAssetPath(skeletonAssetPath, projectDirectory);
							if (runtimeSkeletonAssetPath.empty())
								runtimeSkeletonAssetPath = skeletonAssetPath.wstring();

							(void)m_ecs->EnsureEntitySkeletonRuntime(
								skeletonOwnerEntity,
								runtimeSkeletonAssetPath,
								nullptr,
								m_ecs->GetComponent<SkeletonComponent>(skeletonOwnerEntity) != nullptr,
								true,
								true,
								false);
						}
					}

					std::wstring nodeId = entityData.Mesh.NodeId;
					const std::wstring normalizedSavedNodeId = TryExtractModelNodeId(nodeId);
					if (!normalizedSavedNodeId.empty())
						nodeId = normalizedSavedNodeId;
					if (nodeId.empty())
						nodeId = TryExtractModelNodeId(entityData.Mesh.MeshName);

					const WModelNodeData* nodeData = Witchcraft::WModelRuntime::FindWModelNodeById(wmodelCache.Asset.Data.RootNode, nodeId);
					if (nodeData == nullptr)
						nodeData = FindFirstMeshNodeByName(wmodelCache.Asset.Data.RootNode, entityData.Name);
					Witchcraft::WModelRuntime::WModelMeshPayload meshPayload;
					const DirectX::XMFLOAT4 sceneTypeColor =
						m_ecs->GetEntitySceneTypeVertexColor(entitySceneType);
					bool hasMeshPayload = false;
					if (nodeData != nullptr)
					{
						hasMeshPayload = Witchcraft::WModelRuntime::BuildWModelMeshPayload(
							wmodelCache.Asset,
							*nodeData,
							true,
							&sceneTypeColor,
							&meshPayload);

						// 导入器可以将经过身份转换的网格子节点吸收到其空父节点中。
						// 保存的旧场景可能引用该父节点；如果它不能按吸收规则恢复，则回退到旧逻辑的第一个 mesh descendant。
						if (!hasMeshPayload && nodeData->MeshRef.empty())
						{
							if (const WModelNodeData* meshDescendant = Witchcraft::WModelRuntime::FindFirstWModelMeshDescendant(*nodeData))
							{
								nodeData = meshDescendant;
								hasMeshPayload = Witchcraft::WModelRuntime::BuildWModelMeshPayload(
									wmodelCache.Asset,
									*nodeData,
									false,
									&sceneTypeColor,
									&meshPayload);
							}
						}
					}

					if (hasMeshPayload)
					{
						if ((resolvedMaterialName.empty() || (m_dx != nullptr && m_dx->GetMaterialByMaterialName(resolvedMaterialName) == nullptr))
							&& !meshPayload.PrimaryMaterialRef.empty())
						{
							const auto materialIt = wmodelCache.MaterialByRef.find(meshPayload.PrimaryMaterialRef);
							if (materialIt != wmodelCache.MaterialByRef.end())
								resolvedMaterialName = materialIt->second;
						}

						if (nodeData != nullptr)
						{
							m_ecs->ConfigureMeshEntity(
								entity,
								m_engine,
								entityData.Name.empty() ? L"Entity" : entityData.Name,
								modelPath,
								entityData.Mesh.MeshName,
								renderLayer,
								resolvedMaterialName);

							m_ecs->AppendMeshEntityVertices(entity, meshPayload.Vertices);
							m_ecs->AppendMeshEntityIndices(entity, meshPayload.Indices);

							const bool hasCompleteSkinning =
								meshPayload.HasCompleteSkinning &&
								skeletonOwnerEntity != nullptr;
							if (hasCompleteSkinning && m_engine != nullptr)
							{
								const std::wstring skinnedGeometryName = entityData.Mesh.MeshName + L"_SkinnedGeo";
								std::wstring skeletonAssetPath;
								if (const Witchcraft::Animation::SkeletonData* skeletonData =
									m_ecs->GetSkeletonData(skeletonOwnerEntity))
								{
									skeletonAssetPath = skeletonData->SkeletonAssetPath;
								}

								AssimpLoader skinnedMeshUploader;
								skinnedMeshUploader.Create(m_engine);
								restoredGeometry = skinnedMeshUploader.UploadAndBindInlineSkinnedMeshGeometry(
									m_ecs,
									entity,
									skinnedGeometryName,
									meshPayload.Vertices,
									meshPayload.Skinning,
									meshPayload.Indices,
									skeletonAssetPath,
									false);
							}
							if (!restoredGeometry && m_dx != nullptr)
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

		m_ecs->SetEntitySceneType(entity, entitySceneType);
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
