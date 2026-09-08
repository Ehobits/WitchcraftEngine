#include "ProjectWindow.h"

#include "String/SStringUtils.h"
#include "Common/SceneEntityType.h"
#include "D3DWindow/D3DWindow.h"
#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/SkeletonComponent.h"
#include "ECS/Component/SkinnedMeshComponent.h"
#include "ECS/WitchcraECS.h"
#include "Engine/Engine.h"
#include "Engine/EngineUtils.h"
#include "System/ProjectSceneSystem.h"
#include "System/WitchcraftFile/WMaterialFile.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

static std::wstring TrimProjectWindowWideString(std::wstring value)
{
	const auto isNotSpace = [](wchar_t ch)
	{
		return std::iswspace(ch) == 0;
	};

	value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
	value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
	return value;
}

static std::wstring ToLowerProjectWindowPathKey(std::wstring value)
{
	std::transform(value.begin(), value.end(), value.begin(), towlower);
	return value;
}

static bool IsPathInsideProjectWindowDirectory(const std::filesystem::path& path, const std::filesystem::path& directory)
{
	if (path.empty() || directory.empty())
		return false;

	std::error_code relativeError;
	const std::filesystem::path relativePath = std::filesystem::relative(path, directory, relativeError);
	if (relativeError || relativePath.empty())
		return false;

	for (const std::filesystem::path& part : relativePath)
	{
		if (part == L"..")
			return false;
	}
	return true;
}

static std::filesystem::path ResolveProjectWindowAssetPath(
	const std::wstring& assetPath,
	const std::filesystem::path& projectRootPath,
	const std::filesystem::path& fallbackBasePath = {})
{
	if (assetPath.empty())
		return {};

	std::filesystem::path path(assetPath);
	if (path.is_absolute())
		return path.lexically_normal();

	if (!projectRootPath.empty())
	{
		const std::filesystem::path projectCandidate = (projectRootPath / path).lexically_normal();
		std::error_code projectExistsError;
		if (std::filesystem::exists(projectCandidate, projectExistsError))
			return projectCandidate;
	}

	if (!fallbackBasePath.empty())
	{
		const std::filesystem::path fallbackCandidate = (fallbackBasePath / path).lexically_normal();
		std::error_code fallbackExistsError;
		if (std::filesystem::exists(fallbackCandidate, fallbackExistsError))
			return fallbackCandidate;
	}

	return EngineUtils::ResolveProjectPath(path).lexically_normal();
}

static std::filesystem::path ResolveProjectWindowMaterialTexturePath(
	const std::filesystem::path& materialFilePath,
	const std::wstring& texturePath,
	const std::filesystem::path& projectRootPath)
{
	if (texturePath.empty())
		return {};

	std::filesystem::path path(texturePath);
	if (path.is_absolute())
		return path.lexically_normal();

	if (!projectRootPath.empty())
	{
		const std::filesystem::path projectCandidate = (projectRootPath / path).lexically_normal();
		std::error_code projectExistsError;
		if (std::filesystem::exists(projectCandidate, projectExistsError))
			return projectCandidate;
	}

	if (!path.has_parent_path() && !materialFilePath.empty())
	{
		const std::filesystem::path importedTextureCandidate =
			(materialFilePath.parent_path().parent_path() / L"Textures" / path).lexically_normal();
		std::error_code textureExistsError;
		if (std::filesystem::exists(importedTextureCandidate, textureExistsError))
			return importedTextureCandidate;
	}

	return ResolveProjectWindowAssetPath(texturePath, projectRootPath, materialFilePath.parent_path());
}

static std::wstring BuildProjectWindowResourceDisplayPath(
	const std::filesystem::path& resolvedPath,
	const std::filesystem::path& projectRootPath)
{
	if (resolvedPath.empty())
		return L"";

	const std::filesystem::path normalizedPath = resolvedPath.lexically_normal();
	if (IsPathInsideProjectWindowDirectory(normalizedPath, projectRootPath))
	{
		std::error_code relativeError;
		const std::filesystem::path relativePath = std::filesystem::relative(normalizedPath, projectRootPath, relativeError);
		if (!relativeError && !relativePath.empty())
			return relativePath.generic_wstring();
	}

	return normalizedPath.filename().wstring();
}

