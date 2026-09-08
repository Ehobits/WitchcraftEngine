#include "ProjectSceneSystem.h"

#include "ECS/WitchcraECS.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/LightComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/ScriptingComponent.h"
#include "ENGINE/EngineUtils.h"
#include "Helpers/Helpers.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "String/SStringUtils.h"
#include "System/WitchcraftFile/WModelFile.h"
#include "System/WitchcraftFile/WProjectFile.h"
#include "System/WitchcraftFile/WModelRuntimeHelpers.h"
#include "System/WitchcraftFile/WSceneFile.h"
#include "System/WitchcraftFile/WSkeletonFile.h"

#include "ECS/Component/SkeletonComponent.h"
#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/SkinnedMeshComponent.h"
#include "ECS/Component/SkinningRuntimeComponent.h"
#include "Engine/Engine.h"
#include <algorithm>
#include <cstddef>
#include <DirectXMath.h>
#include <shellapi.h>
#include <set>
#include <system_error>
#include <utility>

namespace ProjectSceneSystemDetail
{
	constexpr UINT kSkyRenderLayer = 0;
	const wchar_t* kDefaultSkyGeometryRef = L"shapeGeo";
	const wchar_t* kDefaultSkyTexturePath = L"DATA/HDRIs/scythian_tombs_2_4k.png";
	const wchar_t* kDefaultProjectName = L"未命名项目";
	const wchar_t* kNoProjectName = L"未打开项目";
	bool IsSerializedSkeletonHelperEntity(WitchcraECS* ecs, SceneEntityBase* entity);

	void AddConsoleError(Engine* engine, const std::wstring& text)
	{
		if (engine != nullptr && engine->GetConsoleWindow() != nullptr)
			engine->GetConsoleWindow()->AddErrorMessage(L"%s", text.c_str());
		else
			EngineHelpers::AddLog(L"%s", text.c_str());
	}

	WSceneAnimatorLayerBlendMode ConvertAnimatorLayerBlendMode(AnimatorComponent::AnimationLayerBlendMode blendMode)
	{
		switch (blendMode)
		{
		case AnimatorComponent::AnimationLayerBlendMode::Additive:
			return WSceneAnimatorLayerBlendMode::Additive;
		case AnimatorComponent::AnimationLayerBlendMode::Override:
		default:
			return WSceneAnimatorLayerBlendMode::Override;
		}
	}

	AnimatorComponent::AnimationLayerBlendMode ConvertAnimatorLayerBlendMode(WSceneAnimatorLayerBlendMode blendMode)
	{
		switch (blendMode)
		{
		case WSceneAnimatorLayerBlendMode::Additive:
			return AnimatorComponent::AnimationLayerBlendMode::Additive;
		case WSceneAnimatorLayerBlendMode::Override:
		default:
			return AnimatorComponent::AnimationLayerBlendMode::Override;
		}
	}

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

