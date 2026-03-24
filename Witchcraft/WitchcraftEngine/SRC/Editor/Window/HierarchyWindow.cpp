#include "HierarchyWindow.h"

#include "ConsoleWindow.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "ECS/WitchcraECS.h"
#include "ECS/COMPONENT/GeneralComponent.h"
#include "ENGINE/EngineUtils.h"
#include "String/SStringUtils.h"
#include "D3DWindow/D3DWindow.h"
#include "System/WitchcraftFile/WMaterialFile.h"
#include "System/Assets.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

#include <misc/cpp/imgui_stdlib.h>

namespace
{
	constexpr const char* HIERARCHY_ENTITY_PAYLOAD = "DND_HIERARCHY_ENTITY";
	constexpr UINT DEFAULT_CREATE_LIGHT_TYPE = 1;

	const wchar_t* GetCreateLightTypeLabel(UINT lightType)
	{
		switch (lightType)
		{
		case 0:
			return L"环境光";
		case 1:
			return L"定向光（平行光）";
		case 2:
			return L"聚光";
		case 3:
			return L"点光";
		default:
			return L"定向光（平行光）";
		}
	}
}

void HierarchyWindow::Init(ConsoleWindow* consoleWindow, AssimpLoader* assimpLoader, WitchcraECS* ecs, D3DWindow* dx)
{
	m_consoleWindow = consoleWindow;
	m_assimpLoader = assimpLoader;
	m_ecs = ecs;
	m_dx = dx;
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
		if (m_deleteCandidateEntity != nullptr)
			RenderDeleteImpactTree(m_deleteCandidateEntity);
		else
			ImGui::BulletText("%s", SString::WstringToUTF8(m_deleteCandidateEntityName).c_str());

		ImGui::Separator();

		if (m_deleteCandidateHasChildren)
		{
			ImGui::Text("实体 \"%s\" 拥有子实体。", SString::WstringToUTF8(m_deleteCandidateEntityName).c_str());
			ImGui::Text("是否同时删除子实体？");
			ImGui::Text("点击“否”会把子实体移动到上一层。");

			if (ImGui::Button("是"))
			{
				m_pendingDeleteEntity = m_deleteCandidateEntity;
				m_pendingDeleteChildren = true;
				m_hasPendingDeleteRequest = true;
				m_deleteCandidateEntity = nullptr;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("否"))
			{
				m_pendingDeleteEntity = m_deleteCandidateEntity;
				m_pendingDeleteChildren = false;
				m_hasPendingDeleteRequest = true;
				m_deleteCandidateEntity = nullptr;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("取消"))
			{
				m_deleteCandidateEntity = nullptr;
				ImGui::CloseCurrentPopup();
			}
		}
		else
		{
			ImGui::Text("确认删除实体 \"%s\" 吗？", SString::WstringToUTF8(m_deleteCandidateEntityName).c_str());

			if (ImGui::Button("删除"))
			{
				m_pendingDeleteEntity = m_deleteCandidateEntity;
				m_pendingDeleteChildren = true;
				m_hasPendingDeleteRequest = true;
				m_deleteCandidateEntity = nullptr;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("取消"))
			{
				m_deleteCandidateEntity = nullptr;
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}

	if (m_openImportWindow)
	{
		if (CreateComponentWindow(&m_openImportWindow, &m_importEntityName, &m_importTransform))
		{
			// 导入会创建/销毁 D3D 资源，不能在 ImGui 渲染命令录制期间直接执行。
			m_pendingImportFilePath = m_importFilePath;
			m_pendingImportEntityName = m_importEntityName;
			m_pendingImportTransform = m_importTransform;
			m_hasPendingImportRequest = true;
		}
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

bool HierarchyWindow::CreateComponentWindow(
	bool* pOpen,
	std::wstring* name,
	Transform* transform,
	std::wstring* materialFilePath,
	UINT* lightType,
	SceneEntityBase* siblingScopeParent)
{
	if (ImGui::Begin("创建实体", pOpen, ImGuiWindowFlags_NoDocking))
	{
		ImGui::Text("名称：");
		ImGui::SameLine();
		std::string tmp = "";
		tmp = SString::WstringToUTF8(*name);
		if (ImGui::InputText("##NameComponent", &tmp))
			*name = SString::UTF8ToWstring(tmp);

		// 如果transform是空的就不需要设置，这部分也就不需要显示
		if (transform)
		{
			// 位置
			{
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3 / (2 * 0.875));

				ImGui::Text("位置：");
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text("X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char X[12] = "0.000000000";
				sprintf_s(X, 12, "%f", transform->position.x);
				std::string mX = X;
				if (ImGui::InputText("##PotXTransformComp", &mX))
					transform->position.x = atof(mX.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text("Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Y[12] = "0.000000000";
				sprintf_s(Y, 12, "%f", transform->position.y);
				std::string mY = Y;
				if (ImGui::InputText("##PotYTransformComp", &mY))
					transform->position.y = atof(mY.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text("Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Z[12] = "0.000000000";
				sprintf_s(Z, 12, "%f", transform->position.z);
				std::string mZ = Z;
				if (ImGui::InputText("##PotZTransformComp", &mZ))
					transform->position.z = atof(mZ.c_str());
			}
			// 旋转
			{
				ImGui::Text("旋转：");
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text("X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char X[12] = "0.000000000";
				sprintf_s(X, 12, "%f", transform->rotation.x);
				std::string mX = X;
				if (ImGui::InputText("##RotXTransformComp", &mX))
					transform->rotation.x = atof(mX.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text("Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Y[12] = "0.000000000";
				sprintf_s(Y, 12, "%f", transform->rotation.y);
				std::string mY = Y;
				if (ImGui::InputText("##RotYTransformComp", &mY))
					transform->rotation.y = atof(mY.c_str());

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text("Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Z[12] = "0.000000000";
				sprintf_s(Z, 12, "%f", transform->rotation.z);
				std::string mZ = Z;
				if (ImGui::InputText("##RotZTransformComp", &mZ))
					transform->rotation.z = atof(mZ.c_str());
			}
			// 缩放
			{
				transform->scale.x = std::max(0.0f, transform->scale.x);
				transform->scale.y = std::max(0.0f, transform->scale.y);
				transform->scale.z = std::max(0.0f, transform->scale.z);

				ImGui::Text("缩放：");
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text("X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char X[12] = "1.000000000";
				sprintf_s(X, 12, "%f", transform->scale.x);
				std::string mX = X;
				if (ImGui::InputText("##ScaXTransformComp", &mX))
					transform->scale.x = static_cast<float>(std::max(0.0, atof(mX.c_str())));

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text("Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Y[12] = "1.000000000";
				sprintf_s(Y, 12, "%f", transform->scale.y);
				std::string mY = Y;
				if (ImGui::InputText("##ScaYTransformComp", &mY))
					transform->scale.y = static_cast<float>(std::max(0.0, atof(mY.c_str())));

				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text("Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				char Z[12] = "1.000000000";
				sprintf_s(Z, 12, "%f", transform->scale.z);
				std::string mZ = Z;
				if (ImGui::InputText("##ScaZTransformComp", &mZ))
					transform->scale.z = static_cast<float>(std::max(0.0, atof(mZ.c_str())));
			}
		}

		if (materialFilePath != nullptr)
		{
			struct MaterialFileEntry
			{
				std::wstring path;
				std::wstring displayName;
				std::wstring relativePath;
			};

			std::vector<MaterialFileEntry> materialFileEntries;
			const std::filesystem::path importedAssetsDir =
				std::filesystem::path(EngineUtils::GetProjectDirPath()) / L"ImportedAssets";
			if (!importedAssetsDir.empty() && std::filesystem::exists(importedAssetsDir))
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(importedAssetsDir))
				{
					if (!entry.is_regular_file())
						continue;

					std::wstring extension = entry.path().extension().wstring();
					std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
					if (extension != L".wmat")
						continue;

					WMaterialFileData materialData;
					std::wstring displayName = entry.path().stem().wstring();
					if (WMaterialFile::LoadFromFile(entry.path(), &materialData) && !materialData.MaterialName.empty())
						displayName = materialData.MaterialName;

					std::error_code relativeError;
					std::filesystem::path relativePath = std::filesystem::relative(entry.path(), importedAssetsDir, relativeError);
					const std::wstring relativePathText = relativeError
						? entry.path().filename().wstring()
						: relativePath.generic_wstring();

					materialFileEntries.push_back({ entry.path().wstring(), displayName, relativePathText });
				}
			}

			if (!materialFileEntries.empty())
			{
				std::sort(materialFileEntries.begin(), materialFileEntries.end(),
					[](const MaterialFileEntry& lhs, const MaterialFileEntry& rhs)
					{
						if (lhs.displayName == rhs.displayName)
							return lhs.path < rhs.path;
						return lhs.displayName < rhs.displayName;
					});

				size_t currentMaterialIndex = 0;
				bool foundCurrentMaterial = false;
				for (size_t i = 0; i < materialFileEntries.size(); ++i)
				{
					if (materialFileEntries[i].path == *materialFilePath)
					{
						currentMaterialIndex = i;
						foundCurrentMaterial = true;
						break;
					}
				}

				if (!foundCurrentMaterial)
					currentMaterialIndex = 0;

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
							*materialFilePath = materialFileEntries[i].path;

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
			else
			{
				ImGui::TextDisabled("材质：ImportedAssets 中未找到 .wmat 文件");
				materialFilePath->clear();
			}
		}

		if (lightType != nullptr)
		{
			constexpr UINT kMaxCreateLightType = 3;
			if (*lightType > kMaxCreateLightType)
				*lightType = DEFAULT_CREATE_LIGHT_TYPE;

			const std::string lightTypeText = SString::WstringToUTF8(L"灯光类型：");
			ImGui::Text("%s", lightTypeText.c_str());
			ImGui::SameLine();

			const std::string preview = SString::WstringToUTF8(GetCreateLightTypeLabel(*lightType));
			if (ImGui::BeginCombo("##CreateEntityLightType", preview.c_str()))
			{
				for (UINT option = 0; option <= kMaxCreateLightType; ++option)
				{
					const bool isSelected = (*lightType == option);
					const std::string label = SString::WstringToUTF8(GetCreateLightTypeLabel(option));
					if (ImGui::Selectable(label.c_str(), isSelected))
						*lightType = option;

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		if (ImGui::Button("确定"))
		{
			const std::wstring requestedName = *name;
			if (requestedName.empty())
			{
				MessageBox(nullptr, L"名称不能为空！", L"信息", MB_OK);
				ImGui::End();
				return false;
			}

			if (!m_ecs->IsEntityNameAvailable(requestedName, siblingScopeParent))
			{
				MessageBox(nullptr, L"名称不得与现有同级项目重名！", L"信息", MB_OK);
				ImGui::End();
				return false;
			}
			
			ImGui::End();
			*pOpen = false;
			return true;
		}
		else
			ImGui::SameLine();
		if (ImGui::Button("取消")) { ImGui::End(); *pOpen = false; return false; }
		else
			ImGui::End();
	}

	return false;
}

SceneEntityBase* HierarchyWindow::GetCurrentEntity()
{
	return m_ecs != nullptr ? m_ecs->GetSelectedEntity() : nullptr;
}

void HierarchyWindow::NeedRender(bool render)
{
	renderHierarchy = render;
}

void HierarchyWindow::RenderTree()
{
	if (m_ecs == nullptr)
		return;

	SceneEntityBase* selectedEntity = m_ecs->GetSelectedEntity();
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
			if (dragPayload->Preview)
				DrawHierarchyDropTargetHighlight(canDrop);

			if (canDrop)
			{
				if (const ImGuiPayload* acceptedPayload =
					ImGui::AcceptDragDropPayload(HIERARCHY_ENTITY_PAYLOAD, ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
				{
					SceneEntityBase* const* acceptedEntity = static_cast<SceneEntityBase* const*>(acceptedPayload->Data);
					if (acceptedPayload->Delivery && acceptedEntity != nullptr)
						QueueReparentRequest(*acceptedEntity, nullptr);
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
			m_ecs->ClearHierarchySelection();

	if (node_open)
	{
		const std::vector<SceneEntityBase*>& rootEntities = m_ecs->GetHierarchyRootEntities();
		for (UINT i = 0; i < m_ecs->GetHierarchyRootEntityCount(); ++i)
		{
			RenderNode(rootEntities[i], selectedEntity);
		}
		ImGui::TreePop();
	}
}

void HierarchyWindow::RenderNode(SceneEntityBase* ent, SceneEntityBase* selectedEntity)
{
	const bool hasChildren = m_ecs != nullptr && m_ecs->GetHierarchyChildCount(ent) > 0;
	ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_DefaultOpen;
	if (!hasChildren)
		tree_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	if (selectedEntity == ent)
		tree_flags |= ImGuiTreeNodeFlags_Selected;
	const std::wstring entityName = m_ecs->GetEntityName(ent);
	const std::string entityLabel = SString::WstringToUTF8(entityName);

	ImGui::PushID(ent);
	bool node_open = ImGui::TreeNodeEx("##HierarchyEntityNode", tree_flags, "%s", entityLabel.c_str());

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		m_ecs->SelectEntityForHierarchy(ent);

	if (ImGui::BeginDragDropSource())
	{
		SceneEntityBase* payloadEntity = ent;
		ImGui::SetDragDropPayload(HIERARCHY_ENTITY_PAYLOAD, &payloadEntity, sizeof(payloadEntity));
		ImGui::TextUnformatted(entityLabel.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginPopupContextItem("HierarchyEntityContextMenu"))
	{
		m_ecs->SelectEntityForHierarchy(ent);
		if (ImGui::MenuItem("删除"))
		{
			QueueDeleteRequest(ent);
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginDragDropTarget())
	{
		const ImGuiPayload* dragPayload = ImGui::GetDragDropPayload();
		if (dragPayload != nullptr &&
			dragPayload->IsDataType(HIERARCHY_ENTITY_PAYLOAD) &&
			dragPayload->DataSize == sizeof(SceneEntityBase*))
		{
			SceneEntityBase* const* draggedEntity = static_cast<SceneEntityBase* const*>(dragPayload->Data);
			SceneEntityBase* dragged = draggedEntity != nullptr ? *draggedEntity : nullptr;
			const bool canDrop = dragged != nullptr && m_ecs->CanReparentEntityInHierarchy(dragged, ent);
			if (dragPayload->Preview)
				DrawHierarchyDropTargetHighlight(canDrop);

			if (canDrop)
			{
				if (const ImGuiPayload* acceptedPayload =
					ImGui::AcceptDragDropPayload(HIERARCHY_ENTITY_PAYLOAD, ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
				{
					SceneEntityBase* const* acceptedEntity = static_cast<SceneEntityBase* const*>(acceptedPayload->Data);
					if (acceptedPayload->Delivery && acceptedEntity != nullptr)
						QueueReparentRequest(*acceptedEntity, ent);
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

	if (node_open && hasChildren)
	{
		const std::vector<SceneEntityBase*>& childrenEntity = m_ecs->GetHierarchyChildren(ent);
		for (UINT i = 0; i < childrenEntity.size(); i++)
		{
			RenderNode(childrenEntity[i], selectedEntity);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void HierarchyWindow::QueueImportRequest(const std::wstring& filePath, const std::wstring& fileName, SceneEntityBase* entityToSelect)
{
	if (m_ecs != nullptr && entityToSelect != nullptr)
		m_ecs->SelectEntityForHierarchy(entityToSelect);

	m_importFilePath = filePath;
	m_importEntityName = fileName;
	m_importTransform = Transform{};
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

	m_deleteCandidateEntity = entity;
	m_deleteCandidateEntityName = m_ecs->GetEntityName(entity);
	m_deleteCandidateHasChildren = m_ecs->GetHierarchyChildCount(entity) > 0;
	m_openDeleteConfirmPopup = true;
}

void HierarchyWindow::ProcessPendingReparentRequest()
{
	if (!m_hasPendingReparentRequest)
		return;
	if (m_ecs == nullptr)
	{
		m_hasPendingReparentRequest = false;
		m_pendingReparentEntity = nullptr;
		m_pendingReparentNewParent = nullptr;
		return;
	}

	m_hasPendingReparentRequest = false;
	SceneEntityBase* entity = m_pendingReparentEntity;
	SceneEntityBase* newParent = m_pendingReparentNewParent;
	m_pendingReparentEntity = nullptr;
	m_pendingReparentNewParent = nullptr;

	if (entity == nullptr || !m_ecs->HasEntity(entity))
		return;
	if (newParent != nullptr && !m_ecs->HasEntity(newParent))
		return;
	if (!m_ecs->CanReparentEntityInHierarchy(entity, newParent))
		return;

	if (m_ecs->ReparentEntityInHierarchy(entity, newParent) && m_dx != nullptr)
		m_dx->RebuildRenderItemsFromEntities(m_ecs);
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
	SceneEntityBase* deleteTargetEntity = m_pendingDeleteEntity;
	m_pendingDeleteEntity = nullptr;
	if (deleteTargetEntity == nullptr || !m_ecs->HasEntity(deleteTargetEntity))
		return;

	// “同时删除子实体”不会改变剩余实体的父子结构，
	// 因此这里可以直接局部移除对应子树的 RenderItem，
	// 避免整棵场景做一次全量重建。
	if (m_pendingDeleteChildren && deleteTargetEntity != nullptr && m_dx != nullptr)
		m_dx->RemoveRenderItemsFromEntity(deleteTargetEntity, m_ecs);

	m_ecs->DeleteEntityFromHierarchy(deleteTargetEntity, m_pendingDeleteChildren);

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
		m_pendingImportTransform))
	{
		MessageBox(nullptr, L"模型导入失败。", L"信息", MB_OK);
	}
}

