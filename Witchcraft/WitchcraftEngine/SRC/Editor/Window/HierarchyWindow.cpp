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

	ImGui::Begin("层次");
	if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		ImGui::OpenPopup("菜单"); 
	
	
	Transform transform;
	if(ImGui::BeginPopup("菜单"))
	{
		if (ImGui::MenuItem("空的"))
		{
			openCreateWindow = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("天空"))
		{
			openCreateWindow = true;
			name = L"天空";
		}
		if (ImGui::MenuItem("盒子"))
		{
			openCreateWindow = true;
			name = L"盒子";
		}
		if (ImGui::MenuItem("球体"))
		{
			openCreateWindow = true;
			name = L"球体";
		}
		if (ImGui::MenuItem("胶囊"))
		{
			openCreateWindow = true;
			name = L"胶囊";
		}
		if (ImGui::MenuItem("平面"))
		{
			openCreateWindow = true;
			name = L"平面";
		}
		ImGui::Separator();
		if (ImGui::MenuItem("相机"))
		{
			openCreateWindow = true;
			name = L"相机";
		}
		ImGui::EndPopup();
	}

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
				m_pendingDeleteEntityName = m_deleteCandidateEntityName;
				m_pendingDeleteChildren = true;
				m_hasPendingDeleteRequest = true;
				m_deleteCandidateEntity = nullptr;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("否"))
			{
				m_pendingDeleteEntityName = m_deleteCandidateEntityName;
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
				m_pendingDeleteEntityName = m_deleteCandidateEntityName;
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
	if (m_hasPendingDeleteRequest)
	{
		m_hasPendingDeleteRequest = false;
		m_ecs->DestroyEntity(m_pendingDeleteEntityName, m_pendingDeleteChildren);
		if (m_dx != nullptr)
					m_dx->RebuildRenderItemsFromEntities(m_ecs->GetRootEntities(), m_ecs);
	}

	if (!m_hasPendingImportRequest)
		return;

	m_hasPendingImportRequest = false;

	if (m_assimpLoader == nullptr || !m_assimpLoader->ImportModelToScene(
		m_pendingImportFilePath,
		m_ecs,
		m_pendingImportEntityName,
		m_pendingImportTransform))
	{
		MessageBox(nullptr, L"模型导入失败。", L"信息", MB_OK);
	}
}

void HierarchyWindow::RenderDeleteImpactTree(SceneEntityBase* ent)
{
	if (ent == nullptr)
		return;

	ImGui::BulletText("%s", SString::WstringToUTF8(ent->GetName()).c_str());

	std::vector<SceneEntityBase*> children = ent->GetChildrenEntity();
	if (children.empty())
		return;

	ImGui::Indent();
	for (SceneEntityBase* child : children)
	{
		RenderDeleteImpactTree(child);
	}
	ImGui::Unindent();
}

bool HierarchyWindow::CreateComponentWindow(bool* pOpen, std::wstring* name, Transform* transform, std::wstring* materialFilePath)
{
	if (ImGui::Begin("创建实体", pOpen, ImGuiWindowFlags_NoDocking))
	{
		ImGui::Text("名称：");
		ImGui::SameLine();
		std::string tmp = "";
		tmp = SString::WstringToUTF8(*name);
		if (ImGui::InputText("##NameComponent", &tmp, ImGuiInputTextFlags_EnterReturnsTrue))
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

		if (ImGui::Button("确定"))
		{
			// 判断是否存在同名称的对象
			if (m_ecs->GetEntity(SString::UTF8ToWstring(tmp)))
			{
				ImGui::End();
				*pOpen = false;
				MessageBox(nullptr, L"名称不得与现有同级项目重名！", L"信息", MB_OK);
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
	return m_ecs->GetSelectedEntity();
}

void HierarchyWindow::NeedRender(bool render)
{
	renderHierarchy = render;
}

void HierarchyWindow::RenderTree()
{
	///////////////////////////////////////////////////////////
	ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_DefaultOpen;

	bool node_open = ImGui::TreeNodeEx("世界根节点", tree_flags);

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_DEMO_ASS"))
		{
			const AssetDragPayload* file = static_cast<const AssetDragPayload*>(payload->Data);
			if (file != nullptr && !file->is_dir &&
				(file->file_type == FILEs::File_Type::OBJFILE ||
				 file->file_type == FILEs::File_Type::GLTFFILE ||
				 file->file_type == FILEs::File_Type::GLBFILE ||
				 file->file_type == FILEs::File_Type::WMODELFILE))
			{
				m_importFilePath = file->full_path;
				m_importEntityName = file->file_name_only;
				m_importTransform = Transform{};
				m_openImportWindow = true;
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsItemHovered())
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			m_ecs->SetSelectedEntity(nullptr);

	if (node_open)
	{
		for (UINT i = 0; i < m_ecs->Size(); i++)
		{
			RenderNode(m_ecs->GetEntity(i));
		}
		ImGui::TreePop();
	}
}

void HierarchyWindow::RenderNode(SceneEntityBase* ent)
{
	ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_DefaultOpen;

	if (m_ecs->GetSelectedEntity() == ent)
		tree_flags |= ImGuiTreeNodeFlags_Selected;
	bool node_open = ImGui::TreeNodeEx(SString::WstringToUTF8(ent->GetName()).c_str(), tree_flags);

	std::string popupId = "实体菜单##" + SString::WstringToUTF8(ent->GetName());
	if (ImGui::BeginPopupContextItem(popupId.c_str()))
	{
		m_ecs->SetSelectedEntity(ent);
		if (ImGui::MenuItem("删除"))
		{
			m_deleteCandidateEntity = ent;
			m_deleteCandidateEntityName = ent->GetName();
			m_deleteCandidateHasChildren = !ent->GetChildrenEntity().empty();
			m_openDeleteConfirmPopup = true;
		}
		ImGui::EndPopup();
	}

	if(node_open)
	{
		if (ImGui::IsItemClicked())
			m_ecs->SetSelectedEntity(ent);

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_DEMO_ASS"))
			{
				const AssetDragPayload* file = static_cast<const AssetDragPayload*>(payload->Data);
				if (file != nullptr && !file->is_dir &&
					(file->file_type == FILEs::File_Type::OBJFILE ||
					 file->file_type == FILEs::File_Type::GLTFFILE ||
					 file->file_type == FILEs::File_Type::GLBFILE ||
					 file->file_type == FILEs::File_Type::WMODELFILE))
				{
					m_ecs->SetSelectedEntity(ent);
					m_importFilePath = file->full_path;
					m_importEntityName = file->file_name_only;
					m_importTransform = Transform{};
					m_openImportWindow = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		auto childrenEntity = ent->GetChildrenEntity();
		for (UINT i = 0; i < childrenEntity.size(); i++)
		{
			RenderNode(childrenEntity[i]);
		}
		ImGui::TreePop();
	}
}