	flecs::entity_t ParseEntityIdText(const std::wstring& text)
	{
		if (text.empty())
			return 0;

		try
		{
			return static_cast<flecs::entity_t>(std::stoull(text));
		}
		catch (...)
		{
			return 0;
		}
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

		return EngineUtils::ResolveProjectPath(path);
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

	bool IsPathInsideDirectory(const std::filesystem::path& path, const std::filesystem::path& directory)
	{
		if (path.empty() || directory.empty())
			return false;

		std::error_code relativeError;
		const std::filesystem::path relativePath = std::filesystem::relative(path, directory, relativeError);
		if (relativeError || relativePath.empty())
			return false;

		if (relativePath == L".")
			return false;

		for (const std::filesystem::path& part : relativePath)
		{
			if (part == L"..")
				return false;
		}
		return true;
	}

	bool MovePathToRecycleBin(const std::filesystem::path& path)
	{
		if (path.empty())
			return false;

		std::wstring fromPath = path.wstring();
		fromPath.push_back(L'\0');
		fromPath.push_back(L'\0');

		SHFILEOPSTRUCTW fileOp{};
		fileOp.wFunc = FO_DELETE;
		fileOp.pFrom = fromPath.c_str();
		fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
		return SHFileOperationW(&fileOp) == 0 && !fileOp.fAnyOperationsAborted;
	}

	std::wstring BuildProjectAssetReferencePath(const std::wstring& assetPath)
	{
		if (assetPath.empty())
			return L"";

		const std::filesystem::path projectDirectory(EngineUtils::GetProjectDirPath());
		const std::filesystem::path originalPath(assetPath);
		const std::filesystem::path resolvedPath = ResolveProjectAssetPath(originalPath);
		const std::filesystem::path engineDirectory(EngineUtils::GetEngineResourceDirPath());

		if (originalPath.is_relative())
		{
			const std::filesystem::path projectCandidate = (projectDirectory / originalPath).lexically_normal();
			std::error_code projectExistsError;
			if (std::filesystem::exists(projectCandidate, projectExistsError))
				return originalPath.lexically_normal().wstring();

			const std::filesystem::path engineCandidate = (engineDirectory / originalPath).lexically_normal();
			std::error_code engineExistsError;
			if (std::filesystem::exists(engineCandidate, engineExistsError))
				return originalPath.lexically_normal().wstring();
		}

		if (IsPathInsideDirectory(resolvedPath, projectDirectory))
			return BuildRelativeAssetPath(resolvedPath, projectDirectory);

		if (IsPathInsideDirectory(resolvedPath, engineDirectory))
			return BuildRelativeAssetPath(resolvedPath, engineDirectory);

		return resolvedPath.lexically_normal().wstring();
	}

	void AppendAnimatorComponentData(const AnimatorComponent& animatorComponent, WSceneAnimatorData* outAnimatorData)
	{
		if (outAnimatorData == nullptr)
			return;

		const std::vector<AnimatorComponent::AnimationLayer>& layers = animatorComponent.GetLayers();
		outAnimatorData->Layers.reserve(layers.size());
		for (const AnimatorComponent::AnimationLayer& layer : layers)
		{
			WSceneAnimatorLayerData layerData;
			layerData.Name = layer.Name;
			layerData.ClipAssetPath = BuildProjectAssetReferencePath(layer.ClipAssetPath);
			layerData.MaskRootBoneName = layer.MaskRootBoneName;
			layerData.Weight = layer.Weight;
			layerData.Loop = layer.Loop;
			layerData.Enabled = layer.Enabled;
			layerData.BlendMode = ConvertAnimatorLayerBlendMode(layer.BlendMode);
			outAnimatorData->Layers.push_back(std::move(layerData));
		}
	}

	void ApplyAnimatorComponentData(const WSceneAnimatorData& animatorData, AnimatorComponent* animatorComponent)
	{
		if (animatorComponent == nullptr)
			return;

		animatorComponent->ClearLayers();
		for (const WSceneAnimatorLayerData& layerData : animatorData.Layers)
		{
			const std::size_t layerIndex = animatorComponent->AddLayer(layerData.Name);
			const std::filesystem::path resolvedClipAssetPath = ResolveProjectAssetPath(layerData.ClipAssetPath);
			(void)animatorComponent->SetLayerClipAssetPath(layerIndex, resolvedClipAssetPath.wstring());
			(void)animatorComponent->SetLayerMaskRootBoneName(layerIndex, layerData.MaskRootBoneName);
			(void)animatorComponent->SetLayerWeight(layerIndex, layerData.Weight);
			(void)animatorComponent->SetLayerLoop(layerIndex, layerData.Loop);
			(void)animatorComponent->SetLayerEnabled(layerIndex, layerData.Enabled);
			(void)animatorComponent->SetLayerBlendMode(layerIndex, ConvertAnimatorLayerBlendMode(layerData.BlendMode));
		}
		if (animatorComponent->GetLayers().empty())
			(void)animatorComponent->EnsureBaseLayer();
	}

	void AppendScriptingComponentData(WitchcraECS* ecs, SceneEntityBase* entity, WSceneScriptingData* outScriptingData)
	{
		if (ecs == nullptr || entity == nullptr || outScriptingData == nullptr)
			return;

		EntityScriptingComponentData scriptingSnapshot;
		if (!ecs->GetEntityScriptingSnapshot(entity, &scriptingSnapshot) || scriptingSnapshot.scripts.empty())
			return;

		outScriptingData->Scripts.clear();
		outScriptingData->Scripts.reserve(scriptingSnapshot.scripts.size());
		for (const EntityScriptingComponentData::ScriptSnapshot& scriptSnapshot : scriptingSnapshot.scripts)
		{
			if (scriptSnapshot.filePath.empty())
				continue;

			WSceneScriptData scriptData;
			scriptData.FilePath = BuildProjectAssetReferencePath(scriptSnapshot.filePath);
			scriptData.Active = scriptSnapshot.activeComponent;
			outScriptingData->Scripts.push_back(std::move(scriptData));
		}
	}

	bool ApplyScriptingComponentData(WitchcraECS* ecs, SceneEntityBase* entity, const WSceneScriptingData& scriptingData)
	{
		if (ecs == nullptr || entity == nullptr)
			return false;

		ScriptingComponent* scriptingComponent = ecs->GetComponent<ScriptingComponent>(entity);
		if (scriptingData.Scripts.empty())
		{
			if (scriptingComponent != nullptr && !ecs->RemoveScriptingComponent(entity))
				return false;
			return true;
		}

		if (scriptingComponent != nullptr && !ecs->RemoveScriptingComponent(entity))
			return false;

		bool addedAnyScript = false;
		for (const WSceneScriptData& scriptData : scriptingData.Scripts)
		{
			if (scriptData.FilePath.empty())
				continue;

			const std::filesystem::path resolvedScriptPath = ResolveProjectAssetPath(scriptData.FilePath);
			if (!ecs->AddScriptToEntity(entity, resolvedScriptPath.wstring()))
				return false;
			addedAnyScript = true;
			if (!scriptData.Active)
			{
				EntityScriptingComponentData snapshot;
				if (!ecs->GetEntityScriptingSnapshot(entity, &snapshot) || snapshot.scripts.empty())
					return false;
				if (!ecs->SetEntityScriptActive(entity, snapshot.scripts.size() - 1, false))
					return false;
			}
		}

		return addedAnyScript;
	}

	void CollectSerializedSceneEntitiesById(
		WitchcraECS* ecs,
		SceneEntityBase* entity,
		std::unordered_map<std::wstring, SceneEntityBase*>* outEntitiesById)
	{
		if (ecs == nullptr || entity == nullptr || outEntitiesById == nullptr)
			return;
		if (ecs->IsEnvironmentEntity(entity) || ecs->IsAmbientLightEntity(entity))
			return;
		if (IsSerializedSkeletonHelperEntity(ecs, entity))
			return;

		const std::wstring entityId = BuildEntityIdText(entity);
		if (!entityId.empty())
			outEntitiesById->emplace(entityId, entity);

		for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
			CollectSerializedSceneEntitiesById(ecs, childEntity, outEntitiesById);
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
	// 这个 helper 目前只用于提交/对照的内部场景状态，不再参与每帧 dirty 查询。
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

	WProjectFileData BuildBlankProjectFileData(const std::wstring& projectName)
	{
		WProjectFileData projectFileData;
		projectFileData.Meta.Name = projectName.empty() ? kDefaultProjectName : projectName;
		projectFileData.Meta.CreatedAt = BuildCurrentTimestampText();
		projectFileData.Meta.UpdatedAt = projectFileData.Meta.CreatedAt;
		return projectFileData;
	}

	std::wstring BuildNextProjectSceneId(const std::vector<WProjectSceneData>& scenes)
	{
		std::set<std::wstring> usedIds;
		for (const WProjectSceneData& scene : scenes)
		{
			if (!scene.Id.empty())
				usedIds.insert(scene.Id);
		}

		std::uint32_t sceneIndex = static_cast<std::uint32_t>(scenes.size() + 1);
		while (true)
		{
			const std::wstring candidate = L"scene_" + std::to_wstring(sceneIndex);
			if (usedIds.find(candidate) == usedIds.end())
				return candidate;
			++sceneIndex;
		}
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
			EngineUtils::GetProjectDirPath().c_str(),
			L"Witchcraft 场景 (*.wscene)\0*.wscene\0All Files (*.*)\0*.*\0\0",
			L"打开场景",
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

	class SceneDirtySuppressionScope
	{
	public:
		explicit SceneDirtySuppressionScope(ProjectSceneSystem& system)
			: m_system(system)
		{
			m_system.BeginSceneDirtySuppression();
		}

		~SceneDirtySuppressionScope()
		{
			m_system.EndSceneDirtySuppression();
		}

	private:
		ProjectSceneSystem& m_system;
	};

}

using namespace ProjectSceneSystemDetail;

void ProjectSceneSystem::Init(D3DWindow* dx, WitchcraECS* ecs, Engine* engine)
{
	m_dx = dx;
	m_ecs = ecs;
	m_engine = engine;
	m_projectFilePath.clear();
	EngineUtils::ClearProjectDirPath();
	m_projectFileData = BuildBlankProjectFileData(kDefaultProjectName);
	ClearProjectDirty();
	ClearCurrentSceneDirty();
	ClearScene(DefaultSceneName());
	m_hasCommittedSceneState = false;
	m_committedSceneStateToken.clear();
	CommitCurrentProjectState();
}

void ProjectSceneSystem::SetSceneLoadedCallback(std::function<void()> callback)
{
	m_sceneLoadedCallback = std::move(callback);
}

void ProjectSceneSystem::SetSceneDirtyCallback(std::function<void()> callback)
{
	if (m_ecs != nullptr)
		m_ecs->SetSceneDirtyCallback(std::move(callback));
}

void ProjectSceneSystem::SetProjectFileChangedCallback(std::function<void(const std::filesystem::path&)> callback)
{
	m_projectFileChangedCallback = std::move(callback);
}

void ProjectSceneSystem::NotifySceneLoaded()
{
	if (m_sceneLoadedCallback)
		m_sceneLoadedCallback();
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

std::wstring ProjectSceneSystem::GetProjectName() const
{
	if (m_projectFilePath.empty())
		return kNoProjectName;

	return m_projectFileData.Meta.Name.empty() ? std::wstring(kDefaultProjectName) : m_projectFileData.Meta.Name;
}

std::filesystem::path ProjectSceneSystem::GetProjectFilePath() const
{
	return m_projectFilePath;
}

std::filesystem::path ProjectSceneSystem::GetSceneFilePath() const
{
	return m_sceneFilePath;
}

std::wstring ProjectSceneSystem::GetCurrentProjectSceneId() const
{
	return m_projectFileData.EditorState.CurrentSceneId;
}

const std::vector<WProjectSceneData>& ProjectSceneSystem::GetProjectScenes() const
{
	return m_projectFileData.Scenes;
}

bool ProjectSceneSystem::IsProjectOpen() const
{
	return !m_projectFilePath.empty();
}

bool ProjectSceneSystem::IsProjectDirty() const
{
	return m_projectDirty;
}

// 判断当前场景是否有未保存修改。
// 现在由编辑事件驱动的缓存状态维护，不再每帧做全量快照比对。
bool ProjectSceneSystem::IsCurrentSceneDirty() const
{
	return m_currentSceneDirty;
}

void ProjectSceneSystem::MarkCurrentSceneDirty()
{
	if (m_sceneDirtySuspensionDepth > 0)
		return;
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
		return;
	if (m_projectFilePath.empty())
		return;

	m_currentSceneDirty = true;
}

void ProjectSceneSystem::ClearCurrentSceneDirty()
{
	m_currentSceneDirty = false;
}

void ProjectSceneSystem::BeginSceneDirtySuppression()
{
	++m_sceneDirtySuspensionDepth;
}

void ProjectSceneSystem::EndSceneDirtySuppression()
{
	if (m_sceneDirtySuspensionDepth == 0)
		return;

	--m_sceneDirtySuspensionDepth;
}

bool ProjectSceneSystem::OpenProjectSceneByIndex(std::size_t sceneIndex)
{
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，切换项目场景被阻止。");
		return false;
	}

	if (sceneIndex >= m_projectFileData.Scenes.size())
		return false;

	if (!ConfirmSceneSwitchIfNeeded())
		return false;

	const WProjectSceneData sceneData = m_projectFileData.Scenes[sceneIndex];
	const std::filesystem::path projectDirectory = m_projectFilePath.empty()
		? std::filesystem::path(EngineUtils::GetProjectDirPath())
		: m_projectFilePath.parent_path();
	const std::filesystem::path sceneFilePath = ResolveProjectRelativePath(sceneData.Path, projectDirectory);
	if (sceneFilePath.empty() || !std::filesystem::exists(sceneFilePath))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> 项目场景不存在：" + sceneData.Path;
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	if (!LoadSceneFileData(sceneFilePath))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> 项目场景加载失败：" + sceneFilePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	m_projectFileData.Settings.CurrentScenePath = sceneData.Path;
	m_projectFileData.EditorState.CurrentSceneId = sceneData.Id;
	CommitCurrentProjectState();
	MarkProjectDirty();
	return true;
}

bool ProjectSceneSystem::SetProjectEntrySceneByIndex(std::size_t sceneIndex)
{
	if (sceneIndex >= m_projectFileData.Scenes.size())
		return false;

	for (WProjectSceneData& sceneData : m_projectFileData.Scenes)
		sceneData.Entry = false;

	WProjectSceneData& entrySceneData = m_projectFileData.Scenes[sceneIndex];
	entrySceneData.Entry = true;
	m_projectFileData.Settings.DefaultScenePath = entrySceneData.Path;
	MarkProjectDirty();
	return true;
}

bool ProjectSceneSystem::RemoveProjectSceneByIndex(std::size_t sceneIndex)
{
	if (sceneIndex >= m_projectFileData.Scenes.size())
		return false;

	const WProjectSceneData& sceneData = m_projectFileData.Scenes[sceneIndex];
	if (sceneData.Id == m_projectFileData.EditorState.CurrentSceneId ||
		sceneData.Path == m_projectFileData.Settings.CurrentScenePath)
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 当前正在编辑的场景不能直接从项目中移除。");
		return false;
	}

	const bool removingEntryScene = sceneData.Entry;
	m_projectFileData.Scenes.erase(m_projectFileData.Scenes.begin() + static_cast<std::ptrdiff_t>(sceneIndex));
	if (removingEntryScene && !m_projectFileData.Scenes.empty())
	{
		m_projectFileData.Scenes.front().Entry = true;
		m_projectFileData.Settings.DefaultScenePath = m_projectFileData.Scenes.front().Path;
	}
	MarkProjectDirty();
	return true;
}

bool ProjectSceneSystem::RenameProjectSceneByIndex(std::size_t sceneIndex, const std::wstring& newName)
{
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，重命名项目场景被阻止。");
		return false;
	}

	if (sceneIndex >= m_projectFileData.Scenes.size() || m_projectFilePath.empty())
		return false;

	const std::wstring sceneDisplayName = newName.empty() ? DefaultSceneName() : newName;
	std::wstring sceneStem = SanitizeSceneStem(sceneDisplayName);
	if (sceneStem.empty())
		sceneStem = DefaultSceneName();

	const std::filesystem::path projectDirectory = m_projectFilePath.parent_path();
	const WProjectSceneData oldSceneData = m_projectFileData.Scenes[sceneIndex];
	const std::filesystem::path currentSceneFilePath = ResolveProjectRelativePath(oldSceneData.Path, projectDirectory);
	if (currentSceneFilePath.empty())
		return false;

	const std::filesystem::path targetSceneFilePath =
		(currentSceneFilePath.parent_path() / (sceneStem + WSceneFile::Extension)).lexically_normal();
	const std::wstring targetScenePathText = BuildProjectRelativePathText(targetSceneFilePath, projectDirectory);
	const std::filesystem::path normalizedCurrentSceneFilePath = currentSceneFilePath.lexically_normal();
	const bool isCurrentScene =
		oldSceneData.Id == m_projectFileData.EditorState.CurrentSceneId ||
		normalizedCurrentSceneFilePath == m_sceneFilePath.lexically_normal();

	if (targetSceneFilePath != currentSceneFilePath)
	{
		std::error_code existsError;
		if (std::filesystem::exists(targetSceneFilePath, existsError))
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> 重命名项目场景失败，目标文件已存在：" + targetSceneFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}
	}

	std::wstring updatedCreatedAt = m_sceneCreatedAt;
	if (isCurrentScene)
	{
		WSceneFileData sceneFileData;
		if (!BuildSceneFileData(&sceneFileData))
		{
			EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 重命名项目场景失败：无法生成当前场景数据。");
			return false;
		}

		sceneFileData.Meta.Name = sceneDisplayName;
		sceneFileData.Meta.UpdatedAt = BuildCurrentTimestampText();
		updatedCreatedAt = sceneFileData.Meta.CreatedAt;

		if (!WSceneFile::SaveToFile(targetSceneFilePath, sceneFileData))
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> 重命名项目场景失败，无法保存：" + targetSceneFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}

		if (targetSceneFilePath != currentSceneFilePath)
		{
			std::error_code removeError;
			std::filesystem::remove(currentSceneFilePath, removeError);
		}

		sceneName = sceneDisplayName;
		m_sceneCreatedAt = updatedCreatedAt;
		m_sceneFilePath = targetSceneFilePath;
		CommitSceneStateFromData(sceneFileData);
	}
	else
	{
		WSceneFileData sceneFileData;
		if (!WSceneFile::LoadFromFile(currentSceneFilePath, &sceneFileData))
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> 重命名项目场景失败，无法加载：" + currentSceneFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}

		sceneFileData.Meta.Name = sceneDisplayName;
		sceneFileData.Meta.UpdatedAt = BuildCurrentTimestampText();
		updatedCreatedAt = sceneFileData.Meta.CreatedAt;

		if (!WSceneFile::SaveToFile(targetSceneFilePath, sceneFileData))
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> 重命名项目场景失败，无法保存：" + targetSceneFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}

		if (targetSceneFilePath != currentSceneFilePath)
		{
			std::error_code removeError;
			std::filesystem::remove(currentSceneFilePath, removeError);
		}
	}

	WProjectSceneData& sceneData = m_projectFileData.Scenes[sceneIndex];
	sceneData.Name = sceneDisplayName;
	sceneData.Path = targetScenePathText;
	if (sceneData.Entry)
		m_projectFileData.Settings.DefaultScenePath = targetScenePathText;
	if (m_projectFileData.EditorState.CurrentSceneId == oldSceneData.Id)
		m_projectFileData.Settings.CurrentScenePath = targetScenePathText;

	if (isCurrentScene)
	{
		// 已在上面更新了当前场景相关状态，这里只保留项目状态同步。
		m_sceneCreatedAt = updatedCreatedAt;
	}

	CommitCurrentProjectState();
	MarkProjectDirty();

	const std::wstring logText = L"[ProjectSceneSystem] -> 项目场景已重命名：" + targetSceneFilePath.wstring();
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::DeleteProjectSceneByIndex(std::size_t sceneIndex)
{
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，删除项目场景被阻止。");
		return false;
	}

	if (sceneIndex >= m_projectFileData.Scenes.size() || m_projectFilePath.empty())
		return false;

	const WProjectSceneData sceneData = m_projectFileData.Scenes[sceneIndex];
	const std::filesystem::path projectDirectory = m_projectFilePath.parent_path();
	const std::filesystem::path sceneFilePath = ResolveProjectRelativePath(sceneData.Path, projectDirectory);
	const bool isCurrentScene =
		sceneData.Id == m_projectFileData.EditorState.CurrentSceneId ||
		sceneFilePath == m_sceneFilePath.lexically_normal();

	if (isCurrentScene)
	{
		if (m_projectFileData.Scenes.size() <= 1)
		{
			EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 当前项目只剩一个场景，无法删除当前场景。");
			return false;
		}

		std::size_t fallbackSceneIndex = static_cast<std::size_t>(-1);
		for (std::size_t index = 0; index < m_projectFileData.Scenes.size(); ++index)
		{
			if (index == sceneIndex)
				continue;
			fallbackSceneIndex = index;
			if (m_projectFileData.Scenes[index].Entry)
				break;
		}

		if (fallbackSceneIndex == static_cast<std::size_t>(-1) || !OpenProjectSceneByIndex(fallbackSceneIndex))
			return false;
	}

	if (!sceneFilePath.empty())
	{
		if (!MovePathToRecycleBin(sceneFilePath))
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> 删除项目场景文件失败（未移动到回收站）：" + sceneFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}
	}
	else
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 删除项目场景失败：场景文件路径为空。");
		return false;
	}