static void AddProjectWindowActiveResource(
	std::vector<ProjectWindowActiveResourceEntry>* resources,
	ProjectWindowActiveResourceBuckets* buckets,
	const std::filesystem::path& resolvedPath,
	const std::filesystem::path& projectRootPath)
{
	if (resources == nullptr || buckets == nullptr || resolvedPath.empty())
		return;

	const std::filesystem::path normalizedPath = resolvedPath.lexically_normal();
	const std::wstring key = ToLowerProjectWindowPathKey(normalizedPath.wstring());
	if (!buckets->SeenKeys.insert(key).second)
		return;

	ProjectWindowActiveResourceEntry entry;
	entry.DisplayPath = BuildProjectWindowResourceDisplayPath(normalizedPath, projectRootPath);
	entry.FullPath = normalizedPath.wstring();
	resources->push_back(std::move(entry));
}

static void AddProjectWindowMaterialTextures(
	const std::filesystem::path& materialFilePath,
	const std::filesystem::path& projectRootPath,
	ProjectWindowActiveResourceBuckets* buckets)
{
	if (buckets == nullptr || materialFilePath.empty())
		return;

	WMaterialFileData materialData;
	if (!WMaterialFile::LoadFromFile(materialFilePath, &materialData))
		return;

	auto addTexture = [&](const std::wstring& texturePath)
	{
		const std::filesystem::path resolvedTexturePath =
			ResolveProjectWindowMaterialTexturePath(materialFilePath, texturePath, projectRootPath);
		AddProjectWindowActiveResource(&buckets->Images, buckets, resolvedTexturePath, projectRootPath);
	};

	addTexture(materialData.DiffuseTexture);
	if (materialData.UseNormalTexture)
		addTexture(materialData.NormalTexture);
	if (materialData.UseMetallicTexture)
		addTexture(materialData.MetallicTexture);
	if (materialData.UseRoughnessTexture)
		addTexture(materialData.RoughnessTexture);
	if (materialData.UseOpacityTexture)
		addTexture(materialData.OpacityTexture);
}

static void SortProjectWindowActiveResources(std::vector<ProjectWindowActiveResourceEntry>* resources)
{
	if (resources == nullptr)
		return;

	std::sort(
		resources->begin(),
		resources->end(),
		[](const ProjectWindowActiveResourceEntry& lhs, const ProjectWindowActiveResourceEntry& rhs)
		{
			return lhs.DisplayPath < rhs.DisplayPath;
		});
}

