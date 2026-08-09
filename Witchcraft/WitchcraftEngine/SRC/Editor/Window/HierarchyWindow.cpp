#include "HierarchyWindow.h"

#include "ConsoleWindow.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "ECS/WitchcraECS.h"
#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/Component/SkeletonComponent.h"
#include "ENGINE/EngineUtils.h"
#include "String/SStringUtils.h"
#include "D3DWindow/D3DWindow.h"
#include "System/WitchcraftFile/WMaterialFile.h"
#include "System/Assets.h"
#include "Editor/EditorAssetCache.h"

#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <filesystem>

#include <misc/cpp/imgui_stdlib.h>

void HierarchyWindow::Init(ConsoleWindow* consoleWindow, AssimpLoader* assimpLoader, WitchcraECS* ecs, D3DWindow* dx, Engine* engine)
{
	m_consoleWindow = consoleWindow;
	m_assimpLoader = assimpLoader;
	m_ecs = ecs;
	m_dx = dx;
	m_engine = engine;
	if (m_assimpLoader != nullptr)
		m_assimpLoader->SetConsoleWindow(consoleWindow);
}

void HierarchyWindow::Render()
{
	if (!renderHierarchy)
		return;
	if (m_ecs == nullptr)
		return;

	ImGui::Begin("层次");

	RenderTree();

	if (m_openDeleteConfirmPopup)
	{
		ImGui::OpenPopup("删除实体确认");
		m_openDeleteConfirmPopup = false;
	}

	if (ImGui::BeginPopupModal("删除实体确认", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("受影响实体：");
		ImGui::Separator();
		const float impactTreeFrameHeight = ImGui::GetTextLineHeightWithSpacing() * 12.0f;
		const ImVec2 impactTreeFrameSize(ImGui::GetContentRegionAvail().x, impactTreeFrameHeight);
		if (ImGui::BeginChild("DeleteImpactTreeFrame", impactTreeFrameSize, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			if (!m_deleteCandidateEntities.empty())
			{
				for (SceneEntityBase* candidateEntity : m_deleteCandidateEntities)
					RenderDeleteImpactTree(candidateEntity);
			}
			else if (m_deleteCandidateEntity != nullptr)
				RenderDeleteImpactTree(m_deleteCandidateEntity);
			else
				ImGui::BulletText("%s", SString::WstringToUTF8(m_deleteCandidateEntityName).c_str());
		}
		ImGui::EndChild();

		ImGui::Separator();

		if (m_deleteCandidateHasChildren)
		{
			if (m_deleteCandidateEntities.size() > 1)
				ImGui::Text("所选实体中包含带子级的项目。");
			else
				ImGui::Text("实体 \"%s\" 拥有子实体。", SString::WstringToUTF8(m_deleteCandidateEntityName).c_str());
			ImGui::Text("是否同时删除这些子实体？");
			ImGui::Text("点击“否”会把子实体移动到上一层。");

			if (ImGui::Button("是"))
			{
				m_pendingDeleteEntity = m_deleteCandidateEntity;
				m_pendingDeleteEntities = m_deleteCandidateEntities;
				m_pendingDeleteChildren = true;
				m_hasPendingDeleteRequest = true;
				m_deleteCandidateEntity = nullptr;
				m_deleteCandidateEntities.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("否"))
			{
				m_pendingDeleteEntity = m_deleteCandidateEntity;
				m_pendingDeleteEntities = m_deleteCandidateEntities;
				m_pendingDeleteChildren = false;
				m_hasPendingDeleteRequest = true;
				m_deleteCandidateEntity = nullptr;
				m_deleteCandidateEntities.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("取消"))
			{
				m_deleteCandidateEntity = nullptr;
				m_deleteCandidateEntities.clear();
				ImGui::CloseCurrentPopup();
			}
		}
		else
		{
			if (m_deleteCandidateEntities.size() > 1)
				ImGui::Text("确认删除所选 %d 个实体吗？", static_cast<int>(m_deleteCandidateEntities.size()));
			else
				ImGui::Text("确认删除实体 \"%s\" 吗？", SString::WstringToUTF8(m_deleteCandidateEntityName).c_str());

			if (ImGui::Button("删除"))
			{
				m_pendingDeleteEntity = m_deleteCandidateEntity;
				m_pendingDeleteEntities = m_deleteCandidateEntities;
				m_pendingDeleteChildren = true;
				m_hasPendingDeleteRequest = true;
				m_deleteCandidateEntity = nullptr;
				m_deleteCandidateEntities.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("取消"))
			{
				m_deleteCandidateEntity = nullptr;
				m_deleteCandidateEntities.clear();
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}

	if (m_openImportWindow)
	{
		if (CreateComponentWindow(&m_openImportWindow, &m_importEntityName, &m_importTransform, true, "缩放", nullptr, nullptr, &m_importSceneType))
		{
			// 导入会创建/销毁 D3D 资源，不能在 ImGui 渲染命令录制期间直接执行。
			m_pendingImportFilePath = m_importFilePath;
			m_pendingImportEntityName = m_importEntityName;
			m_pendingImportTransform = m_importTransform;
			m_pendingImportSceneType = m_importSceneType;
			m_hasPendingImportRequest = true;
		}
	}

	RenderImportModelBrowser();

	if (m_openOperationErrorPopup)
	{
		ImGui::OpenPopup("层级操作失败");
		m_openOperationErrorPopup = false;
	}

	if (ImGui::BeginPopupModal("层级操作失败", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("%s", SString::WstringToUTF8(m_operationErrorMessage).c_str());
		if (ImGui::Button("确定"))
		{
			m_operationErrorMessage.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::End();
}

void HierarchyWindow::ProcessDeferredActions()
{
	if (m_ecs == nullptr)
		return;

	ProcessPendingReparentRequest();
	ProcessPendingDeleteRequest();
	ProcessPendingImportRequest();
}

bool HierarchyWindow::ConsumePendingCreateRequest(std::uint32_t* createKind, std::wstring* defaultName, bool* refreshSkyTextures, bool* clearSelectionFirst)
{
	if (!m_hasPendingCreateRequest)
		return false;

	if (createKind != nullptr)
		*createKind = m_pendingCreateKind;
	if (defaultName != nullptr)
		*defaultName = m_pendingCreateDefaultName;
	if (refreshSkyTextures != nullptr)
		*refreshSkyTextures = m_pendingCreateRefreshSkyTextures;
	if (clearSelectionFirst != nullptr)
		*clearSelectionFirst = m_pendingCreateClearSelectionFirst;

	m_hasPendingCreateRequest = false;
	m_pendingCreateKind = 0;
	m_pendingCreateDefaultName.clear();
	m_pendingCreateRefreshSkyTextures = false;
	m_pendingCreateClearSelectionFirst = false;
	return true;
}

bool HierarchyWindow::TryGetSelectedSkeletonBone(SceneEntityBase** outOwnerEntity, std::int32_t* outBoneIndex) const
{
	if (m_ecs == nullptr ||
		m_selectedSkeletonOwnerEntity == nullptr ||
		!m_ecs->HasEntity(m_selectedSkeletonOwnerEntity) ||
		m_selectedSkeletonBoneIndex < 0)
	{
		return false;
	}

	const Witchcraft::Animation::SkeletonData* skeletonData = m_ecs->GetSkeletonData(m_selectedSkeletonOwnerEntity);
	if (skeletonData == nullptr || !skeletonData->Topology.IsValidBoneIndex(m_selectedSkeletonBoneIndex))
		return false;

	if (outOwnerEntity != nullptr)
		*outOwnerEntity = m_selectedSkeletonOwnerEntity;
	if (outBoneIndex != nullptr)
		*outBoneIndex = m_selectedSkeletonBoneIndex;
	return true;
}

void HierarchyWindow::EnsureSkeletonHierarchyForEntity(SceneEntityBase* ownerEntity, std::int32_t preferredBoneIndex, bool createIfMissing)
{
	if (m_ecs == nullptr || ownerEntity == nullptr || !m_ecs->HasEntity(ownerEntity))
		return;

	if (preferredBoneIndex < 0)
	{
		if (const Witchcraft::Animation::SkeletonData* skeletonData = m_ecs->GetSkeletonData(ownerEntity))
		{
			if (skeletonData->Topology.IsValidBoneIndex(skeletonData->Topology.RootBoneIndex))
				preferredBoneIndex = skeletonData->Topology.RootBoneIndex;
		}
	}

	SceneEntityBase* targetEntity = nullptr;
	if (preferredBoneIndex >= 0)
		targetEntity = FindSkeletonHierarchyNode(ownerEntity, preferredBoneIndex);
	if (targetEntity == nullptr)
		targetEntity = m_ecs->FindSkeletonHierarchyRoot(ownerEntity);

	if (targetEntity == nullptr && createIfMissing && m_ecs->RebuildSkeletonHierarchyForEntity(ownerEntity))
	{
		if (preferredBoneIndex >= 0)
			targetEntity = FindSkeletonHierarchyNode(ownerEntity, preferredBoneIndex);
		if (targetEntity == nullptr)
			targetEntity = m_ecs->FindSkeletonHierarchyRoot(ownerEntity);
	}

	if (targetEntity == nullptr)
		return;

	m_ecs->SelectEntityForHierarchy(ownerEntity, false);
	m_selectedSkeletonOwnerEntity = ownerEntity;
	m_selectedSkeletonBoneIndex = preferredBoneIndex >= 0 ? preferredBoneIndex : -1;
	// 骨骼实体是内部编辑器辅助对象，并且有意在普通层次树中隐藏。
	// 不要为一个层次结构焦点排队：隐藏的焦点目标永远不能使用该请求，并将强制其所有可见的祖先打开每个帧。
}

void HierarchyWindow::RenderDeleteImpactTree(SceneEntityBase* ent)
{
	if (ent == nullptr || m_ecs == nullptr)
		return;

	ImGui::BulletText("%s", SString::WstringToUTF8(m_ecs->GetEntityName(ent)).c_str());

	if (m_ecs->GetHierarchyChildCount(ent) == 0)
		return;

	const std::vector<SceneEntityBase*>& children = m_ecs->GetHierarchyChildren(ent);
	ImGui::Indent();
	for (SceneEntityBase* child : children)
	{
		RenderDeleteImpactTree(child);
	}
	ImGui::Unindent();
}

void HierarchyWindow::DrawHierarchyDropTargetHighlight(bool valid)
{
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const ImU32 fillColor = valid ? IM_COL32(70, 160, 100, 48) : IM_COL32(190, 70, 70, 40);
	const ImU32 borderColor = valid ? IM_COL32(110, 220, 140, 200) : IM_COL32(240, 110, 110, 220);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(min, max, fillColor, 4.0f);
	drawList->AddRect(min, max, borderColor, 4.0f, 0, 2.0f);
}

void HierarchyWindow::ClearSelectedSkeletonBone()
{
	m_selectedSkeletonOwnerEntity = nullptr;
	m_selectedSkeletonBoneIndex = -1;
}

void HierarchyWindow::QueueFocusEntity(SceneEntityBase* entity)
{
	if (m_ecs == nullptr || entity == nullptr || !m_ecs->HasEntity(entity))
		return;

	m_pendingFocusEntity = entity;
	m_pendingFocusScroll = true;
}

SceneEntityBase* HierarchyWindow::FindSkeletonHierarchyNode(SceneEntityBase* ownerEntity, std::int32_t boneIndex) const
{
	if (m_ecs == nullptr || ownerEntity == nullptr || boneIndex < 0)
		return nullptr;

	SceneEntityBase* rootEntity = m_ecs->FindSkeletonHierarchyRoot(ownerEntity);
	if (rootEntity == nullptr)
		return nullptr;

	std::vector<SceneEntityBase*> pendingEntities;
	pendingEntities.push_back(rootEntity);
	while (!pendingEntities.empty())
	{
		SceneEntityBase* entity = pendingEntities.back();
		pendingEntities.pop_back();

		SceneEntityBase* bindingOwnerEntity = nullptr;
		std::int32_t bindingBoneIndex = -2;
		if (m_ecs->TryGetSkeletonHierarchyBinding(entity, &bindingOwnerEntity, &bindingBoneIndex, nullptr) &&
			bindingOwnerEntity == ownerEntity &&
			bindingBoneIndex == boneIndex)
		{
			return entity;
		}

		const std::vector<SceneEntityBase*>& children = m_ecs->GetHierarchyChildren(entity);
		pendingEntities.insert(pendingEntities.end(), children.begin(), children.end());
	}

	return nullptr;
}

bool HierarchyWindow::IsHiddenSkeletonHierarchyNode(SceneEntityBase* entity) const
{
	if (m_ecs == nullptr || entity == nullptr)
		return false;
	if (m_ecs->IsSkeletonHierarchyEntity(entity))
		return true;

	auto subtreeContainsMesh = [this](SceneEntityBase* rootEntity)
	{
		if (rootEntity == nullptr)
			return false;
		std::vector<SceneEntityBase*> pendingEntities{ rootEntity };
		while (!pendingEntities.empty())
		{
			SceneEntityBase* currentEntity = pendingEntities.back();
			pendingEntities.pop_back();
			if (m_ecs->GetComponent<MeshComponent>(currentEntity) != nullptr)
				return true;
			const std::vector<SceneEntityBase*>& children = m_ecs->GetHierarchyChildren(currentEntity);
			pendingEntities.insert(pendingEntities.end(), children.begin(), children.end());
		}
		return false;
	};
	for (SceneEntityBase* current = entity; current != nullptr; current = m_ecs->GetParentEntity(current))
	{
		SceneEntityBase* ownerEntity = m_ecs->GetParentEntity(current);
		if (ownerEntity != nullptr && m_ecs->GetComponent<SkeletonComponent>(ownerEntity) != nullptr && !subtreeContainsMesh(current))
			return true;
	}

	// Older scene files persisted the generated bone entities but not their
	// skeletonHierarchy binding fields.  Keep that data alive for animation and
	// the skeleton editor, while hiding the legacy "骨骼" subtree from this UI.
	for (SceneEntityBase* current = entity;
		current != nullptr;
		current = m_ecs->GetParentEntity(current))
	{
		if (m_ecs->GetEntityName(current) != L"骨骼")
			continue;

		SceneEntityBase* ownerEntity = m_ecs->GetParentEntity(current);
		if (ownerEntity != nullptr && m_ecs->GetComponent<SkeletonComponent>(ownerEntity) != nullptr)
			return true;
	}

	return false;
}

void HierarchyWindow::ApplyPendingFocusForNode(SceneEntityBase* entity)
{
	if (m_pendingFocusEntity == nullptr || entity != m_pendingFocusEntity)
		return;

	ImGui::SetScrollHereY(0.35f);
	m_pendingFocusScroll = false;
	m_pendingFocusEntity = nullptr;
}

void HierarchyWindow::InitializeTransformInputBuffer(const Transform& transform)
{
	m_transformInputBuffer[0] = FormatTransformValue(transform.position.x);
	m_transformInputBuffer[1] = FormatTransformValue(transform.position.y);
	m_transformInputBuffer[2] = FormatTransformValue(transform.position.z);
	m_transformInputBuffer[3] = FormatTransformValue(transform.rotation.x);
	m_transformInputBuffer[4] = FormatTransformValue(transform.rotation.y);
	m_transformInputBuffer[5] = FormatTransformValue(transform.rotation.z);
	m_transformInputBuffer[6] = FormatTransformValue(transform.scale.x);
	m_transformInputBuffer[7] = FormatTransformValue(transform.scale.y);
	m_transformInputBuffer[8] = FormatTransformValue(transform.scale.z);
	m_transformInputBufferInitialized = true;
}

void HierarchyWindow::RenderCreateComponentNameField(std::wstring* name)
{
	ImGui::Text("名称：");
	ImGui::SameLine();
	std::string tmp = SString::WstringToUTF8(*name);
	if (ImGui::InputText("##NameComponent", &tmp))
		*name = SString::UTF8ToWstring(tmp);
}

void HierarchyWindow::RenderCreateComponentErrorMessage() const
{
	if (!m_createComponentErrorMessage.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", SString::WstringToUTF8(m_createComponentErrorMessage).c_str());
}

void HierarchyWindow::RenderCreateComponentTransformFields(Transform* transform, bool showRotation, const char* scaleLabel)
{
	if (transform == nullptr)
		return;

	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3 / (2 * 0.875));

	auto renderFloatField = [&](const char* id, std::string& buffer, float* value, const ImVec4& color, const char* axisLabel, bool clampNonNegative = false)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::Text("%s", axisLabel);
			ImGui::PopStyleColor(1);
			ImGui::SameLine();
			if (ImGui::InputText(id, &buffer, ImGuiInputTextFlags_CharsScientific))
			{
				float parsedValue = 0.0f;
				if (TryParseFloatInput(buffer, &parsedValue))
					*value = clampNonNegative ? (std::max)(0.0f, parsedValue) : parsedValue;
			}
		};

	ImGui::Text("位置：");
	ImGui::SameLine();
	renderFloatField("##PotXTransformComp", m_transformInputBuffer[0], &transform->position.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "X");
	ImGui::SameLine();
	renderFloatField("##PotYTransformComp", m_transformInputBuffer[1], &transform->position.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Y");
	ImGui::SameLine();
	renderFloatField("##PotZTransformComp", m_transformInputBuffer[2], &transform->position.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "Z");

	if (showRotation)
	{
		ImGui::Text("旋转：");
		ImGui::SameLine();
		renderFloatField("##RotXTransformComp", m_transformInputBuffer[3], &transform->rotation.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "X");
		ImGui::SameLine();
		renderFloatField("##RotYTransformComp", m_transformInputBuffer[4], &transform->rotation.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Y");
		ImGui::SameLine();
		renderFloatField("##RotZTransformComp", m_transformInputBuffer[5], &transform->rotation.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "Z");
	}

	transform->scale.x = std::max(0.0f, transform->scale.x);
	transform->scale.y = std::max(0.0f, transform->scale.y);
	transform->scale.z = std::max(0.0f, transform->scale.z);

	ImGui::Text("%s：", scaleLabel != nullptr ? scaleLabel : "缩放");
	ImGui::SameLine();
	renderFloatField("##ScaXTransformComp", m_transformInputBuffer[6], &transform->scale.x, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "X", true);
	ImGui::SameLine();
	renderFloatField("##ScaYTransformComp", m_transformInputBuffer[7], &transform->scale.y, ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Y", true);
	ImGui::SameLine();
	renderFloatField("##ScaZTransformComp", m_transformInputBuffer[8], &transform->scale.z, ImVec4(0.0f, 0.0f, 1.0f, 1.0f), "Z", true);
}

void HierarchyWindow::RenderCreateComponentMaterialField(std::wstring* materialFilePath)
{
	if (materialFilePath == nullptr)
		return;

	const std::vector<EditorAssetCache::MaterialFileEntry>& materialFileEntries = EditorAssetCache::GetMaterialFiles();
	if (materialFileEntries.empty())
	{
		ImGui::TextDisabled("材质：ImportedAssets 中未找到 .wmat 文件");
		materialFilePath->clear();
		return;
	}

	size_t currentMaterialIndex = 0;
	for (size_t i = 0; i < materialFileEntries.size(); ++i)
	{
		if (materialFileEntries[i].path == *materialFilePath)
		{
			currentMaterialIndex = i;
			break;
		}
	}

	*materialFilePath = materialFileEntries[currentMaterialIndex].path;

	ImGui::Text("材质：");
	ImGui::SameLine();
	const std::string preview = SString::WstringToUTF8(materialFileEntries[currentMaterialIndex].displayName);
	if (ImGui::BeginCombo("##CreateEntityMaterial", preview.c_str()))
	{
		for (size_t i = 0; i < materialFileEntries.size(); ++i)
		{
			const bool isSelected = (materialFileEntries[i].path == *materialFilePath);
			const std::string materialUtf8 = SString::WstringToUTF8(materialFileEntries[i].displayName);
			if (ImGui::Selectable(materialUtf8.c_str(), isSelected))
			{
				*materialFilePath = materialFileEntries[i].path;
				currentMaterialIndex = i;
			}

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	for (size_t i = 0; i < materialFileEntries.size(); ++i)
	{
		if (materialFileEntries[i].path == *materialFilePath)
		{
			currentMaterialIndex = i;
			break;
		}
	}

	ImGui::TextDisabled("路径：ImportedAssets/%s",
		SString::WstringToUTF8(materialFileEntries[currentMaterialIndex].relativePath).c_str());
}

void HierarchyWindow::RenderCreateComponentSceneTypeField(SceneEntityType* sceneType)
{
	if (sceneType == nullptr)
		return;

	*sceneType = SanitizeSceneEntityType(static_cast<std::uint32_t>(*sceneType));
	ImGui::Text("实体分类：");
	ImGui::SameLine();

	const std::string preview = SString::WstringToUTF8(SceneEntityTypeToDisplayName(*sceneType));
	if (ImGui::BeginCombo("##CreateEntitySceneType", preview.c_str()))
	{
		for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
		{
			const SceneEntityType option = static_cast<SceneEntityType>(typeIndex);
			const bool isSelected = (*sceneType == option);
			const std::string label = SString::WstringToUTF8(SceneEntityTypeToDisplayName(option));
			if (ImGui::Selectable(label.c_str(), isSelected))
				*sceneType = option;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
}

void HierarchyWindow::RenderCreateComponentLightTypeField(CreateLightType* lightType)
{
	const std::string lightTypeText = SString::WstringToUTF8(L"灯光类型：");
	ImGui::Text("%s", lightTypeText.c_str());
	ImGui::SameLine();

	const std::string preview = SString::WstringToUTF8(GetCreateLightTypeLabel(*lightType));
	if (ImGui::BeginCombo("##CreateEntityLightType", preview.c_str()))
	{
		for (UINT option = 0; option < 3; option++)
		{
			const bool isSelected = (*lightType == option);
			const std::string label = SString::WstringToUTF8(GetCreateLightTypeLabel(option));
			if (ImGui::Selectable(label.c_str(), isSelected))
				*lightType = (CreateLightType)option;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
}

bool HierarchyWindow::ValidateCreateComponentRequest(const std::wstring& requestedName, SceneEntityBase* siblingScopeParent)
{
	if (requestedName.empty())
	{
		m_createComponentErrorMessage = L"名称不能为空。";
		return false;
	}

	if (!m_ecs->IsEntityNameAvailable(requestedName, siblingScopeParent))
	{
		m_createComponentErrorMessage = L"名称不得与现有同级项目重名。";
		return false;
	}

	m_createComponentErrorMessage.clear();
	return true;
}

void HierarchyWindow::ResetCreateComponentWindowState()
{
	m_createComponentErrorMessage.clear();
	m_transformInputBufferInitialized = false;
}

bool HierarchyWindow::CreateComponentWindow(
	bool* pOpen,
	std::wstring* name,
	Transform* transform,
	bool showRotation,
	const char* scaleLabel,
	std::wstring* materialFilePath,
	CreateLightType* lightType,
	SceneEntityType* sceneType,
	SceneEntityBase* siblingScopeParent)
{
	if (name == nullptr)
	{
		ResetCreateComponentWindowState();
		if (pOpen != nullptr)
			*pOpen = false;
		return false;
	}

	bool accepted = false;
	bool shouldCloseWindow = false;
	const bool isOpen = ImGui::Begin("创建实体", pOpen, ImGuiWindowFlags_NoDocking);

	if (isOpen)
	{
		if (transform != nullptr && !m_transformInputBufferInitialized)
			InitializeTransformInputBuffer(*transform);

		RenderCreateComponentNameField(name);
		RenderCreateComponentErrorMessage();
		RenderCreateComponentTransformFields(transform, showRotation, scaleLabel);
		RenderCreateComponentMaterialField(materialFilePath);
		RenderCreateComponentSceneTypeField(sceneType);
		if(lightType)
			RenderCreateComponentLightTypeField(lightType);

		if (ImGui::Button("确定"))
		{
			if (ValidateCreateComponentRequest(*name, siblingScopeParent))
			{
				accepted = true;
				shouldCloseWindow = true;
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("取消"))
			shouldCloseWindow = true;
	}

	ImGui::End();

	if (shouldCloseWindow)
	{
		ResetCreateComponentWindowState();
		if (pOpen != nullptr)
			*pOpen = false;
	}

	if (pOpen != nullptr && !(*pOpen))
		ResetCreateComponentWindowState();

	return accepted;
}

SceneEntityBase* HierarchyWindow::GetCurrentEntity()
{
	return m_ecs != nullptr ? m_ecs->GetSelectedEntity() : nullptr;
}

void HierarchyWindow::RequestFocusSelectedEntity()
{
	QueueFocusEntity(GetCurrentEntity());
}

void HierarchyWindow::RequestDeleteSelectedEntity()
{
	QueueDeleteSelectedEntitiesRequest();
}

void HierarchyWindow::RequestRenameSelectedEntity()
{
	SceneEntityBase* currentEntity = GetCurrentEntity();
	if (currentEntity == nullptr || m_ecs == nullptr)
		return;
	if (!m_ecs->HasEntity(currentEntity))
		return;
	if (m_ecs->IsEnvironmentEntity(currentEntity))
		return;

	BeginInlineRename(currentEntity);
	QueueFocusEntity(currentEntity);
}

void HierarchyWindow::RequestDuplicateSelectedEntities()
{
	DuplicateSelectedEntities();
}

void HierarchyWindow::BeginInlineRename(SceneEntityBase* entity)
{
	if (entity == nullptr || m_ecs == nullptr)
		return;
	if (!m_ecs->HasEntity(entity))
		return;
	if (m_ecs->IsEnvironmentEntity(entity))
		return;

	// 层级内联重命名：进入编辑态时直接拷贝当前名字到 UTF-8 buffer，
	// 后续由 RenderNode 内的 InputText 就地编辑，不额外弹窗。
	m_inlineRenameEntity = entity;
	m_inlineRenameBuffer = SString::WstringToUTF8(m_ecs->GetEntityName(entity));
	m_inlineRenameErrorMessage.clear();
	m_focusInlineRenameInput = true;
}

void HierarchyWindow::CancelInlineRename()
{
	m_inlineRenameEntity = nullptr;
	m_inlineRenameBuffer.clear();
	m_inlineRenameErrorMessage.clear();
	m_focusInlineRenameInput = false;
}

bool HierarchyWindow::CommitInlineRename(SceneEntityBase* entity)
{
	if (entity == nullptr || m_ecs == nullptr || !m_ecs->HasEntity(entity))
	{
		CancelInlineRename();
		return false;
	}

	const std::wstring currentName = m_ecs->GetEntityName(entity);
	const std::wstring requestedName = SString::UTF8ToWstring(m_inlineRenameBuffer);
	if (requestedName == currentName)
	{
		// 名字未变化时直接退出编辑态，不再做额外提交。
		CancelInlineRename();
		return true;
	}

	if (requestedName.empty())
	{
		m_inlineRenameErrorMessage = L"名称不能为空。";
		m_focusInlineRenameInput = true;
		return false;
	}

	if (!m_ecs->RenameEntity(entity, requestedName))
	{
		// 失败时保留编辑态，允许继续改；某些“点击外部”的路径会在外层把这次失败视作取消。
		m_inlineRenameErrorMessage = L"名称不得与现有同级项目重名。";
		m_focusInlineRenameInput = true;
		return false;
	}

	QueueFocusEntity(entity);
	CancelInlineRename();
	return true;
}

void HierarchyWindow::QueueCreateRequest(std::uint32_t createKind, const std::wstring& defaultName, bool refreshSkyTextures, bool clearSelectionFirst)
{
	m_hasPendingCreateRequest = true;
	m_pendingCreateKind = createKind;
	m_pendingCreateDefaultName = defaultName;
	m_pendingCreateRefreshSkyTextures = refreshSkyTextures;
	m_pendingCreateClearSelectionFirst = clearSelectionFirst;
}

void HierarchyWindow::OpenImportModelBrowser(SceneEntityBase* entityToSelect)
{
	m_importBrowserCurrentDir = EngineUtils::GetProjectDirPath();
	m_importBrowserSelectedPath.clear();
	m_importBrowserTargetEntity = entityToSelect;
	m_openImportBrowserPopup = true;
}

void HierarchyWindow::RenderImportModelBrowser()
{
	if (m_openImportBrowserPopup)
	{
		ImGui::OpenPopup("导入模型");
		m_openImportBrowserPopup = false;
	}

	if (!ImGui::BeginPopupModal("导入模型", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	if (m_importBrowserCurrentDir.empty())
		m_importBrowserCurrentDir = EngineUtils::GetProjectDirPath();

	ImGui::Text("当前目录：");
	ImGui::TextWrapped("%s", SString::WstringToUTF8(m_importBrowserCurrentDir).c_str());

	const std::filesystem::path currentDirPath(m_importBrowserCurrentDir);
	const std::filesystem::path projectDirPath(EngineUtils::GetProjectDirPath());

	if (currentDirPath.has_parent_path() && currentDirPath != projectDirPath)
	{
		if (ImGui::Button("上一级"))
		{
			m_importBrowserCurrentDir = currentDirPath.parent_path().wstring();
			m_importBrowserSelectedPath.clear();
		}
	}

	ImGui::Separator();

	std::vector<std::filesystem::directory_entry> entries;
	try
	{
		for (const auto& entry : std::filesystem::directory_iterator(currentDirPath))
			entries.push_back(entry);
	}
	catch (const std::filesystem::filesystem_error&)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "无法读取当前目录。");
	}

	std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs)
	{
		const bool lhsDir = lhs.is_directory();
		const bool rhsDir = rhs.is_directory();
		if (lhsDir != rhsDir)
			return lhsDir && !rhsDir;

		std::wstring lhsName = lhs.path().filename().wstring();
		std::wstring rhsName = rhs.path().filename().wstring();
		std::transform(lhsName.begin(), lhsName.end(), lhsName.begin(), towlower);
		std::transform(rhsName.begin(), rhsName.end(), rhsName.begin(), towlower);
		if (lhsName != rhsName)
			return lhsName < rhsName;

		return lhs.path().filename().wstring() < rhs.path().filename().wstring();
	});

	const ImVec2 listSize((std::max)(420.0f, ImGui::GetContentRegionAvail().x), 320.0f);
	if (ImGui::BeginChild("ImportModelBrowserList", listSize, ImGuiChildFlags_None))
	{
		for (const auto& entry : entries)
		{
			const std::filesystem::path entryPath = entry.path();
			const std::wstring entryName = entryPath.filename().wstring();
			if (entry.is_directory())
			{
				const std::string label = "[目录] " + SString::WstringToUTF8(entryName);
				if (ImGui::Selectable(label.c_str(), false))
				{
					m_importBrowserCurrentDir = entryPath.wstring();
					m_importBrowserSelectedPath.clear();
				}
				continue;
			}

			const FILEs::File_Type fileType = FILEs::extensionToFileType(entryPath.extension().wstring());
			if (!IsImportableModelFileType(fileType))
				continue;

			const bool isSelected = m_importBrowserSelectedPath == entryPath.wstring();
			if (ImGui::Selectable(SString::WstringToUTF8(entryName).c_str(), isSelected))
				m_importBrowserSelectedPath = entryPath.wstring();
		}
	}
	ImGui::EndChild();

	if (!m_importBrowserSelectedPath.empty())
	{
		ImGui::Separator();
		ImGui::Text("已选择：");
		ImGui::TextWrapped("%s", SString::WstringToUTF8(m_importBrowserSelectedPath).c_str());
	}

	ImGui::Separator();
	const bool canImport = !m_importBrowserSelectedPath.empty();
	if (ImGui::Button("导入", ImVec2(96.0f, 0.0f)) && canImport)
	{
		QueueImportRequest(
			m_importBrowserSelectedPath,
			std::filesystem::path(m_importBrowserSelectedPath).stem().wstring(),
			m_importBrowserTargetEntity);
		m_importBrowserSelectedPath.clear();
		m_importBrowserTargetEntity = nullptr;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("取消", ImVec2(96.0f, 0.0f)))
	{
		m_importBrowserSelectedPath.clear();
		m_importBrowserTargetEntity = nullptr;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void HierarchyWindow::NeedRender(bool render)
{
	renderHierarchy = render;
}

void HierarchyWindow::RenderTree()
{
	if (m_ecs == nullptr)
		return;

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& m_inlineRenameEntity == nullptr
		&& !ImGui::IsAnyItemActive()
		&& ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		QueueDeleteSelectedEntitiesRequest();
	}

	///////////////////////////////////////////////////////////
	ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_DefaultOpen;

	bool node_open = ImGui::TreeNodeEx("世界根节点", tree_flags);

	if (ImGui::BeginDragDropTarget())
	{
		const ImGuiPayload* dragPayload = ImGui::GetDragDropPayload();
		if (dragPayload != nullptr &&
			dragPayload->IsDataType(HIERARCHY_ENTITY_PAYLOAD) &&
			dragPayload->DataSize == sizeof(SceneEntityBase*))
		{
			SceneEntityBase* const* draggedEntity = static_cast<SceneEntityBase* const*>(dragPayload->Data);
			SceneEntityBase* dragged = draggedEntity != nullptr ? *draggedEntity : nullptr;
			const bool canDrop = dragged != nullptr && m_ecs->CanReparentEntityInHierarchy(dragged, nullptr);
			bool canDropSelection = canDrop;
			if (dragged != nullptr && m_ecs->IsEntitySelectedInHierarchy(dragged))
			{
				const std::vector<SceneEntityBase*> selectedRoots = m_ecs->GetHierarchySelectionRootSnapshot();
				for (SceneEntityBase* selectedRoot : selectedRoots)
				{
					if (!m_ecs->CanReparentEntityInHierarchy(selectedRoot, nullptr))
					{
						canDropSelection = false;
						break;
					}
				}
			}
			if (dragPayload->Preview)
				DrawHierarchyDropTargetHighlight(canDropSelection);

			if (canDropSelection)
			{
				if (const ImGuiPayload* acceptedPayload =
					ImGui::AcceptDragDropPayload(HIERARCHY_ENTITY_PAYLOAD, ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
				{
					SceneEntityBase* const* acceptedEntity = static_cast<SceneEntityBase* const*>(acceptedPayload->Data);
					if (acceptedPayload->Delivery && acceptedEntity != nullptr)
					{
						if (m_ecs->IsEntitySelectedInHierarchy(*acceptedEntity))
							QueueReparentSelectedEntitiesRequest(nullptr, true);
						else
							QueueReparentRequest(*acceptedEntity, nullptr);
					}
				}
			}
		}

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_DEMO_ASS"))
		{
			const AssetDragPayload* file = static_cast<const AssetDragPayload*>(payload->Data);
			if (file != nullptr && !file->is_dir &&
				(file->file_type == FILEs::File_Type::OBJFILE ||
					file->file_type == FILEs::File_Type::GLTFFILE ||
					file->file_type == FILEs::File_Type::GLBFILE ||
					file->file_type == FILEs::File_Type::WMODELFILE))
			{
				QueueImportRequest(file->full_path, file->file_name_only);
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsItemHovered())
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (!(ImGui::GetIO().KeyShift || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0))
				m_ecs->ClearHierarchySelection();
			ClearSelectedSkeletonBone();
		}

	if (ImGui::BeginPopupContextWindow("HierarchyBlankContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::BeginMenu("创建"))
		{
			if (ImGui::MenuItem("空的"))
				QueueCreateRequest(kCreateItemEmpty, L"空的", false, true);
			if (ImGui::MenuItem("骨骼"))
				QueueCreateRequest(kCreateItemSkeleton, L"骨骼", false, true);
			ImGui::Separator();
			if (ImGui::MenuItem("天空"))
				QueueCreateRequest(kCreateItemSky, L"天空", true, true);
			if (ImGui::MenuItem("盒子"))
				QueueCreateRequest(kCreateItemBox, L"盒子", false, true);
			if (ImGui::MenuItem("球体"))
				QueueCreateRequest(kCreateItemSphere, L"球体", false, true);
			if (ImGui::MenuItem("胶囊"))
				QueueCreateRequest(kCreateItemCapsule, L"胶囊", false, true);
			if (ImGui::MenuItem("平面"))
				QueueCreateRequest(kCreateItemPlane, L"平面", false, true);
			if (ImGui::MenuItem("告示牌"))
				QueueCreateRequest(kCreateItemBillboard, L"告示牌", false, true);
			ImGui::Separator();
			if (ImGui::MenuItem("相机"))
				QueueCreateRequest(kCreateItemCamera, L"相机", false, true);
			if (ImGui::MenuItem("灯光"))
				QueueCreateRequest(kCreateItemLight, L"灯光", false, true);
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("导入模型..."))
			OpenImportModelBrowser(nullptr);
		ImGui::EndPopup();
	}

	if (node_open)
	{
		const std::vector<SceneEntityBase*>& rootEntities = m_ecs->GetHierarchyRootEntities();
		for (UINT i = 0; i < m_ecs->GetHierarchyRootEntityCount(); ++i)
		{
			RenderNode(rootEntities[i]);
		}
		ImGui::TreePop();
	}
}

void HierarchyWindow::RenderNode(SceneEntityBase* ent)
{
	const bool isEnvironmentEntity = m_ecs != nullptr && m_ecs->IsEnvironmentEntity(ent);
	SceneEntityBase* skeletonOwnerEntity = nullptr;
	std::int32_t skeletonBoneIndex = -2;
	const bool isSkeletonHierarchyNode =
		m_ecs != nullptr &&
		m_ecs->TryGetSkeletonHierarchyBinding(
			ent,
			&skeletonOwnerEntity,
			&skeletonBoneIndex,
			nullptr);
	// Skeleton hierarchy entities are an implementation detail for the skeleton
	// editor.  The regular scene hierarchy only shows user-authored entities.
	if (isSkeletonHierarchyNode || IsHiddenSkeletonHierarchyNode(ent))
		return;
	const Witchcraft::Animation::SkeletonData* skeletonData = m_ecs != nullptr ? m_ecs->GetSkeletonData(ent) : nullptr;
	bool hasVisibleChildren = isEnvironmentEntity;
	if (m_ecs != nullptr)
	{
		for (SceneEntityBase* childEntity : m_ecs->GetHierarchyChildren(ent))
		{
			if (childEntity != nullptr && !IsHiddenSkeletonHierarchyNode(childEntity))
			{
				hasVisibleChildren = true;
				break;
			}
		}
	}
	const bool hasChildren = hasVisibleChildren;
	ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_DefaultOpen;

	if (m_pendingFocusEntity != nullptr)
	{
		SceneEntityBase* current = m_pendingFocusEntity;
		while (current != nullptr)
		{
			if (current == ent)
			{
				ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				break;
			}
			current = m_ecs->GetParentEntity(current);
		}
	}

	if (!hasChildren)
		tree_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	if (m_ecs != nullptr && m_ecs->IsEntitySelectedInHierarchy(ent))
		tree_flags |= ImGuiTreeNodeFlags_Selected;
	if (isSkeletonHierarchyNode &&
		m_selectedSkeletonOwnerEntity == skeletonOwnerEntity &&
		m_selectedSkeletonBoneIndex == skeletonBoneIndex)
	{
		tree_flags |= ImGuiTreeNodeFlags_Selected;
	}
	const std::wstring entityName = m_ecs->GetEntityName(ent);
	const std::string entityLabel = SString::WstringToUTF8(entityName);
	// 仅允许一个节点处于内联重命名状态；命中的节点把 label 区域替换成 InputText。
	const bool isInlineRenaming = (m_inlineRenameEntity == ent);

	ImGui::PushID(ent);
	bool node_open = false;
	if (isInlineRenaming)
		node_open = ImGui::TreeNodeEx("##HierarchyEntityNode", tree_flags, "%s", "");
	else
		node_open = ImGui::TreeNodeEx("##HierarchyEntityNode", tree_flags, "%s", entityLabel.c_str());

	if (isInlineRenaming)
	{
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (m_focusInlineRenameInput)
		{
			ImGui::SetKeyboardFocusHere();
			m_focusInlineRenameInput = false;
		}

		ImGui::InputText("##HierarchyInlineRename", &m_inlineRenameBuffer);
		const bool inputFocused = ImGui::IsItemFocused();
		const bool inputDeactivated = ImGui::IsItemDeactivated();
		if (ImGui::IsItemFocused())
		{
			// 明确约定：Enter 提交，Esc 取消。
			if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
			{
				ImGui::ClearActiveID();
				CommitInlineRename(ent);
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				ImGui::ClearActiveID();
				CancelInlineRename();
			}
		}
		else if (inputDeactivated && !inputFocused)
		{
			// 点击层级外部/其他控件导致失焦时，把这次离开编辑态视作一次确认；
			// 若确认失败（例如重名），则直接取消，避免卡在不可继续输入的状态。
			ImGui::ClearActiveID();
			if (!CommitInlineRename(ent))
				CancelInlineRename();
		}
	}

	if (m_pendingFocusScroll)
		ApplyPendingFocusForNode(ent);
	const bool additiveSelection = ImGui::GetIO().KeyShift || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

	// 展开箭头也是 TreeNode item 的一部分。不能把它当成选择骨骼节点，
	// 否则选择链路会排队聚焦该节点，并在下一帧强制重新展开其祖先。
	if (!isInlineRenaming &&
		ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
		!ImGui::IsItemToggledOpen())
	{
		if (m_inlineRenameEntity != nullptr && m_inlineRenameEntity != ent)
		{
			// 点击其他层级节点切换选择时，先显式收掉旧节点的重命名状态，
			// 再继续这次选中，避免“父子之间切换选择”被失焦逻辑吞掉。
			SceneEntityBase* renameEntity = m_inlineRenameEntity;
			ImGui::ClearActiveID();
			if (!CommitInlineRename(renameEntity))
				CancelInlineRename();
		}

		if (isSkeletonHierarchyNode && skeletonOwnerEntity != nullptr)
		{
			if (skeletonBoneIndex < 0)
			{
				if (const Witchcraft::Animation::SkeletonData* skeletonData = m_ecs->GetSkeletonData(skeletonOwnerEntity))
				{
					if (skeletonData->Topology.IsValidBoneIndex(skeletonData->Topology.RootBoneIndex))
						skeletonBoneIndex = skeletonData->Topology.RootBoneIndex;
				}
			}

			m_selectedSkeletonOwnerEntity = skeletonOwnerEntity;
			m_selectedSkeletonBoneIndex = skeletonBoneIndex;
			m_ecs->SelectEntityForHierarchy(skeletonOwnerEntity, false);
		}
		else
		{
			ClearSelectedSkeletonBone();
			m_ecs->SelectEntityForHierarchy(ent, additiveSelection);
			if (m_consoleWindow != nullptr)
				m_consoleWindow->AddDebugMessage(L"[Hierarchy] click: name=%s, shift=%d", m_ecs->GetEntityName(ent).c_str(), additiveSelection ? 1 : 0);
		}
	}

	if (!isSkeletonHierarchyNode && ImGui::BeginDragDropSource())
	{
		SceneEntityBase* payloadEntity = ent;
		m_dragSelectionEntities.clear();
		if (m_ecs->IsEntitySelectedInHierarchy(ent))
			m_dragSelectionEntities = m_ecs->GetHierarchySelectionRootSnapshot();
		ImGui::SetDragDropPayload(HIERARCHY_ENTITY_PAYLOAD, &payloadEntity, sizeof(payloadEntity));
		if (m_dragSelectionEntities.size() > 1)
			ImGui::Text("移动所选 %d 个实体", static_cast<int>(m_dragSelectionEntities.size()));
		else
			ImGui::TextUnformatted(entityLabel.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginPopupContextItem("HierarchyEntityContextMenu"))
	{
		SceneEntityBase* contextEntity = ent;
		if (isSkeletonHierarchyNode && skeletonOwnerEntity != nullptr)
			contextEntity = skeletonOwnerEntity;
		if (!m_ecs->IsEntitySelectedInHierarchy(contextEntity))
			m_ecs->SelectEntityForHierarchy(contextEntity, false);
		const int selectedCount = static_cast<int>(m_ecs->GetHierarchySelectionRootSnapshot().size());
		const bool canRebuildSkeletonHierarchy =
			!isSkeletonHierarchyNode &&
			skeletonData != nullptr &&
			!skeletonData->Topology.Bones.empty();
		if (canRebuildSkeletonHierarchy && ImGui::MenuItem("重建骨骼子层级"))
		{
			(void)m_ecs->RebuildSkeletonHierarchyForEntity(ent);
			QueueFocusEntity(ent);
		}
		if (!isSkeletonHierarchyNode && !m_ecs->IsEnvironmentEntity(ent) && ImGui::MenuItem("重命名"))
		{
			BeginInlineRename(ent);
		}
		const std::string duplicateLabel = selectedCount > 1 ? ("重复所选 (" + std::to_string(selectedCount) + ")") : "重复";
		if (!isSkeletonHierarchyNode && ImGui::MenuItem(duplicateLabel.c_str()))
		{
			DuplicateSelectedEntities();
		}
		const bool canDelete =
			!isSkeletonHierarchyNode &&
			!m_ecs->IsEnvironmentEntity(ent) &&
			!m_ecs->IsAmbientLightEntity(ent);
		const std::string deleteLabel = selectedCount > 1 ? ("删除所选 (" + std::to_string(selectedCount) + ")") : "删除";
		if (canDelete && ImGui::MenuItem(deleteLabel.c_str()))
		{
			QueueDeleteSelectedEntitiesRequest();
		}
		ImGui::EndPopup();
	}

	if (!isSkeletonHierarchyNode && ImGui::BeginDragDropTarget())
	{
		const ImGuiPayload* dragPayload = ImGui::GetDragDropPayload();
		if (dragPayload != nullptr &&
			dragPayload->IsDataType(HIERARCHY_ENTITY_PAYLOAD) &&
			dragPayload->DataSize == sizeof(SceneEntityBase*))
		{
			SceneEntityBase* const* draggedEntity = static_cast<SceneEntityBase* const*>(dragPayload->Data);
			SceneEntityBase* dragged = draggedEntity != nullptr ? *draggedEntity : nullptr;
			const bool canDrop = dragged != nullptr && m_ecs->CanReparentEntityInHierarchy(dragged, ent);
			bool canDropSelection = canDrop;
			if (dragged != nullptr && m_ecs->IsEntitySelectedInHierarchy(dragged))
			{
				const std::vector<SceneEntityBase*> selectedRoots = m_ecs->GetHierarchySelectionRootSnapshot();
				for (SceneEntityBase* selectedRoot : selectedRoots)
				{
					if (!m_ecs->CanReparentEntityInHierarchy(selectedRoot, ent))
					{
						canDropSelection = false;
						break;
					}
				}
			}
			if (dragPayload->Preview)
				DrawHierarchyDropTargetHighlight(canDropSelection);

			if (canDropSelection)
			{
				if (const ImGuiPayload* acceptedPayload =
					ImGui::AcceptDragDropPayload(HIERARCHY_ENTITY_PAYLOAD, ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
				{
					SceneEntityBase* const* acceptedEntity = static_cast<SceneEntityBase* const*>(acceptedPayload->Data);
					if (acceptedPayload->Delivery && acceptedEntity != nullptr)
					{
						if (m_ecs->IsEntitySelectedInHierarchy(*acceptedEntity))
							QueueReparentSelectedEntitiesRequest(ent, false);
						else
							QueueReparentRequest(*acceptedEntity, ent);
					}
				}
			}
		}

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_DEMO_ASS"))
		{
			const AssetDragPayload* file = static_cast<const AssetDragPayload*>(payload->Data);
			if (file != nullptr && !file->is_dir &&
				(file->file_type == FILEs::File_Type::OBJFILE ||
					file->file_type == FILEs::File_Type::GLTFFILE ||
					file->file_type == FILEs::File_Type::GLBFILE ||
					file->file_type == FILEs::File_Type::WMODELFILE))
			{
				QueueImportRequest(file->full_path, file->file_name_only, ent);
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (isInlineRenaming && !m_inlineRenameErrorMessage.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", SString::WstringToUTF8(m_inlineRenameErrorMessage).c_str());

	if (node_open && hasChildren)
	{
		const std::vector<SceneEntityBase*>& childrenEntity = m_ecs->GetHierarchyChildren(ent);
		for (UINT i = 0; i < childrenEntity.size(); i++)
		{
			if (childrenEntity[i] != nullptr && !IsHiddenSkeletonHierarchyNode(childrenEntity[i]))
				RenderNode(childrenEntity[i]);
		}

		ImGui::TreePop();
	}
	ImGui::PopID();
}

void HierarchyWindow::QueueImportRequest(const std::wstring& filePath, const std::wstring& fileName, SceneEntityBase* entityToSelect)
{
	if (m_ecs != nullptr && entityToSelect != nullptr)
		m_ecs->SelectEntityForHierarchy(entityToSelect);
	if (entityToSelect != nullptr)
		QueueFocusEntity(entityToSelect);

	m_importFilePath = filePath;
	m_importEntityName = fileName;
	m_importTransform = Transform{};
	m_importSceneType = SceneEntityType::StaticScenery;
	m_openImportWindow = true;
}

void HierarchyWindow::QueueReparentRequest(SceneEntityBase* entity, SceneEntityBase* newParent)
{
	if (m_ecs == nullptr || entity == nullptr)
		return;

	m_pendingReparentEntity = entity;
	m_pendingReparentNewParent = newParent;
	m_hasPendingReparentRequest = true;
}

void HierarchyWindow::QueueDeleteRequest(SceneEntityBase* entity)
{
	if (entity == nullptr || m_ecs == nullptr)
		return;
	if (m_ecs->IsEnvironmentEntity(entity))
		return;
	if (m_ecs->IsAmbientLightEntity(entity))
		return;

	m_deleteCandidateEntity = entity;
	m_deleteCandidateEntities.clear();
	m_deleteCandidateEntities.push_back(entity);
	m_deleteCandidateEntityName = m_ecs->GetEntityName(entity);
	m_deleteCandidateHasChildren = m_ecs->GetHierarchyChildCount(entity) > 0;
	m_openDeleteConfirmPopup = true;
}

void HierarchyWindow::QueueDeleteSelectedEntitiesRequest()
{
	if (m_ecs == nullptr)
		return;

	std::vector<SceneEntityBase*> selectedRoots = m_ecs->GetHierarchySelectionRootSnapshot();
	selectedRoots.erase(
		std::remove_if(selectedRoots.begin(), selectedRoots.end(),
			[this](SceneEntityBase* entity)
			{
				return entity == nullptr
					|| !m_ecs->HasEntity(entity)
					|| m_ecs->IsEnvironmentEntity(entity)
					|| m_ecs->IsAmbientLightEntity(entity);
			}),
		selectedRoots.end());

	if (selectedRoots.empty())
		return;

	m_deleteCandidateEntities = selectedRoots;
	m_deleteCandidateEntity = selectedRoots.front();
	m_deleteCandidateEntityName =
		selectedRoots.size() == 1
		? m_ecs->GetEntityName(selectedRoots.front())
		: (L"已选择 " + std::to_wstring(selectedRoots.size()) + L" 个实体");
	m_deleteCandidateHasChildren = std::any_of(
		selectedRoots.begin(),
		selectedRoots.end(),
		[this](SceneEntityBase* entity)
		{
			return entity != nullptr && m_ecs->GetHierarchyChildCount(entity) > 0;
		});
	m_openDeleteConfirmPopup = true;
}

void HierarchyWindow::DuplicateSelectedEntities()
{
	if (m_ecs == nullptr)
		return;

	std::vector<SceneEntityBase*> selectedRoots = m_ecs->GetHierarchySelectionRootSnapshot();
	selectedRoots.erase(
		std::remove_if(selectedRoots.begin(), selectedRoots.end(),
			[this](SceneEntityBase* entity)
			{
				return entity == nullptr
					|| !m_ecs->HasEntity(entity)
					|| m_ecs->IsEnvironmentEntity(entity);
			}),
		selectedRoots.end());

	if (selectedRoots.empty())
		return;

	std::vector<SceneEntityBase*> duplicatedRoots;
	duplicatedRoots.reserve(selectedRoots.size());
	for (SceneEntityBase* entity : selectedRoots)
	{
		SceneEntityBase* duplicated = m_ecs->DuplicateEntityHierarchy(entity, m_dx, m_engine);
		if (duplicated != nullptr)
			duplicatedRoots.push_back(duplicated);
	}

	if (duplicatedRoots.empty())
		return;

	m_ecs->ClearHierarchySelection();
	for (size_t i = 0; i < duplicatedRoots.size(); ++i)
		m_ecs->SelectEntityForHierarchy(duplicatedRoots[i], i != 0);

	QueueFocusEntity(duplicatedRoots.back());
}

void HierarchyWindow::QueueReparentSelectedEntitiesRequest(SceneEntityBase* dropTargetEntity, bool dropToRoot)
{
	if (m_ecs == nullptr)
		return;

	std::vector<SceneEntityBase*> selectedRoots = m_ecs->GetHierarchySelectionRootSnapshot();
	selectedRoots.erase(
		std::remove_if(selectedRoots.begin(), selectedRoots.end(),
			[this](SceneEntityBase* entity)
			{
				return entity == nullptr || !m_ecs->HasEntity(entity);
			}),
		selectedRoots.end());

	if (selectedRoots.empty())
		return;

	SceneEntityBase* newParent = dropToRoot ? nullptr : dropTargetEntity;
	for (SceneEntityBase* entity : selectedRoots)
	{
		if (!m_ecs->CanReparentEntityInHierarchy(entity, newParent))
			return;
	}

	m_pendingReparentEntities = selectedRoots;
	m_pendingReparentEntity = selectedRoots.front();
	m_pendingReparentNewParent = newParent;
	m_hasPendingReparentRequest = true;
}

void HierarchyWindow::ProcessPendingReparentRequest()
{
	if (!m_hasPendingReparentRequest)
		return;
	if (m_ecs == nullptr)
	{
		m_hasPendingReparentRequest = false;
		m_pendingReparentEntity = nullptr;
		m_pendingReparentEntities.clear();
		m_pendingReparentNewParent = nullptr;
		return;
	}

	m_hasPendingReparentRequest = false;
	std::vector<SceneEntityBase*> entities = m_pendingReparentEntities;
	if (entities.empty() && m_pendingReparentEntity != nullptr)
		entities.push_back(m_pendingReparentEntity);
	SceneEntityBase* entity = m_pendingReparentEntity;
	SceneEntityBase* newParent = m_pendingReparentNewParent;
	m_pendingReparentEntity = nullptr;
	m_pendingReparentEntities.clear();
	m_pendingReparentNewParent = nullptr;

	if (entities.empty())
		return;
	if (newParent != nullptr && !m_ecs->HasEntity(newParent))
		return;

	bool anyReparented = false;
	for (SceneEntityBase* reparentEntity : entities)
	{
		if (reparentEntity == nullptr || !m_ecs->HasEntity(reparentEntity))
			continue;
		if (!m_ecs->CanReparentEntityInHierarchy(reparentEntity, newParent))
			continue;
		if (m_ecs->ReparentEntityInHierarchy(reparentEntity, newParent))
			anyReparented = true;
	}

	if (anyReparented && m_dx != nullptr)
		m_dx->RebuildRenderItemsFromEntities(m_ecs);
	if (anyReparented && entity != nullptr)
		QueueFocusEntity(entity);
}

const wchar_t* HierarchyWindow::GetCreateLightTypeLabel(UINT lightType)
{
	switch (lightType)
	{
	case 0:
		return L"定向光（平行光）";
	case 1:
		return L"聚光";
	case 2:
		return L"点光";
	default:
		return L"定向光（平行光）";
	}
}

std::string HierarchyWindow::FormatTransformValue(float value)
{
	char buffer[32] = {};
	sprintf_s(buffer, sizeof(buffer), "%.6f", value);
	return std::string(buffer);
}

bool HierarchyWindow::TryParseFloatInput(const std::string& text, float* outValue)
{
	if (outValue == nullptr)
		return false;

	std::string trimmed = text;
	trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) { return !std::isspace(ch); }));
	trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), trimmed.end());
	if (trimmed.empty())
		return false;

	char* endPtr = nullptr;
	const float parsedValue = strtof(trimmed.c_str(), &endPtr);
	if (endPtr == nullptr)
		return false;

	while (*endPtr != '\0' && std::isspace(static_cast<unsigned char>(*endPtr)))
		++endPtr;
	if (*endPtr != '\0')
		return false;

	*outValue = parsedValue;
	return true;
}

bool HierarchyWindow::IsImportableModelFileType(UINT fileType)
{
	return fileType == FILEs::File_Type::OBJFILE
		|| fileType == FILEs::File_Type::GLTFFILE
		|| fileType == FILEs::File_Type::GLBFILE
		|| fileType == FILEs::File_Type::WMODELFILE;
}

void HierarchyWindow::ProcessPendingDeleteRequest()
{
	if (!m_hasPendingDeleteRequest)
		return;
	if (m_ecs == nullptr)
	{
		m_hasPendingDeleteRequest = false;
		m_pendingDeleteEntity = nullptr;
		return;
	}

	m_hasPendingDeleteRequest = false;
	std::vector<SceneEntityBase*> deleteTargets = m_pendingDeleteEntities;
	if (deleteTargets.empty() && m_pendingDeleteEntity != nullptr)
		deleteTargets.push_back(m_pendingDeleteEntity);
	m_pendingDeleteEntity = nullptr;
	m_pendingDeleteEntities.clear();
	if (deleteTargets.empty())
		return;

	// “同时删除子实体”不会改变剩余实体的父子结构，
	// 因此这里可以直接局部移除对应子树的 RenderItem，
	// 避免整棵场景做一次全量重建。
	if (m_pendingDeleteChildren && m_dx != nullptr)
	{
		for (SceneEntityBase* deleteTargetEntity : deleteTargets)
		{
			if (deleteTargetEntity != nullptr && m_ecs->HasEntity(deleteTargetEntity) && !m_ecs->IsEnvironmentEntity(deleteTargetEntity))
				m_dx->RemoveRenderItemsFromEntity(deleteTargetEntity, m_ecs);
		}
	}

	for (SceneEntityBase* deleteTargetEntity : deleteTargets)
	{
		if (deleteTargetEntity == nullptr || !m_ecs->HasEntity(deleteTargetEntity))
			continue;
		if (m_ecs->IsEnvironmentEntity(deleteTargetEntity))
			continue;
		m_ecs->DeleteEntityFromHierarchy(deleteTargetEntity, m_pendingDeleteChildren);
	}

	// 只有“保留子实体并上移一层”这类层级重挂场景，
	// 才仍然走全量 rebuild，确保世界矩阵和渲染项集合整体一致。
	if (!m_pendingDeleteChildren && m_dx != nullptr)
		m_dx->RebuildRenderItemsFromEntities(m_ecs);
}

void HierarchyWindow::ProcessPendingImportRequest()
{
	if (!m_hasPendingImportRequest)
		return;
	if (m_assimpLoader == nullptr || m_ecs == nullptr)
	{
		m_hasPendingImportRequest = false;
		return;
	}

	m_hasPendingImportRequest = false;

	if (!m_assimpLoader->ImportModelToScene(
		m_pendingImportFilePath,
		m_ecs,
		m_pendingImportEntityName,
		m_pendingImportTransform,
		m_pendingImportSceneType))
	{
		m_operationErrorMessage = L"模型导入失败。";
		m_openOperationErrorPopup = true;
	}
	else
	{
		QueueFocusEntity(m_ecs->GetSelectedEntity());
	}
}