	const bool removingEntryScene = sceneData.Entry;
	m_projectFileData.Scenes.erase(m_projectFileData.Scenes.begin() + static_cast<std::ptrdiff_t>(sceneIndex));
	if (removingEntryScene && !m_projectFileData.Scenes.empty())
	{
		m_projectFileData.Scenes.front().Entry = true;
		m_projectFileData.Settings.DefaultScenePath = m_projectFileData.Scenes.front().Path;
	}
	if (m_projectFileData.Scenes.empty())
	{
		m_projectFileData.Settings.DefaultScenePath.clear();
		if (m_projectFileData.EditorState.CurrentSceneId == sceneData.Id)
			m_projectFileData.Settings.CurrentScenePath.clear();
	}

	CommitCurrentProjectState();
	MarkProjectDirty();

	const std::wstring logText = L"[ProjectSceneSystem] -> 项目场景已删除：" + sceneFilePath.wstring();
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::NewProject()
{
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，新建项目被阻止。");
		return false;
	}

	if (!ConfirmSceneSwitchIfNeeded())
		return false;

	std::wstring projectPath;
	const std::wstring projectDirectory = EngineUtils::GetProjectDirPath();
	if (!EngineHelpers::TrySaveFileDialog(
		m_dx->GetHwnd(),
		projectDirectory.c_str(),
		L"Witchcraft 项目 (*.wproject)\0*.wproject\0所有文件 (*.*)\0*.*\0\0",
		L"新建项目",
		L"wproject",
		&projectPath))
	{
		return false;
	}

	const std::filesystem::path selectedProjectPath = std::filesystem::path(projectPath).lexically_normal();
	const std::wstring projectName = selectedProjectPath.stem().wstring().empty()
		? std::wstring(kDefaultProjectName)
		: selectedProjectPath.stem().wstring();

	m_projectFilePath = selectedProjectPath;
	EngineUtils::SetProjectDirPath(m_projectFilePath.parent_path());
	m_projectFileData = BuildBlankProjectFileData(projectName);
	ClearCurrentSceneDirty();

	SceneDirtySuppressionScope sceneDirtyScope(*this);
	m_dx->FlushCommandQueue();
	m_dx->ResetSceneRuntimeRenderState();

	m_ecs->Clear();
	m_ecs->EnsureEnvironmentEntity();
	m_ecs->EnsureDefaultAmbientLightEntity();
	m_ecs->ResetEntitySceneTypeVertexColorsToDefault(false);
	m_dx->RebuildRenderItemsFromEntities(m_ecs);

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
	m_dx->SetColorAdjustWhiteBalance(defaultRenderSettings.ColorAdjustWhiteBalance);
	m_dx->SetColorAdjustContrast(defaultRenderSettings.ColorAdjustContrast);
	m_dx->SetColorAdjustSaturation(defaultRenderSettings.ColorAdjustSaturation);
	m_dx->SetEnvironmentDiffuseIntensity(defaultRenderSettings.EnvironmentDiffuseIntensity);
	m_dx->SetEnvironmentSpecularIntensity(defaultRenderSettings.EnvironmentSpecularIntensity);
	m_dx->SetEnvironmentBrdfLutEnabled(defaultRenderSettings.EnvironmentBrdfLutEnabled);

	ClearScene(L"未命名场景");
	CommitCurrentSceneState();
	CommitCurrentProjectState();

	if (!SaveProject())
		return false;
	ClearProjectDirty();

	if (m_projectFileChangedCallback)
		m_projectFileChangedCallback(m_projectFilePath);

	const std::wstring logText = L"[ProjectSceneSystem] -> 项目已新建：" + m_projectFilePath.wstring();
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::NewScene(
	std::wstring _name,
	const std::array<DirectX::XMFLOAT4, static_cast<std::size_t>(SceneEntityType::Count)>* sceneTypeColors)
{
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，新建场景被阻止。");
		return false;
	}