static void RenderProjectWindowActiveResourceGroup(
	const char* label,
	const std::vector<ProjectWindowActiveResourceEntry>& resources)
{
	std::string nodeLabel = label;
	nodeLabel += " (";
	nodeLabel += std::to_string(resources.size());
	nodeLabel += ")";

	const bool opened = ImGui::TreeNodeEx(
		nodeLabel.c_str(),
		ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
	if (!opened)
		return;

	if (resources.empty())
	{
		ImGui::TextDisabled("无活动资源。");
	}
	else
	{
		for (std::size_t resourceIndex = 0; resourceIndex < resources.size(); ++resourceIndex)
		{
			const ProjectWindowActiveResourceEntry& resource = resources[resourceIndex];
			ImGui::PushID(static_cast<int>(resourceIndex));
			ImGui::TreeNodeEx(
				SString::WstringToUTF8(resource.DisplayPath).c_str(),
				ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("资源路径：%s", SString::WstringToUTF8(resource.FullPath).c_str());
			ImGui::PopID();
		}
	}

	ImGui::TreePop();
}

void ProjectWindow::Init(Engine* engine, ProjectSceneSystem* projectSceneSystem)
{
	m_engine = engine;
	m_projectSceneSystem = projectSceneSystem;
}

void ProjectWindow::Render()
{
	if (m_renderProject)
	{
		if (ImGui::Begin("项目"))
		{
			const bool playModeActive = IsPlayModeActive();
			const bool projectOpen = IsProjectOpen();
			const bool projectDirty = m_projectSceneSystem != nullptr && m_projectSceneSystem->IsProjectDirty();

			if (!projectOpen)
			{
				ImGui::TextDisabled("未打开项目。");
			}
			else
			{
				const std::wstring projectName = m_projectSceneSystem->GetProjectName();
				const std::wstring projectLabel = projectDirty ? projectName + L" *" : projectName;
				const std::filesystem::path projectFilePath = m_projectSceneSystem->GetProjectFilePath();
				const std::filesystem::path projectRootPath = projectFilePath.parent_path();

				const bool projectNodeOpen = ImGui::TreeNodeEx(
					SString::WstringToUTF8(projectLabel).c_str(),
					ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("项目路径：%s", SString::WstringToUTF8(projectFilePath.wstring()).c_str());
				if (ImGui::BeginPopupContextItem("ProjectNodeContextMenu", ImGuiPopupFlags_MouseButtonRight))
				{
					ImGui::BeginDisabled(playModeActive);
					if (ImGui::BeginMenu("添加场景"))
					{
						if (ImGui::MenuItem("现有场景"))
							m_projectSceneSystem->AddProjectSceneFromFile();
						if (ImGui::MenuItem("新建场景"))
							OpenNewSceneDialog();
						ImGui::EndMenu();
					}
					if (ImGui::MenuItem("重命名"))
						OpenProjectRenameDialog();
					ImGui::EndDisabled();
					ImGui::EndPopup();
				}
				if (projectNodeOpen)
				{
					if (ImGui::TreeNodeEx("场景", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth))
					{
						const std::vector<WProjectSceneData>& projectScenes = m_projectSceneSystem->GetProjectScenes();
						const std::wstring currentSceneId = m_projectSceneSystem->GetCurrentProjectSceneId();
						if (projectScenes.empty())
						{
							ImGui::TextDisabled("项目场景列表为空。");
						}
						else
						{
							for (std::size_t sceneIndex = 0; sceneIndex < projectScenes.size(); ++sceneIndex)
							{
								const WProjectSceneData& projectScene = projectScenes[sceneIndex];
								const bool isCurrentScene = !currentSceneId.empty() && projectScene.Id == currentSceneId;
								std::wstring sceneLabel = projectScene.Name.empty() ? projectScene.Path : projectScene.Name;
								if (projectScene.Entry)
									sceneLabel += L" [入口]";
								if (isCurrentScene)
									sceneLabel += L" [当前]";
								if (isCurrentScene && m_projectSceneSystem->IsCurrentSceneDirty())
									sceneLabel += L" *";

								ImGui::PushID(static_cast<int>(sceneIndex));
								const bool sceneNodeOpen = ImGui::TreeNodeEx(
									SString::WstringToUTF8(sceneLabel).c_str(),
									ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
								std::filesystem::path scenePath(projectScene.Path);
								if (!scenePath.is_absolute())
									scenePath = projectRootPath / scenePath;
								scenePath = scenePath.lexically_normal();
								if (ImGui::IsItemHovered())
									ImGui::SetTooltip("场景路径：%s", SString::WstringToUTF8(scenePath.wstring()).c_str());
								if (ImGui::BeginPopupContextItem("ProjectSceneNodeContextMenu", ImGuiPopupFlags_MouseButtonRight))
								{
									ImGui::BeginDisabled(playModeActive);
									if (ImGui::MenuItem("切换至", nullptr, false, !isCurrentScene))
										m_projectSceneSystem->OpenProjectSceneByIndex(sceneIndex);
									if (ImGui::MenuItem("设为入口", nullptr, false, !projectScene.Entry))
										m_projectSceneSystem->SetProjectEntrySceneByIndex(sceneIndex);
									if (ImGui::MenuItem("重命名"))
										OpenSceneRenameDialog(sceneIndex);
									if (ImGui::MenuItem("移除", nullptr, false, !isCurrentScene))
										m_projectSceneSystem->RemoveProjectSceneByIndex(sceneIndex);
									if (ImGui::MenuItem("删除", nullptr, false, !isCurrentScene))
										OpenSceneDeleteDialog(sceneIndex, projectScene.Name.empty() ? projectScene.Path : projectScene.Name);
									if (isCurrentScene)
									{
										ImGui::Separator();
										ImGui::TextDisabled("当前编辑的场景不可移除或删除。");
									}
									ImGui::EndDisabled();
									ImGui::EndPopup();
								}
								if (sceneNodeOpen)
									ImGui::TreePop();
								ImGui::PopID();
							}
						}
						ImGui::TreePop();
					}

					if (ImGui::TreeNodeEx("当前场景活动资源", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth))
					{
						RenderActiveResources(projectRootPath);
						ImGui::TreePop();
					}

					ImGui::TreePop();
				}
			}
		}
		ImGui::End();
	}

	RenderDialogs();
}

void ProjectWindow::RenderDialogs()
{
	RenderNewSceneDialog();
	RenderProjectRenameDialog();
	RenderSceneRenameDialog();
	RenderSceneDeleteDialog();
}

void ProjectWindow::NeedRender(bool render)
{
	m_renderProject = render;
}

bool ProjectWindow::IsRendering() const
{
	return m_renderProject;
}

void ProjectWindow::OpenNewSceneDialog()
{
	if (IsPlayModeActive() || !IsProjectOpen())
		return;

	m_newSceneNameBuffer.fill('\0');
	const std::string defaultSceneName = SString::WstringToUTF8(L"未命名场景");
	strcpy_s(m_newSceneNameBuffer.data(), m_newSceneNameBuffer.size(), defaultSceneName.c_str());
	m_newSceneTypeColorDraft = WitchcraECS::BuildDefaultSceneEntityTypeColors();
	m_newSceneTypeColorDraftDirty = false;
	m_newSceneErrorMessage.clear();
	m_openNewSceneDialog = true;
}

void ProjectWindow::OpenProjectRenameDialog()
{
	if (IsPlayModeActive() || !IsProjectOpen())
		return;

	m_projectRenameNameBuffer.fill('\0');
	const std::string currentProjectName = SString::WstringToUTF8(m_projectSceneSystem->GetProjectName());
	strcpy_s(m_projectRenameNameBuffer.data(), m_projectRenameNameBuffer.size(), currentProjectName.c_str());
	m_projectRenameErrorMessage.clear();
	m_openProjectRenameDialog = true;
}

void ProjectWindow::OpenSceneRenameDialog(std::size_t sceneIndex)
{
	if (IsPlayModeActive() || !IsProjectOpen())
		return;

	const std::vector<WProjectSceneData>& projectScenes = m_projectSceneSystem->GetProjectScenes();
	if (sceneIndex >= projectScenes.size())
		return;

	m_sceneRenameTargetIndex = sceneIndex;
	m_sceneRenameNameBuffer.fill('\0');
	const std::wstring currentSceneName = projectScenes[sceneIndex].Name.empty()
		? projectScenes[sceneIndex].Path
		: projectScenes[sceneIndex].Name;
	const std::string currentSceneNameUtf8 = SString::WstringToUTF8(currentSceneName);
	strcpy_s(m_sceneRenameNameBuffer.data(), m_sceneRenameNameBuffer.size(), currentSceneNameUtf8.c_str());
	m_sceneRenameErrorMessage.clear();
	m_openSceneRenameDialog = true;
}

void ProjectWindow::OpenSceneDeleteDialog(std::size_t sceneIndex, const std::wstring& sceneName)
{
	if (IsPlayModeActive() || !IsProjectOpen())
		return;

	const std::vector<WProjectSceneData>& projectScenes = m_projectSceneSystem->GetProjectScenes();
	if (sceneIndex >= projectScenes.size())
		return;

	m_sceneDeleteTargetIndex = sceneIndex;
	m_sceneDeleteTargetName = sceneName;
	m_openSceneDeleteDialog = true;
}

bool ProjectWindow::ConsumePendingNewScene(
	std::wstring* sceneName,
	std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)>* typeColorDraft,
	bool* hasTypeColorOverride)
{
	if (!m_pendingNewSceneRequested)
		return false;

	if (sceneName != nullptr)
		*sceneName = m_pendingNewSceneName;
	if (typeColorDraft != nullptr)
		*typeColorDraft = m_pendingNewSceneTypeColorDraft;
	if (hasTypeColorOverride != nullptr)
		*hasTypeColorOverride = m_pendingNewSceneTypeColorOverride;

	m_pendingNewSceneRequested = false;
	m_pendingNewSceneName.clear();
	m_pendingNewSceneTypeColorOverride = false;
	return true;
}

bool ProjectWindow::IsPlayModeActive() const
{
	return m_engine != nullptr && m_engine->IsPlayModeActive();
}

bool ProjectWindow::IsProjectOpen() const
{
	return m_projectSceneSystem != nullptr && m_projectSceneSystem->IsProjectOpen();
}

void ProjectWindow::RenderSceneTypeColorDraftControls(
	std::array<DirectX::XMFLOAT4, static_cast<size_t>(SceneEntityType::Count)>& colorDraft,
	bool* dirty,
	const char* resetButtonLabel)
{
	for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
	{
		const SceneEntityType sceneType = static_cast<SceneEntityType>(typeIndex);
		DirectX::XMFLOAT4 typeColor = colorDraft[typeIndex];
		float color[4] = { typeColor.x, typeColor.y, typeColor.z, typeColor.w };
		const std::string label = SString::WstringToUTF8(SceneEntityTypeToDisplayName(sceneType));
		if (ImGui::ColorEdit4(label.c_str(), color))
		{
			colorDraft[typeIndex] = DirectX::XMFLOAT4(color[0], color[1], color[2], color[3]);
			if (dirty != nullptr)
				*dirty = true;
		}
	}

	if (ImGui::Button(resetButtonLabel != nullptr ? resetButtonLabel : "恢复默认颜色"))
	{
		colorDraft = WitchcraECS::BuildDefaultSceneEntityTypeColors();
		if (dirty != nullptr)
			*dirty = true;
	}
}

void ProjectWindow::RenderActiveResources(const std::filesystem::path& projectRootPath)
{
	if (m_engine == nullptr)
	{
		ImGui::TextDisabled("无法读取当前场景活动资源。");
		return;
	}

	const std::filesystem::path normalizedProjectRootPath = projectRootPath.lexically_normal();
	if (m_activeResourcesCacheDirty || m_activeResourcesCacheProjectRootPath != normalizedProjectRootPath)
		RefreshActiveResourcesCache(normalizedProjectRootPath);

	RenderProjectWindowActiveResourceGroup("图像 / 贴图", m_activeResourcesCache.Images);
	RenderProjectWindowActiveResourceGroup("音频", m_activeResourcesCache.Audio);
	RenderProjectWindowActiveResourceGroup("模型", m_activeResourcesCache.Models);
	RenderProjectWindowActiveResourceGroup("材质", m_activeResourcesCache.Materials);
	RenderProjectWindowActiveResourceGroup("骨骼动画", m_activeResourcesCache.SkeletalAnimation);
}

void ProjectWindow::MarkActiveResourcesDirty()
{
	m_activeResourcesCacheDirty = true;
}

void ProjectWindow::RefreshActiveResourcesCache(const std::filesystem::path& projectRootPath)
{
	m_activeResourcesCache = {};
	m_activeResourcesCacheProjectRootPath = projectRootPath.lexically_normal();

	if (m_engine == nullptr)
		return;

	WitchcraECS* ecs = m_engine->GetECS();
	D3DWindow* dx = m_engine->GetD3DWindow();
	if (ecs == nullptr || dx == nullptr)
		return;

	ProjectWindowActiveResourceBuckets buckets;
	std::vector<SceneEntityBase*> pendingEntities = ecs->GetSceneRootEntities();
	while (!pendingEntities.empty())
	{
		SceneEntityBase* entity = pendingEntities.back();
		pendingEntities.pop_back();
		if (entity == nullptr)
			continue;

		const std::vector<SceneEntityBase*>& children = ecs->GetSceneChildren(entity);
		for (SceneEntityBase* child : children)
			pendingEntities.push_back(child);

		if (MeshComponent* meshComponent = ecs->GetComponent<MeshComponent>(entity))
		{
			const std::wstring modelPathText = meshComponent->GetFileName();
			if (!modelPathText.empty())
			{
				const std::filesystem::path modelFilePath =
					ResolveProjectWindowAssetPath(modelPathText, projectRootPath);
				AddProjectWindowActiveResource(&buckets.Models, &buckets, modelFilePath, projectRootPath);
			}

			const std::wstring materialName = meshComponent->GetDefaultMaterialName();
			if (!materialName.empty())
			{
				const std::wstring skyTexturePathText = dx->GetSkyTexturePathByMaterialName(materialName);
				if (!skyTexturePathText.empty())
				{
					const std::filesystem::path skyTexturePath =
						ResolveProjectWindowAssetPath(skyTexturePathText, projectRootPath);
					AddProjectWindowActiveResource(&buckets.Images, &buckets, skyTexturePath, projectRootPath);
				}

				const std::wstring materialFilePathText = dx->GetMaterialFilePathByMaterialName(materialName);
				if (!materialFilePathText.empty())
				{
					const std::filesystem::path materialFilePath =
						ResolveProjectWindowAssetPath(materialFilePathText, projectRootPath);
					AddProjectWindowActiveResource(&buckets.Materials, &buckets, materialFilePath, projectRootPath);
					AddProjectWindowMaterialTextures(materialFilePath, projectRootPath, &buckets);
				}
			}
		}

		if (SkeletonComponent* skeletonComponent = ecs->GetComponent<SkeletonComponent>(entity))
		{
			const std::filesystem::path skeletonAssetPath =
				ResolveProjectWindowAssetPath(skeletonComponent->GetSkeletonAssetPath(), projectRootPath);
			AddProjectWindowActiveResource(&buckets.SkeletalAnimation, &buckets, skeletonAssetPath, projectRootPath);
		}

		if (SkinnedMeshComponent* skinnedMeshComponent = ecs->GetComponent<SkinnedMeshComponent>(entity))
		{
			const std::filesystem::path skinnedMeshAssetPath =
				ResolveProjectWindowAssetPath(skinnedMeshComponent->GetSkinnedMeshAssetPath(), projectRootPath);
			AddProjectWindowActiveResource(&buckets.Models, &buckets, skinnedMeshAssetPath, projectRootPath);

			const std::filesystem::path skeletonAssetPath =
				ResolveProjectWindowAssetPath(skinnedMeshComponent->GetSkeletonAssetPath(), projectRootPath);
			AddProjectWindowActiveResource(&buckets.SkeletalAnimation, &buckets, skeletonAssetPath, projectRootPath);

			for (const std::wstring& materialName : skinnedMeshComponent->GetMaterialSlots())
			{
				const std::wstring materialFilePathText = dx->GetMaterialFilePathByMaterialName(materialName);
				if (materialFilePathText.empty())
					continue;

				const std::filesystem::path materialFilePath =
					ResolveProjectWindowAssetPath(materialFilePathText, projectRootPath);
				AddProjectWindowActiveResource(&buckets.Materials, &buckets, materialFilePath, projectRootPath);
				AddProjectWindowMaterialTextures(materialFilePath, projectRootPath, &buckets);
			}
		}

		if (AnimatorComponent* animatorComponent = ecs->GetComponent<AnimatorComponent>(entity))
		{
			for (const AnimatorComponent::AnimationLayer& layer : animatorComponent->GetLayers())
			{
				const std::filesystem::path clipAssetPath =
					ResolveProjectWindowAssetPath(layer.ClipAssetPath, projectRootPath);
				AddProjectWindowActiveResource(&buckets.SkeletalAnimation, &buckets, clipAssetPath, projectRootPath);

				const std::filesystem::path transitionClipAssetPath =
					ResolveProjectWindowAssetPath(layer.TransitionClipAssetPath, projectRootPath);
				AddProjectWindowActiveResource(&buckets.SkeletalAnimation, &buckets, transitionClipAssetPath, projectRootPath);
			}
		}
	}

	SortProjectWindowActiveResources(&buckets.Images);
	SortProjectWindowActiveResources(&buckets.Audio);
	SortProjectWindowActiveResources(&buckets.Models);
	SortProjectWindowActiveResources(&buckets.Materials);
	SortProjectWindowActiveResources(&buckets.SkeletalAnimation);

	m_activeResourcesCache = std::move(buckets);
	m_activeResourcesCacheDirty = false;
}

void ProjectWindow::ResetSceneRenameDialog()
{
	m_openSceneRenameDialog = false;
	m_sceneRenameErrorMessage.clear();
	m_sceneRenameTargetIndex = static_cast<std::size_t>(-1);
}

void ProjectWindow::ResetSceneDeleteDialog()
{
	m_openSceneDeleteDialog = false;
	m_sceneDeleteTargetName.clear();
	m_sceneDeleteTargetIndex = static_cast<std::size_t>(-1);
}

void ProjectWindow::RenderNewSceneDialog()
{
	if (m_openNewSceneDialog)
		ImGui::OpenPopup("新建场景");

	bool dialogOpen = m_openNewSceneDialog;
	ImGui::SetNextWindowSize(ImVec2(2240.0f, 1200.0f), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("新建场景", &dialogOpen, ImGuiWindowFlags_NoDocking))
	{
		const bool playModeActive = IsPlayModeActive();
		const bool projectOpen = IsProjectOpen();

		ImGui::InputText("名称", m_newSceneNameBuffer.data(), m_newSceneNameBuffer.size());

		ImGui::Separator();
		ImGui::Text("场景设置");
		ImGui::Separator();
		if (ImGui::TreeNodeEx("实体描边颜色设置", ImGuiTreeNodeFlags_DefaultOpen))
		{
			RenderSceneTypeColorDraftControls(m_newSceneTypeColorDraft, &m_newSceneTypeColorDraftDirty, "恢复默认颜色");
			ImGui::TreePop();
		}

		if (!m_newSceneErrorMessage.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", SString::WstringToUTF8(m_newSceneErrorMessage).c_str());
		if (!projectOpen)
			ImGui::TextDisabled("请先新建或打开项目，再新建场景。");
		if (playModeActive)
			ImGui::TextDisabled("Play Mode 中不能新建场景。");

		ImGui::Separator();
		const float buttonHeight = 40.0f;
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float buttonWidth = (availableWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::BeginDisabled(playModeActive || !projectOpen);
		if (ImGui::Button("创建", ImVec2(buttonWidth, buttonHeight)))
		{
			const std::wstring sceneName = TrimProjectWindowWideString(SString::UTF8ToWstring(std::string(m_newSceneNameBuffer.data())));
			if (sceneName.empty())
			{
				m_newSceneErrorMessage = L"场景名称不能为空。";
			}
			else
			{
				m_pendingNewSceneName = sceneName;
				m_pendingNewSceneTypeColorDraft = m_newSceneTypeColorDraft;
				m_pendingNewSceneTypeColorOverride = true;
				m_pendingNewSceneRequested = true;
				m_openNewSceneDialog = false;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(buttonWidth, buttonHeight)))
		{
			m_openNewSceneDialog = false;
			m_newSceneErrorMessage.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (!dialogOpen)
		m_openNewSceneDialog = false;
}

void ProjectWindow::RenderSceneRenameDialog()
{
	if (m_openSceneRenameDialog)
		ImGui::OpenPopup("重命名场景");

	bool dialogOpen = m_openSceneRenameDialog;
	if (ImGui::BeginPopupModal("重命名场景", &dialogOpen, ImGuiWindowFlags_NoDocking))
	{
		const bool playModeActive = IsPlayModeActive();
		const bool projectOpen = IsProjectOpen();
		const bool sceneIndexValid =
			m_projectSceneSystem != nullptr &&
			projectOpen &&
			m_sceneRenameTargetIndex < m_projectSceneSystem->GetProjectScenes().size();

		ImGui::InputText("名称", m_sceneRenameNameBuffer.data(), m_sceneRenameNameBuffer.size());
		if (!m_sceneRenameErrorMessage.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", SString::WstringToUTF8(m_sceneRenameErrorMessage).c_str());
		if (!projectOpen)
			ImGui::TextDisabled("请先打开项目。");
		if (playModeActive)
			ImGui::TextDisabled("Play Mode 中不能重命名场景。");

		ImGui::Separator();
		const float buttonHeight = 40.0f;
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float buttonWidth = (availableWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::BeginDisabled(playModeActive || !projectOpen || !sceneIndexValid);
		if (ImGui::Button("确认", ImVec2(buttonWidth, buttonHeight)))
		{
			const std::wstring sceneName = TrimProjectWindowWideString(SString::UTF8ToWstring(std::string(m_sceneRenameNameBuffer.data())));
			if (sceneName.empty())
			{
				m_sceneRenameErrorMessage = L"场景名称不能为空。";
			}
			else if (m_projectSceneSystem != nullptr &&
				m_projectSceneSystem->RenameProjectSceneByIndex(m_sceneRenameTargetIndex, sceneName))
			{
				ResetSceneRenameDialog();
				ImGui::CloseCurrentPopup();
			}
			else
			{
				m_sceneRenameErrorMessage = L"场景重命名失败。";
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(buttonWidth, buttonHeight)))
		{
			ResetSceneRenameDialog();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (!dialogOpen)
		ResetSceneRenameDialog();
}

void ProjectWindow::RenderSceneDeleteDialog()
{
	if (m_openSceneDeleteDialog)
		ImGui::OpenPopup("删除场景");

	bool dialogOpen = m_openSceneDeleteDialog;
	if (ImGui::BeginPopupModal("删除场景", &dialogOpen, ImGuiWindowFlags_NoDocking))
	{
		const bool playModeActive = IsPlayModeActive();
		const bool projectOpen = IsProjectOpen();
		const bool sceneIndexValid =
			m_projectSceneSystem != nullptr &&
			projectOpen &&
			m_sceneDeleteTargetIndex < m_projectSceneSystem->GetProjectScenes().size();

		ImGui::Text("确认删除场景：");
		ImGui::TextWrapped("%s", SString::WstringToUTF8(m_sceneDeleteTargetName).c_str());
		ImGui::TextDisabled("此操作会删除场景文件并从项目中移除。");
		if (!projectOpen)
			ImGui::TextDisabled("请先打开项目。");
		if (playModeActive)
			ImGui::TextDisabled("Play Mode 中不能删除场景。");

		ImGui::Separator();
		const float buttonHeight = 40.0f;
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float buttonWidth = (availableWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::BeginDisabled(playModeActive || !projectOpen || !sceneIndexValid);
		if (ImGui::Button("删除", ImVec2(buttonWidth, buttonHeight)))
		{
			if (m_projectSceneSystem != nullptr &&
				m_projectSceneSystem->DeleteProjectSceneByIndex(m_sceneDeleteTargetIndex))
			{
				ResetSceneDeleteDialog();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(buttonWidth, buttonHeight)))
		{
			ResetSceneDeleteDialog();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (!dialogOpen)
		ResetSceneDeleteDialog();
}

void ProjectWindow::RenderProjectRenameDialog()
{
	if (m_openProjectRenameDialog)
		ImGui::OpenPopup("重命名项目");

	bool dialogOpen = m_openProjectRenameDialog;
	if (ImGui::BeginPopupModal("重命名项目", &dialogOpen, ImGuiWindowFlags_NoDocking))
	{
		const bool playModeActive = IsPlayModeActive();
		const bool projectOpen = IsProjectOpen();

		ImGui::InputText("名称", m_projectRenameNameBuffer.data(), m_projectRenameNameBuffer.size());
		if (!m_projectRenameErrorMessage.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", SString::WstringToUTF8(m_projectRenameErrorMessage).c_str());
		if (playModeActive)
			ImGui::TextDisabled("Play Mode 中不能重命名项目。");
		if (!projectOpen)
			ImGui::TextDisabled("请先打开项目。");

		ImGui::Separator();
		const float buttonHeight = 40.0f;
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float buttonWidth = (availableWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::BeginDisabled(playModeActive || !projectOpen);
		if (ImGui::Button("确认", ImVec2(buttonWidth, buttonHeight)))
		{
			const std::wstring projectName = TrimProjectWindowWideString(SString::UTF8ToWstring(std::string(m_projectRenameNameBuffer.data())));
			if (projectName.empty())
			{
				m_projectRenameErrorMessage = L"项目名称不能为空。";
			}
			else if (m_projectSceneSystem != nullptr && m_projectSceneSystem->RenameProject(projectName))
			{
				m_openProjectRenameDialog = false;
				m_projectRenameErrorMessage.clear();
				ImGui::CloseCurrentPopup();
			}
			else
			{
				m_projectRenameErrorMessage = L"项目重命名失败。";
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(buttonWidth, buttonHeight)))
		{
			m_openProjectRenameDialog = false;
			m_projectRenameErrorMessage.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (!dialogOpen)
		m_openProjectRenameDialog = false;
}