	if (!IsProjectOpen())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 请先新建或打开项目，再新建项目场景。");
		return false;
	}

	if (!ConfirmSceneSwitchIfNeeded())
		return false;

	// 切场景前先等待 GPU 完成上一轮绘制。
	// 否则旧场景的网格/材质资源可能仍被工作线程命令列表引用，
	// 随后清空 ECS 时释放资源会触发 OBJECT_DELETED_WHILE_STILL_IN_USE。
	SceneDirtySuppressionScope sceneDirtyScope(*this);
	m_dx->FlushCommandQueue();
	m_dx->ResetSceneRuntimeRenderState();

	m_ecs->Clear();
	m_ecs->EnsureEnvironmentEntity();
	m_ecs->EnsureDefaultAmbientLightEntity();
	m_ecs->ResetEntitySceneTypeVertexColorsToDefault(false);
	if (sceneTypeColors != nullptr)
	{
		for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
		{
			const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
			m_ecs->SetEntitySceneTypeVertexColor(sceneType, (*sceneTypeColors)[typeIndex], false);
		}
	}
	m_dx->RebuildRenderItemsFromEntities(m_ecs);

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
		m_dx->SetColorAdjustWhiteBalance(defaultRenderSettings.ColorAdjustWhiteBalance);
		m_dx->SetColorAdjustContrast(defaultRenderSettings.ColorAdjustContrast);
		m_dx->SetColorAdjustSaturation(defaultRenderSettings.ColorAdjustSaturation);
		m_dx->SetEnvironmentDiffuseIntensity(defaultRenderSettings.EnvironmentDiffuseIntensity);
		m_dx->SetEnvironmentSpecularIntensity(defaultRenderSettings.EnvironmentSpecularIntensity);
		m_dx->SetEnvironmentBrdfLutEnabled(defaultRenderSettings.EnvironmentBrdfLutEnabled);
	}

	const std::wstring sceneDisplayName = _name.empty() ? DefaultSceneName() : _name;
	ClearScene(sceneDisplayName);
	m_sceneFilePath = BuildUniqueProjectSceneFilePath(sceneDisplayName);
	if (!SaveSceneInternal())
		return false;

	NotifySceneLoaded();
	const std::wstring logText = L"[ProjectSceneSystem] -> 项目场景已新建：" + m_sceneFilePath.wstring();
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::SaveScene()
{
	if (m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，保存场景被阻止。");
		return false;
	}

	return SaveSceneInternal();
}

bool ProjectSceneSystem::SaveSkeletonToModel(SceneEntityBase* entity)
{
	if (m_ecs == nullptr)
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 骨架保存到模型失败：ECS为空。");
		return false;
	}
	SceneEntityBase* ownerEntity = ResolveSkeletonOwnerEntityForSave(m_ecs, entity);
	if (ownerEntity == nullptr || !m_ecs->HasEntity(ownerEntity))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 将骨架保存到模型失败：实体无效。");
		return false;
	}

	const Witchcraft::Animation::SkeletonData* skeletonData = m_ecs->GetSkeletonData(ownerEntity);
	if (skeletonData == nullptr || skeletonData->Topology.Bones.empty())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 将骨架保存到模型失败：实体没有骨架数据。");
		return false;
	}

	std::set<std::wstring> referencedModelPathStrings;
	CollectReferencedWModelPathsRecursive(m_ecs, ownerEntity, &referencedModelPathStrings);
	if (referencedModelPathStrings.empty())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 将骨架保存到模型失败：找不到拥有的.wmodel。");
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
				L"[ProjectSceneSystem] -> 将骨架保存到模型失败：无法加载 " + modelFilePath.wstring();
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
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 将骨架保存到模型失败：缺少主模型数据。");
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
			L"[ProjectSceneSystem] -> 将骨架保存到模型失败：无法创建目录 " + skeletonFilePath.parent_path().wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	WSkeletonFileData skeletonFileData;
	skeletonFileData.Name = primaryModelData.Name.empty() ? primaryModelPath.stem().wstring() : primaryModelData.Name;
	skeletonFileData.Topology = skeletonData->Topology;
	if (!WSkeletonFile::SaveToFile(skeletonFilePath, skeletonFileData))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> 将骨架保存到模型失败：无法保存 " + skeletonFilePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	std::set<std::filesystem::path> modelsToUpdate = ownerModelPaths;
	// 先走“当前骨骼 owner 关联模型”快速路径，避免保存动作扫描全项目造成明显卡顿。
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
			const std::wstring logText = L"[ProjectSceneSystem] -> 将骨架保存到模型失败：无法加载 " + modelFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}

		modelFileData.SkeletonAsset = BuildRelativeAssetPath(skeletonFilePath, modelFilePath.parent_path());
		modelFileData.SkeletonName = skeletonFileData.Name;
		modelFileData.Skeleton = {};
		if (!WModelFile::SaveToFile(modelFilePath, modelFileData))
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> 将骨架保存到模型失败：无法更新 " + modelFilePath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}
		++updatedModelCount;
	}

	const std::wstring projectRelativeSkeletonPath = BuildRelativeAssetPath(skeletonFilePath, projectDirectory);
	(void)m_ecs->SetEntitySkeletonAssetPath(ownerEntity, projectRelativeSkeletonPath);

	const std::wstring logText =
		L"[ProjectSceneSystem] -> 骨架已保存：skeleton=" + skeletonFilePath.wstring() +
		L", updatedModels=" + std::to_wstring(updatedModelCount);
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::ConfirmLeaveCurrentSceneIfNeeded()
{
	return ConfirmSceneSwitchIfNeeded();
}

bool ProjectSceneSystem::CaptureSceneSnapshot(WSceneFileData* outData) const
{
	if (outData == nullptr)
		return false;

	if (!BuildSceneFileData(outData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 捕获场景快照失败：无法生成场景数据。");
		return false;
	}

	return true;
}

bool ProjectSceneSystem::RestoreSceneSnapshot(const WSceneFileData& sceneFileData)
{
	SceneDirtySuppressionScope sceneDirtyScope(*this);
	if (!ApplySceneFileData(sceneFileData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 还原场景快照失败：无法应用场景数据。");
		return false;
	}

	return true;
}

bool ProjectSceneSystem::RestoreSceneSnapshotDiff(const WSceneFileData& sceneFileData)
{
	SceneDirtySuppressionScope sceneDirtyScope(*this);
	if (m_ecs == nullptr || m_dx == nullptr)
		return false;

	WSceneFileData currentSceneData;
	if (!BuildSceneFileData(&currentSceneData))
		return false;

	sceneName = sceneFileData.Meta.Name.empty() ? DefaultSceneName() : sceneFileData.Meta.Name;
	m_sceneCreatedAt = sceneFileData.Meta.CreatedAt;

	std::unordered_map<std::wstring, SceneEntityBase*> currentEntitiesById;
	for (SceneEntityBase* rootEntity : m_ecs->GetSceneRootEntities())
		CollectSerializedSceneEntitiesById(m_ecs, rootEntity, &currentEntitiesById);

	std::unordered_map<std::wstring, std::wstring> modelPathById;
	for (const WSceneModelAssetData& modelData : sceneFileData.Models)
	{
		if (!modelData.Id.empty())
			modelPathById[modelData.Id] = ResolveProjectAssetPath(modelData.Path).wstring();
	}

	std::unordered_map<std::wstring, const WSceneEntityData*> originalEntitiesById;
	for (const WSceneEntityData& entityData : sceneFileData.Entities)
	{
		if (entityData.Id.empty())
			return false;
		originalEntitiesById[entityData.Id] = &entityData;
	}

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
	m_dx->SetColorAdjustWhiteBalance(sceneFileData.RenderSettings.ColorAdjustWhiteBalance);
	m_dx->SetColorAdjustContrast(sceneFileData.RenderSettings.ColorAdjustContrast);
	m_dx->SetColorAdjustSaturation(sceneFileData.RenderSettings.ColorAdjustSaturation);
	m_dx->SetEnvironmentDiffuseIntensity(sceneFileData.RenderSettings.EnvironmentDiffuseIntensity);
	m_dx->SetEnvironmentSpecularIntensity(sceneFileData.RenderSettings.EnvironmentSpecularIntensity);
	m_dx->SetEnvironmentBrdfLutEnabled(sceneFileData.RenderSettings.EnvironmentBrdfLutEnabled);

	m_ecs->ResetEntitySceneTypeVertexColorsToDefault(false);
	for (const WSceneEntityTypeColorData& typeColorData : sceneFileData.EntityTypeColors)
	{
		SceneEntityType sceneType = SceneEntityType::StaticScenery;
		if (!TryParseSceneEntityType(typeColorData.Type, &sceneType))
			continue;
		m_ecs->SetEntitySceneTypeVertexColor(sceneType, typeColorData.Color, false);
	}

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
		if (!m_ecs->SetEntityLightSnapshot(ambientLightEntity, lightSnapshot))
			return false;
		(void)m_ecs->SetEntityVisible(ambientLightEntity, sceneFileData.Environment.AmbientLightActive);
	}

	auto createEntityFromSnapshot = [&](const WSceneEntityData& entityData, SceneEntityBase* parent, flecs::entity_t desiredEntityId) -> SceneEntityBase*
	{
		const std::wstring entityName = entityData.Name.empty() ? L"Entity" : entityData.Name;
		SceneEntityBase* entity = nullptr;
		if (entityData.HasMesh)
			entity = m_ecs->CreateMeshEntityWithId(entityName, parent, desiredEntityId);
		else if (entityData.HasCamera)
			entity = m_ecs->CreateCameraEntityWithId(entityName, parent, desiredEntityId);
		else if (entityData.HasLight)
			entity = m_ecs->CreateLightEntityWithId(entityName, parent, desiredEntityId);
		else
			entity = m_ecs->CreateBasicEntityWithId(entityName, parent, ComponentType::Co_Unk, desiredEntityId);

		if (entity == nullptr)
			return nullptr;

		if (entityData.HasSkeleton && m_ecs->GetComponent<SkeletonComponent>(entity) == nullptr)
			(void)m_ecs->AddSkeletonComponent(entity);
		if (entityData.HasAnimator && m_ecs->GetComponent<AnimatorComponent>(entity) == nullptr)
			(void)m_ecs->AddAnimatorComponent(entity);
		if (entityData.HasSkinningRuntime && m_ecs->GetComponent<SkinningRuntimeComponent>(entity) == nullptr)
			(void)m_ecs->AddSkinningRuntimeComponent(entity);

		return entity;
	};

	auto ensureEntityFromSnapshot = [&](auto&& self, const std::wstring& entityId) -> SceneEntityBase*
	{
		auto currentIt = currentEntitiesById.find(entityId);
		if (currentIt != currentEntitiesById.end())
			return currentIt->second;

		auto originalIt = originalEntitiesById.find(entityId);
		if (originalIt == originalEntitiesById.end() || originalIt->second == nullptr)
			return nullptr;

		const WSceneEntityData& snapshotEntity = *originalIt->second;
		SceneEntityBase* parent = nullptr;
		if (!snapshotEntity.ParentId.empty())
		{
			parent = self(self, snapshotEntity.ParentId);
			if (parent == nullptr)
				return nullptr;
		}

		SceneEntityBase* createdEntity = createEntityFromSnapshot(snapshotEntity, parent, ParseEntityIdText(snapshotEntity.Id));
		if (createdEntity == nullptr)
			return nullptr;

		currentEntitiesById[snapshotEntity.Id] = createdEntity;
		return createdEntity;
	};

	for (const WSceneEntityData& entityData : sceneFileData.Entities)
	{
		SceneEntityBase* entity = ensureEntityFromSnapshot(ensureEntityFromSnapshot, entityData.Id);
		if (entity == nullptr)
			return false;

		SceneEntityBase* expectedParent = nullptr;
		if (!entityData.ParentId.empty())
		{
			expectedParent = ensureEntityFromSnapshot(ensureEntityFromSnapshot, entityData.ParentId);
			if (expectedParent == nullptr)
				return false;
		}

		if (m_ecs->GetParentEntity(entity) != expectedParent)
		{
			if (!m_ecs->ReparentEntityInHierarchy(entity, expectedParent))
				return false;
		}

		if (!m_ecs->SetEntityVisible(entity, entityData.Active))
			return false;
		if (!m_ecs->SetEntityEditableLocalTransform(entity, entityData.LocalTransform))
			return false;

		SceneEntityType sceneType = InferSceneEntityTypeFromEntityData(entityData);
		(void)m_ecs->SetEntitySceneType(entity, sceneType, true);

		LightComponent* currentLightComponent = m_ecs->GetComponent<LightComponent>(entity);
		CameraComponent* currentCameraComponent = m_ecs->GetComponent<CameraComponent>(entity);
		AnimatorComponent* currentAnimatorComponent = m_ecs->GetComponent<AnimatorComponent>(entity);
		ScriptingComponent* currentScriptingComponent = m_ecs->GetComponent<ScriptingComponent>(entity);
		MeshComponent* currentMeshComponent = m_ecs->GetComponent<MeshComponent>(entity);
		SkeletonComponent* currentSkeletonComponent = m_ecs->GetComponent<SkeletonComponent>(entity);
		SkinningRuntimeComponent* currentSkinningRuntimeComponent = m_ecs->GetComponent<SkinningRuntimeComponent>(entity);

		if (entityData.HasLight && currentLightComponent == nullptr)
			currentLightComponent = m_ecs->AddLightComponent(entity);
		else if (!entityData.HasLight && currentLightComponent != nullptr)
		{
			if (!m_ecs->RemoveLightComponent(entity))
				return false;
			currentLightComponent = nullptr;
		}

		if (entityData.HasCamera && currentCameraComponent == nullptr)
			currentCameraComponent = m_ecs->AddCameraComponent(entity);
		else if (!entityData.HasCamera && currentCameraComponent != nullptr)
		{
			if (!m_ecs->RemoveCameraComponentFromEntity(entity))
				return false;
			currentCameraComponent = nullptr;
		}

		if (entityData.HasAnimator && currentAnimatorComponent == nullptr)
			currentAnimatorComponent = m_ecs->AddAnimatorComponent(entity);
		else if (!entityData.HasAnimator && currentAnimatorComponent != nullptr)
		{
			if (!m_ecs->RemoveAnimatorComponent(entity))
				return false;
			currentAnimatorComponent = nullptr;
		}

		if (entityData.HasMesh && currentMeshComponent == nullptr)
			currentMeshComponent = m_ecs->AddMeshComponent(entity);
		else if (!entityData.HasMesh && currentMeshComponent != nullptr)
		{
			if (!m_ecs->RemoveMeshComponentFromEntity(entity))
				return false;
			currentMeshComponent = nullptr;
		}

		if (entityData.HasSkeleton && currentSkeletonComponent == nullptr)
			currentSkeletonComponent = m_ecs->AddSkeletonComponent(entity);
		else if (!entityData.HasSkeleton && currentSkeletonComponent != nullptr)
		{
			if (!m_ecs->RemoveSkeletonComponent(entity))
				return false;
			currentSkeletonComponent = nullptr;
		}

		if (entityData.HasSkinningRuntime && currentSkinningRuntimeComponent == nullptr)
			currentSkinningRuntimeComponent = m_ecs->AddSkinningRuntimeComponent(entity);
		else if (!entityData.HasSkinningRuntime && currentSkinningRuntimeComponent != nullptr)
		{
			if (!m_ecs->RemoveSkinningRuntimeComponent(entity))
				return false;
			currentSkinningRuntimeComponent = nullptr;
		}

		if (entityData.HasScripting)
		{
			if (!ApplyScriptingComponentData(m_ecs, entity, entityData.Scripting))
				return false;
			currentScriptingComponent = m_ecs->GetComponent<ScriptingComponent>(entity);
		}
		else if (currentScriptingComponent != nullptr)
		{
			if (!m_ecs->RemoveScriptingComponent(entity))
				return false;
			currentScriptingComponent = nullptr;
		}

		if (entityData.HasLight)
		{
			EntityLightComponentData lightSnapshot{};
			const LightKind lightKind = ResolveNormalizedSceneLightKind(entityData.Light);
			lightSnapshot.kind = static_cast<std::uint32_t>(lightKind);
			lightSnapshot.type = ResolveSceneLightTypeFromKind(lightKind, entityData.Light.Type);
			lightSnapshot.color = entityData.Light.Color;
			lightSnapshot.power = entityData.Light.Power;
			lightSnapshot.castShadow = lightKind != LightKind::Ambient && entityData.Light.CastShadow;
			lightSnapshot.enableVolumetric = lightKind != LightKind::Ambient && entityData.Light.EnableVolumetric;
			lightSnapshot.volumetricIntensity = lightKind != LightKind::Ambient ? entityData.Light.VolumetricIntensity : 0.0f;
			lightSnapshot.volumetricAttenuationDistance = lightKind != LightKind::Ambient ? entityData.Light.VolumetricAttenuationDistance : 0.0f;
			if (!m_ecs->SetEntityLightSnapshot(entity, lightSnapshot))
				return false;
		}

		if (entityData.HasCamera)
		{
			EntityCameraComponentData cameraSnapshot{};
			cameraSnapshot.primary = entityData.Camera.Primary;
			cameraSnapshot.renderEnabled = entityData.Camera.RenderEnabled;
			cameraSnapshot.renderToTextureEnabled = entityData.Camera.RenderToTextureEnabled;
			cameraSnapshot.nearZ = entityData.Camera.NearZ;
			cameraSnapshot.farZ = entityData.Camera.FarZ;
			cameraSnapshot.fovY = entityData.Camera.FovY;
			cameraSnapshot.viewportScale = entityData.Camera.ViewportScale;
			cameraSnapshot.outputTargetId = entityData.Camera.OutputTargetId;
			if (!m_ecs->SetEntityCameraSnapshot(entity, cameraSnapshot))
				return false;
			(void)m_ecs->SetCameraEntityEngine(entity, m_engine);
		}

		if (entityData.HasAnimator)
		{
			if (currentAnimatorComponent == nullptr)
				return false;
			ApplyAnimatorComponentData(entityData.Animator, currentAnimatorComponent);
		}

		if (entityData.HasMesh)
		{
			if (currentMeshComponent == nullptr)
				return false;
			currentMeshComponent->SetEngine(m_engine);
			currentMeshComponent->SetRenderLayerIndex(entityData.Mesh.RenderLayer);
			currentMeshComponent->SetMeshName(entityData.Mesh.MeshName.empty() ? entityData.Name : entityData.Mesh.MeshName);
			currentMeshComponent->SetGeometryName(entityData.Mesh.GeometryRef);
			currentMeshComponent->SetDefaultMaterialName(entityData.Mesh.MaterialName);
			currentMeshComponent->SetFileName(entityData.Mesh.ModelRef.empty() ? std::wstring() : modelPathById[entityData.Mesh.ModelRef]);

			if (entityData.Mesh.RenderSourceType == L"Sky")
			{
				const std::wstring resolvedSkyTexturePath = entityData.Mesh.SkyTexturePath.empty()
					? std::wstring(kDefaultSkyTexturePath)
					: entityData.Mesh.SkyTexturePath;
				const std::wstring skyMaterialName = m_dx->GetOrCreateSkyMaterial(resolvedSkyTexturePath);
				if (!skyMaterialName.empty())
					currentMeshComponent->SetDefaultMaterialName(skyMaterialName);
			}
			else if (!entityData.Mesh.MaterialFile.empty())
			{
				const std::filesystem::path materialFilePath = ResolveProjectAssetPath(entityData.Mesh.MaterialFile);
				if (std::filesystem::exists(materialFilePath))
				{
					const std::wstring materialName = m_dx->GetOrCreateMaterialFromWMaterialFile(materialFilePath);
					if (!materialName.empty())
						currentMeshComponent->SetDefaultMaterialName(materialName);
				}
			}
		}
	}

	auto computeHierarchyDepth = [&](SceneEntityBase* entity) -> std::size_t
	{
		std::size_t depth = 0;
		for (SceneEntityBase* current = entity; current != nullptr; current = m_ecs->GetParentEntity(current))
			++depth;
		return depth;
	};

	struct ExtraEntityRecord
	{
		SceneEntityBase* Entity = nullptr;
		std::size_t Depth = 0;
	};

	std::vector<ExtraEntityRecord> extraEntities;
	for (const auto& currentEntityPair : currentEntitiesById)
	{
		if (originalEntitiesById.find(currentEntityPair.first) != originalEntitiesById.end())
			continue;

		SceneEntityBase* entity = currentEntityPair.second;
		if (entity == nullptr)
			continue;

		extraEntities.push_back({ entity, computeHierarchyDepth(entity) });
	}

	std::sort(
		extraEntities.begin(),
		extraEntities.end(),
		[](const ExtraEntityRecord& lhs, const ExtraEntityRecord& rhs)
		{
			return lhs.Depth > rhs.Depth;
		});

	for (const ExtraEntityRecord& extraEntityRecord : extraEntities)
	{
		SceneEntityBase* extraEntity = extraEntityRecord.Entity;
		if (extraEntity == nullptr || !m_ecs->HasEntity(extraEntity))
			continue;

		if (!m_ecs->DestroyEntity(extraEntity, false))
			return false;
	}

	for (const WSceneEntityData& entityData : sceneFileData.Entities)
	{
		auto entityIt = currentEntitiesById.find(entityData.Id);
		if (entityIt == currentEntitiesById.end() || entityIt->second == nullptr)
			continue;

		SceneEntityBase* entity = entityIt->second;
		if (!entityData.Name.empty() && m_ecs->GetEntityName(entity) != entityData.Name)
		{
			if (!m_ecs->RenameEntity(entity, entityData.Name))
				return false;
		}
	}

	m_dx->RebuildRenderItemsFromEntities(m_ecs);
	return true;
}

bool ProjectSceneSystem::SaveSceneInternal()
{
	if (m_ecs == nullptr)
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 保存场景失败：ECS为空。");
		return false;
	}

	WSceneFileData sceneFileData;
	if (!BuildSceneFileData(&sceneFileData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 保存场景失败：无法生成场景数据。");
		return false;
	}

	const std::filesystem::path sceneFilePath = BuildDefaultSceneFilePath();
	if (!WSceneFile::SaveToFile(sceneFilePath, sceneFileData))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> 保存场景失败：" + sceneFilePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	sceneName = sceneFileData.Meta.Name;
	m_sceneCreatedAt = sceneFileData.Meta.CreatedAt;
	m_sceneFilePath = sceneFilePath;
	CommitSceneStateFromData(sceneFileData);
	ClearCurrentSceneDirty();
	CommitCurrentProjectState();
	MarkProjectDirty();

	const std::wstring logText = L"[ProjectSceneSystem] -> 场景已保存：" + sceneFilePath.wstring();
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::BuildProjectFileData(WProjectFileData* outData) const
{
	if (outData == nullptr)
		return false;

	WProjectFileData projectFileData = m_projectFileData;
	if (projectFileData.Meta.Name.empty())
		projectFileData.Meta.Name = kDefaultProjectName;
	projectFileData.Meta.UpdatedAt = BuildCurrentTimestampText();

	const std::filesystem::path projectDirectory = m_projectFilePath.empty()
		? std::filesystem::path(EngineUtils::GetProjectDirPath())
		: m_projectFilePath.parent_path();

	const std::wstring currentScenePathText = m_sceneFilePath.empty()
		? std::wstring()
		: BuildProjectRelativePathText(m_sceneFilePath, projectDirectory);

	projectFileData.Settings.CurrentScenePath = currentScenePathText;
	if (currentScenePathText.empty())
	{
		projectFileData.EditorState.CurrentSceneId.clear();
		*outData = std::move(projectFileData);
		return true;
	}

	if (projectFileData.Settings.DefaultScenePath.empty())
		projectFileData.Settings.DefaultScenePath = currentScenePathText;

	std::size_t currentSceneIndex = static_cast<std::size_t>(-1);
	for (std::size_t index = 0; index < projectFileData.Scenes.size(); ++index)
	{
		const WProjectSceneData& scene = projectFileData.Scenes[index];
		if (!currentScenePathText.empty() && scene.Path == currentScenePathText)
		{
			currentSceneIndex = index;
			break;
		}
	}

	if (currentSceneIndex == static_cast<std::size_t>(-1))
	{
		WProjectSceneData currentScene;
		currentScene.Id = BuildNextProjectSceneId(projectFileData.Scenes);
		currentScene.Name = sceneName.empty() ? DefaultSceneName() : sceneName;
		currentScene.Path = currentScenePathText;
		currentScene.Entry = projectFileData.Scenes.empty();
		projectFileData.Scenes.push_back(std::move(currentScene));
		currentSceneIndex = projectFileData.Scenes.size() - 1;
	}
	else
	{
		WProjectSceneData& currentScene = projectFileData.Scenes[currentSceneIndex];
		if (currentScene.Id.empty())
			currentScene.Id = BuildNextProjectSceneId(projectFileData.Scenes);
		currentScene.Name = sceneName.empty() ? DefaultSceneName() : sceneName;
		currentScene.Path = currentScenePathText;
	}

	projectFileData.EditorState.CurrentSceneId = projectFileData.Scenes[currentSceneIndex].Id;

	*outData = std::move(projectFileData);
	return true;
}

bool ProjectSceneSystem::LoadProjectFileData(const std::filesystem::path& path)
{
	WProjectFileData projectFileData;
	if (!WProjectFile::LoadFromFile(path, &projectFileData))
	{
		AddConsoleError(m_engine, L"[ProjectSceneSystem] -> 项目文件读取失败：" + path.wstring());
		return false;
	}

	const std::filesystem::path previousProjectFilePath = m_projectFilePath;
	const std::filesystem::path previousProjectDirPath = EngineUtils::GetProjectDirPath();
	m_projectFilePath = path.lexically_normal();
	EngineUtils::SetProjectDirPath(m_projectFilePath.parent_path());
	if (!ApplyProjectFileData(projectFileData))
	{
		AddConsoleError(m_engine, L"[ProjectSceneSystem] -> 项目入口场景加载失败：" + path.wstring());
		m_projectFilePath = previousProjectFilePath;
		if (m_projectFilePath.empty())
			EngineUtils::ClearProjectDirPath();
		else
			EngineUtils::SetProjectDirPath(previousProjectDirPath);
		return false;
	}

	if (!m_sceneFilePath.empty())
	{
		const std::filesystem::path projectDirectory = m_projectFilePath.parent_path();
		const std::wstring currentScenePathText = BuildProjectRelativePathText(m_sceneFilePath, projectDirectory);
		projectFileData.Settings.CurrentScenePath = currentScenePathText;
		projectFileData.EditorState.CurrentSceneId.clear();
		for (const WProjectSceneData& sceneData : projectFileData.Scenes)
		{
			if (ResolveProjectRelativePath(sceneData.Path, projectDirectory) == m_sceneFilePath.lexically_normal())
			{
				projectFileData.EditorState.CurrentSceneId = sceneData.Id;
				break;
			}
		}
	}

	m_projectFileData = std::move(projectFileData);
	ClearProjectDirty();
	if (m_projectFileChangedCallback)
		m_projectFileChangedCallback(m_projectFilePath);

	const std::wstring logText = L"[ProjectSceneSystem] -> 项目已加载：" + m_projectFilePath.wstring();
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::ApplyProjectFileData(const WProjectFileData& projectFileData)
{
	SceneDirtySuppressionScope sceneDirtyScope(*this);
	const std::filesystem::path projectDirectory = m_projectFilePath.empty()
		? std::filesystem::path(EngineUtils::GetProjectDirPath())
		: m_projectFilePath.parent_path();

	std::wstring activeScenePathText;
	for (const WProjectSceneData& sceneData : projectFileData.Scenes)
	{
		if (sceneData.Entry)
		{
			activeScenePathText = sceneData.Path;
			break;
		}
	}
	if (activeScenePathText.empty())
	{
		activeScenePathText =
			!projectFileData.Settings.CurrentScenePath.empty()
			? projectFileData.Settings.CurrentScenePath
			: projectFileData.Settings.DefaultScenePath;
	}

	std::filesystem::path activeScenePath = ResolveProjectRelativePath(activeScenePathText, projectDirectory);
	if (activeScenePath.empty() && !projectFileData.Scenes.empty())
		activeScenePath = ResolveProjectRelativePath(projectFileData.Scenes.front().Path, projectDirectory);

	if (activeScenePath.empty())
	{
		if (!projectFileData.Scenes.empty() || !activeScenePathText.empty())
			AddConsoleError(m_engine, L"[ProjectSceneSystem] -> 项目没有可加载的场景路径。");
		return true;
	}

	if (!std::filesystem::exists(activeScenePath))
	{
		AddConsoleError(m_engine, L"[ProjectSceneSystem] -> 项目场景文件不存在：" + activeScenePath.wstring());
		return false;
	}

	return LoadSceneFileData(activeScenePath);
}

void ProjectSceneSystem::MarkProjectDirty()
{
	if (!m_projectFilePath.empty())
		m_projectDirty = true;
}

void ProjectSceneSystem::ClearProjectDirty()
{
	m_projectDirty = false;
}

void ProjectSceneSystem::CommitCurrentProjectState()
{
	WProjectFileData projectFileData;
	if (!BuildProjectFileData(&projectFileData))
		return;

	m_projectFileData = std::move(projectFileData);
}

bool ProjectSceneSystem::OpenProject()
{
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，打开项目被阻止。");
		return false;
	}

	if (!ConfirmSceneSwitchIfNeeded())
		return false;

	std::wstring projectPath;
	const std::wstring projectDirectory = EngineUtils::GetProjectDirPath();
	if (!EngineHelpers::TryOpenFileDialog(
		m_dx->GetHwnd(),
		projectDirectory.c_str(),
		L"Witchcraft 项目 (*.wproject)\0*.wproject\0所有文件 (*.*)\0*.*\0\0",
		L"打开项目",
		&projectPath))
	{
		return false;
	}

	return OpenProjectFromPath(std::filesystem::path(projectPath));
}

bool ProjectSceneSystem::OpenProjectFromPath(const std::filesystem::path& projectFilePath)
{
	const std::filesystem::path normalizedPath = projectFilePath.lexically_normal();
	return LoadProjectFileData(normalizedPath);
}

bool ProjectSceneSystem::SaveProject()
{
	if (m_projectFilePath.empty())
	{
		std::wstring projectPath;
		const std::wstring projectDirectory = EngineUtils::GetProjectDirPath();
		if (!EngineHelpers::TrySaveFileDialog(
			m_dx->GetHwnd(),
			projectDirectory.c_str(),
			L"Witchcraft 项目 (*.wproject)\0*.wproject\0所有文件 (*.*)\0*.*\0\0",
			L"保存项目",
			L"wproject",
			&projectPath))
		{
			return false;
		}
		m_projectFilePath = std::filesystem::path(projectPath);
		EngineUtils::SetProjectDirPath(m_projectFilePath.parent_path());
	}

	WProjectFileData projectFileData;
	if (!BuildProjectFileData(&projectFileData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 保存项目失败：无法生成项目数据。");
		return false;
	}

	if (!WProjectFile::SaveToFile(m_projectFilePath, projectFileData))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> 保存项目失败：" + m_projectFilePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	m_projectFileData = std::move(projectFileData);
	const std::wstring logText = L"[ProjectSceneSystem] -> 项目已保存：" + m_projectFilePath.wstring();
	EngineHelpers::AddLog(logText.c_str());
	ClearProjectDirty();
	if (m_projectFileChangedCallback)
		m_projectFileChangedCallback(m_projectFilePath);
	return true;
}

bool ProjectSceneSystem::RenameProject(const std::wstring& newName)
{
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，重命名项目被阻止。");
		return false;
	}

	if (m_projectFilePath.empty())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 请先新建或打开项目，再重命名项目。");
		return false;
	}

	const std::wstring trimmedName = newName;
	std::wstring projectStem = SanitizeSceneStem(trimmedName);
	if (projectStem.empty())
		projectStem = kDefaultProjectName;

	const std::filesystem::path projectDirectory = m_projectFilePath.parent_path();
	const std::filesystem::path targetPath = (projectDirectory / (projectStem + WProjectFile::Extension)).lexically_normal();
	const std::filesystem::path currentPath = m_projectFilePath.lexically_normal();
	if (targetPath != currentPath)
	{
		std::error_code existsError;
		if (std::filesystem::exists(targetPath, existsError))
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> 重命名项目失败，目标文件已存在：" + targetPath.wstring();
			EngineHelpers::AddLog(logText.c_str());
			return false;
		}
	}

	WProjectFileData projectFileData;
	if (!BuildProjectFileData(&projectFileData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 重命名项目失败：无法生成项目数据。");
		return false;
	}

	projectFileData.Meta.Name = trimmedName.empty() ? projectStem : trimmedName;
	if (!WProjectFile::SaveToFile(targetPath, projectFileData))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> 重命名项目失败，无法保存：" + targetPath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	if (targetPath != currentPath)
	{
		std::error_code removeError;
		std::filesystem::remove(currentPath, removeError);
		if (removeError)
		{
			const std::wstring logText = L"[ProjectSceneSystem] -> 项目已重命名，但旧文件删除失败：" + currentPath.wstring();
			EngineHelpers::AddLog(logText.c_str());
		}
	}

	m_projectFilePath = targetPath;
	EngineUtils::SetProjectDirPath(m_projectFilePath.parent_path());
	m_projectFileData = std::move(projectFileData);
	ClearProjectDirty();
	if (m_projectFileChangedCallback)
		m_projectFileChangedCallback(m_projectFilePath);

	const std::wstring logText = L"[ProjectSceneSystem] -> 项目已重命名为：" + m_projectFilePath.wstring();
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::AddProjectSceneFromFile()
{
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，添加项目场景被阻止。");
		return false;
	}

	if (m_projectFilePath.empty())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 请先新建或打开项目，再添加项目场景。");
		return false;
	}

	std::filesystem::path selectedScenePath;
	if (!TrySelectSceneFilePath(m_dx != nullptr ? m_dx->GetHwnd() : nullptr, &selectedScenePath))
		return false;

	selectedScenePath = selectedScenePath.lexically_normal();
	if (selectedScenePath.empty() || !std::filesystem::exists(selectedScenePath))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> 添加项目场景失败，文件不存在：" + selectedScenePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	const std::filesystem::path projectDirectory = m_projectFilePath.parent_path();
	const std::wstring scenePathText = BuildProjectRelativePathText(selectedScenePath, projectDirectory);
	const std::filesystem::path normalizedSelectedPath = ResolveProjectRelativePath(scenePathText, projectDirectory);
	for (const WProjectSceneData& sceneData : m_projectFileData.Scenes)
	{
		if (ResolveProjectRelativePath(sceneData.Path, projectDirectory) == normalizedSelectedPath)
		{
			EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 项目场景已存在，跳过添加。");
			return true;
		}
	}

	WSceneFileData sceneFileData;
	const bool sceneFileLoaded = WSceneFile::LoadFromFile(selectedScenePath, &sceneFileData);

	WProjectSceneData projectSceneData;
	projectSceneData.Id = BuildNextProjectSceneId(m_projectFileData.Scenes);
	projectSceneData.Name = sceneFileLoaded && !sceneFileData.Meta.Name.empty()
		? sceneFileData.Meta.Name
		: selectedScenePath.stem().wstring();
	projectSceneData.Path = scenePathText;
	projectSceneData.Entry = m_projectFileData.Scenes.empty();

	if (projectSceneData.Entry)
		m_projectFileData.Settings.DefaultScenePath = projectSceneData.Path;

	m_projectFileData.Scenes.push_back(std::move(projectSceneData));
	MarkProjectDirty();

	const std::wstring logText = L"[ProjectSceneSystem] -> 项目场景已添加：" + scenePathText;
	EngineHelpers::AddLog(logText.c_str());
	return true;
}

bool ProjectSceneSystem::OpenScene()
{
	if (m_engine != nullptr && m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 播放模式处于活动状态时，打开场景被阻止。");
		return false;
	}

	if (!ConfirmSceneSwitchIfNeeded())
		return false;

	std::filesystem::path sceneFilePath;
	if (!TrySelectSceneFilePath(m_dx->GetHwnd(), &sceneFilePath))
		return false;

	if (!LoadSceneFileData(sceneFilePath))
	{
		const std::wstring logText = L"[ProjectSceneSystem] -> 打开场景失败：" + sceneFilePath.wstring();
		EngineHelpers::AddLog(logText.c_str());
		return false;
	}

	return true;
}

bool ProjectSceneSystem::ReloadCurrentScene()
{
	if (m_engine->IsPlayModeActive())
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 当播放模式处于活动状态时，重新加载当前场景被阻止。");
		return false;
	}

	WSceneFileData sceneFileData;
	if (!BuildSceneFileData(&sceneFileData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 重新加载当前场景失败：无法生成场景数据。");
		return false;
	}

	if (!ApplySceneFileData(sceneFileData))
	{
		EngineHelpers::AddLog(L"[ProjectSceneSystem] -> 重新加载当前场景失败：无法应用场景数据。");
		return false;
	}

	NotifySceneLoaded();
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

	const int dialogResult = EngineHelpers::ShowMessageBox(
		m_dx->GetHwnd(),
		L"当前场景有未保存修改，是否先保存当前场景？",
		L"场景未保存",
		MB_ICONQUESTION | MB_YESNOCANCEL);

	if (dialogResult == IDYES)
		return SaveSceneInternal();
	if (dialogResult == IDNO)
		return true;

	return false;
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
	ClearCurrentSceneDirty();
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
		ClearCurrentSceneDirty();
		return;
	}

	m_committedSceneStateToken = std::move(currentSceneStateToken);
	m_hasCommittedSceneState = true;
	ClearCurrentSceneDirty();
}

bool ProjectSceneSystem::BuildSceneFileData(WSceneFileData* outData) const
{
	if (outData == nullptr || m_ecs == nullptr)
		return false;

	WSceneFileData sceneFileData;
	sceneFileData.Meta.Name = sceneName.empty() ? DefaultSceneName() : sceneName;
	sceneFileData.Meta.CreatedAt = m_sceneCreatedAt.empty() ? BuildCurrentTimestampText() : m_sceneCreatedAt;
	sceneFileData.Meta.UpdatedAt = BuildCurrentTimestampText();

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
		sceneFileData.RenderSettings.ColorAdjustWhiteBalance = m_dx->GetColorAdjustWhiteBalance();
		sceneFileData.RenderSettings.ColorAdjustContrast = m_dx->GetColorAdjustContrast();
		sceneFileData.RenderSettings.ColorAdjustSaturation = m_dx->GetColorAdjustSaturation();
		sceneFileData.RenderSettings.EnvironmentDiffuseIntensity = m_dx->GetEnvironmentDiffuseIntensity();
		sceneFileData.RenderSettings.EnvironmentSpecularIntensity = m_dx->GetEnvironmentSpecularIntensity();
		sceneFileData.RenderSettings.EnvironmentBrdfLutEnabled = m_dx->IsEnvironmentBrdfLutEnabled();
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
		if (!meshData.MaterialName.empty())
		{
			const std::wstring materialFilePath = m_dx->GetMaterialFilePathByMaterialName(meshData.MaterialName);
			meshData.MaterialFile = BuildProjectAssetReferencePath(materialFilePath);
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
				modelAssetData.Path = BuildProjectAssetReferencePath(modelPath);
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
		if (animComp != nullptr)
			AppendAnimatorComponentData(*animComp, &entityData.Animator);
		entityData.HasSkinningRuntime = (skinComp != nullptr);
		AppendScriptingComponentData(m_ecs, entity, &entityData.Scripting);
		entityData.HasScripting = !entityData.Scripting.Scripts.empty();
	}
	outData->Entities.push_back(std::move(entityData));

	for (SceneEntityBase* childEntity : m_ecs->GetSceneChildren(entity))
		AppendSceneEntityData(childEntity, entity, outData, modelIdByPath, nextModelIndex);
}

bool ProjectSceneSystem::LoadSceneFileData(const std::filesystem::path& path)
{
	SceneDirtySuppressionScope sceneDirtyScope(*this);
	WSceneFileData sceneFileData;
	if (!WSceneFile::LoadFromFile(path, &sceneFileData))
	{
		AddConsoleError(m_engine, L"[ProjectSceneSystem] -> 场景文件读取失败：" + path.wstring());
		return false;
	}

	if (!ApplySceneFileData(sceneFileData))
	{
		AddConsoleError(m_engine, L"[ProjectSceneSystem] -> 场景文件应用失败：" + path.wstring());
		return false;
	}

	m_sceneFilePath = path;
	CommitSceneStateFromData(sceneFileData);
	ClearCurrentSceneDirty();
	if (m_engine != nullptr && m_engine->GetConsoleWindow() != nullptr)
	{
		const std::wstring logText =
			L"[ProjectSceneSystem] -> 场景已加载：" + sceneFileData.Meta.Name +
			L" (" + path.wstring() + L")";
		m_engine->GetConsoleWindow()->AddInfoMessage(logText.c_str());
	}
	else
	{
		const std::wstring logText =
			L"[ProjectSceneSystem] -> 场景已加载：" + sceneFileData.Meta.Name +
			L" (" + path.wstring() + L")";
		EngineHelpers::AddLog(logText.c_str());
	}
	CommitCurrentProjectState();
	MarkProjectDirty();
	NotifySceneLoaded();
	return true;
}

bool ProjectSceneSystem::ApplySceneFileData(const WSceneFileData& sceneFileData)
{
	SceneDirtySuppressionScope sceneDirtyScope(*this);
	if (m_ecs == nullptr)
		return false;

	// 打开场景文件前先确保上一场景对应的命令列表已经彻底执行完成。
	// 这样后续 m_ecs->Clear() 中销毁 MeshComponent / Geometry 时，
	// 就不会删除仍被 normalThreadCommandLists 等列表引用的资源。
	m_dx->FlushCommandQueue();
	m_dx->ResetSceneRuntimeRenderState();

	m_ecs->Clear();
	m_dx->RebuildRenderItemsFromEntities(m_ecs);

	sceneName = sceneFileData.Meta.Name.empty() ? DefaultSceneName() : sceneFileData.Meta.Name;
	m_sceneCreatedAt = sceneFileData.Meta.CreatedAt;

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
		m_dx->SetColorAdjustWhiteBalance(sceneFileData.RenderSettings.ColorAdjustWhiteBalance);
		m_dx->SetColorAdjustContrast(sceneFileData.RenderSettings.ColorAdjustContrast);
		m_dx->SetColorAdjustSaturation(sceneFileData.RenderSettings.ColorAdjustSaturation);
		m_dx->SetEnvironmentDiffuseIntensity(sceneFileData.RenderSettings.EnvironmentDiffuseIntensity);
		m_dx->SetEnvironmentSpecularIntensity(sceneFileData.RenderSettings.EnvironmentSpecularIntensity);
		m_dx->SetEnvironmentBrdfLutEnabled(sceneFileData.RenderSettings.EnvironmentBrdfLutEnabled);
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
			modelPathById[modelData.Id] = ResolveProjectAssetPath(modelData.Path).wstring();
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

		ScriptingComponent* currentScriptingComponent = m_ecs->GetComponent<ScriptingComponent>(entity);
		if (entityData.HasScripting)
		{
			if (!ApplyScriptingComponentData(m_ecs, entity, entityData.Scripting))
				return false;
			currentScriptingComponent = m_ecs->GetComponent<ScriptingComponent>(entity);
		}
		else if (currentScriptingComponent != nullptr)
		{
			if (!m_ecs->RemoveScriptingComponent(entity))
				return false;
			currentScriptingComponent = nullptr;
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
		{
			AnimatorComponent* animatorComponent = m_ecs->AddComponent<AnimatorComponent>(entity);
			ApplyAnimatorComponentData(entityData.Animator, animatorComponent);
		}
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
		if (renderSourceType == L"Sky")
		{
			const std::wstring skyTexturePath = entityData.Mesh.SkyTexturePath.empty()
				? std::wstring(kDefaultSkyTexturePath)
				: entityData.Mesh.SkyTexturePath;
			const std::wstring SkyMaterialName = m_dx->GetOrCreateSkyMaterial(skyTexturePath);
			if (!SkyMaterialName.empty())
				resolvedMaterialName = SkyMaterialName;
		}

		if (!entityData.Mesh.MaterialFile.empty())
		{
			const std::filesystem::path materialFilePath = ResolveProjectAssetPath(entityData.Mesh.MaterialFile);
			if (std::filesystem::exists(materialFilePath))
			{
				const std::wstring MaterialName = m_dx->GetOrCreateMaterialFromWMaterialFile(materialFilePath);
				if (!MaterialName.empty())
					resolvedMaterialName = MaterialName;
			}
		}

		bool restoredGeometry = false;
		// ExternalGeometry仅对没有源模型的场景拥有的几何体稳定。
		// 导入的.wmodel网格用于在此处序列化其瞬态GPU名称；
		// 在ECS清除该名称后，它仍然可以解析为过时的D3D条目。
		// 模型支持的实体必须始终从其资产重建。
		const bool canReuseExternalGeometry =
			renderSourceType == L"Sky" ||
			(renderSourceType == L"ExternalGeometry" && modelPath.empty());
		if (canReuseExternalGeometry)
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
						if ((resolvedMaterialName.empty() || (m_dx->GetMaterialByMaterialName(resolvedMaterialName) == nullptr))
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
							if (hasCompleteSkinning)
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
							if (!restoredGeometry)
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

	m_dx->RebuildRenderItemsFromEntities(m_ecs);

	ClearCurrentSceneDirty();
	return true;
}

std::filesystem::path ProjectSceneSystem::BuildDefaultSceneFilePath() const
{
	if (!m_sceneFilePath.empty())
		return m_sceneFilePath;

	const std::wstring sceneStem = SanitizeSceneStem(sceneName);
	return std::filesystem::path(EngineUtils::GetProjectDirPath()) / (sceneStem + WSceneFile::Extension);
}

std::filesystem::path ProjectSceneSystem::BuildUniqueProjectSceneFilePath(const std::wstring& requestedSceneName) const
{
	const std::filesystem::path projectDirectory = !m_projectFilePath.empty()
		? m_projectFilePath.parent_path()
		: std::filesystem::path(EngineUtils::GetProjectDirPath());
	const std::filesystem::path sceneDirectory = (projectDirectory / L"Scenes").lexically_normal();
	std::error_code createDirectoryError;
	std::filesystem::create_directories(sceneDirectory, createDirectoryError);

	std::wstring sceneStem = SanitizeSceneStem(requestedSceneName.empty() ? DefaultSceneName() : requestedSceneName);
	if (sceneStem.empty())
		sceneStem = DefaultSceneName();

	std::filesystem::path candidate = (sceneDirectory / (sceneStem + WSceneFile::Extension)).lexically_normal();
	std::error_code existsError;
	if (!std::filesystem::exists(candidate, existsError))
		return candidate;

	for (std::uint32_t suffix = 1; suffix < 100000; ++suffix)
	{
		candidate = (sceneDirectory / (sceneStem + L"_" + std::to_wstring(suffix) + WSceneFile::Extension)).lexically_normal();
		existsError.clear();
		if (!std::filesystem::exists(candidate, existsError))
			return candidate;
	}

	return (sceneDirectory / (sceneStem + L"_" + SanitizeSceneStem(BuildCurrentTimestampText()) + WSceneFile::Extension)).lexically_normal();
}

std::filesystem::path ProjectSceneSystem::ResolveProjectRelativePath(const std::wstring& pathText, const std::filesystem::path& baseDirectory) const
{
	if (pathText.empty())
		return {};

	std::filesystem::path path(pathText);
	if (path.is_absolute())
	{
		if (path.root_name().empty() && !baseDirectory.empty())
		{
			const std::filesystem::path rootedRelativePath = path.relative_path();
			if (!rootedRelativePath.empty())
				return (baseDirectory / rootedRelativePath).lexically_normal();
		}

		return path.lexically_normal();
	}

	if (!baseDirectory.empty())
		return (baseDirectory / path).lexically_normal();

	return path.lexically_normal();
}

std::wstring ProjectSceneSystem::BuildProjectRelativePathText(const std::filesystem::path& path, const std::filesystem::path& baseDirectory) const
{
	if (path.empty())
		return L"";

	std::error_code relativeError;
	const std::filesystem::path relativePath = std::filesystem::relative(path, baseDirectory, relativeError);
	if (!relativeError && !relativePath.empty())
		return relativePath.lexically_normal().wstring();

	return path.lexically_normal().wstring();
}
