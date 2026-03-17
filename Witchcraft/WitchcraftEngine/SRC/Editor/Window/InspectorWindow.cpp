#include "InspectorWindow.h"

#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/ScriptingComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"

#include "D3DWindow/D3DWindow.h"
#include "ECS/WitchcraECS.h"
#include "System/WitchcraftFile/WMaterialFile.h"

#include <algorithm>
#include <cwctype>
#include <cfloat>
#include <filesystem>

namespace
{
	bool IsSkyMesh(const MeshComponent* meshComponent)
	{
		return meshComponent != nullptr && meshComponent->GetRenderLayerIndex() == 天空渲染项目;
	}

	std::wstring GetInspectorEntityTypeLabel(
		WitchcraECS* ecs,
		SceneEntityBase* entity,
		const MeshComponent* meshComponent,
		const CameraComponent* cameraComponent,
		const TransformComponent* transformComponent)
	{
		if (IsSkyMesh(meshComponent))
			return L"天空";

		if (ecs != nullptr && entity != nullptr)
		{
			const std::wstring flecsTypeLabel = ecs->GetEntityTypeLabel(entity);
			if (!flecsTypeLabel.empty() && flecsTypeLabel != L"未知")
				return flecsTypeLabel;
		}

		if (meshComponent != nullptr)
			return L"网格";
		if (cameraComponent != nullptr)
			return L"相机";
		if (transformComponent != nullptr)
			return L"空实体";
		return L"未知";
	}
}

void InspectorWindow::Init(D3DWindow* dx, AssetsWindow* assetsWindow, PhysicsSystem* physicsSystem, WitchcraECS* ecs)
{
	m_dx = dx;
	m_assetsWindow = assetsWindow;
	m_physicsSystem = physicsSystem;
	m_ecs = ecs;
	_Static = false;
}

void InspectorWindow::RefreshMaterialFileCache()
{
	m_materialFileCache.clear();
	const std::filesystem::path importedAssetsDir =
		std::filesystem::path(EngineUtils::GetProjectDirPath()) / L"ImportedAssets";
	if (importedAssetsDir.empty() || !std::filesystem::exists(importedAssetsDir))
	{
		m_materialFileCacheDirty = false;
		return;
	}

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

		m_materialFileCache.push_back({ entry.path().wstring(), displayName, relativePathText });
	}

	std::sort(m_materialFileCache.begin(), m_materialFileCache.end(),
		[](const MaterialFileEntry& lhs, const MaterialFileEntry& rhs)
		{
			if (lhs.displayName == rhs.displayName)
				return lhs.path < rhs.path;
			return lhs.displayName < rhs.displayName;
		});

	m_materialFileCacheDirty = false;
}

void InspectorWindow::RefreshSkyTextureFileCache()
{
	m_skyTextureFileCache.clear();

	std::filesystem::path probe = std::filesystem::current_path();
	std::filesystem::path skyTextureDir;
	while (!probe.empty())
	{
		const std::filesystem::path candidate = probe / L"DATA" / L"HDRIs";
		if (std::filesystem::exists(candidate))
		{
			skyTextureDir = candidate;
			break;
		}

		const std::filesystem::path parent = probe.parent_path();
		if (parent == probe)
			break;
		probe = parent;
	}

	if (skyTextureDir.empty())
	{
		m_skyTextureFileCacheDirty = false;
		return;
	}

	const std::filesystem::path projectRoot = skyTextureDir.parent_path().parent_path();
	for (const auto& entry : std::filesystem::directory_iterator(skyTextureDir))
	{
		if (!entry.is_regular_file())
			continue;

		std::wstring extension = entry.path().extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
		if (extension != L".png")
			continue;

		m_skyTextureFileCache.push_back(std::filesystem::relative(entry.path(), projectRoot).generic_wstring());
	}

	std::sort(m_skyTextureFileCache.begin(), m_skyTextureFileCache.end());
	m_skyTextureFileCacheDirty = false;
}

void InspectorWindow::Render()
{
	if (!renderInspector)
		return;

	if (m_materialFileCacheDirty)
		RefreshMaterialFileCache();
	if (m_skyTextureFileCacheDirty)
		RefreshSkyTextureFileCache();

	ImGui::Begin("实体信息");
	{
		SceneEntityBase* selectedEntity = m_ecs != nullptr ? m_ecs->GetSelectedEntity() : nullptr;
		if (selectedEntity == nullptr)
		{
			m_ComponentServices = nullptr;
			ImGui::TextDisabled("未选择实体。");
		}
		else
		{
			m_ComponentServices = selectedEntity->GetChildrenContainer();
			ImGui::TextWrapped("当前实体：%s", SString::WstringToUTF8(selectedEntity->GetName()).c_str());
			ImGui::Separator();

			if (m_ComponentServices == nullptr || m_ComponentServices->Size() == 0)
				ImGui::TextDisabled("该实体暂无可显示组件。");
			else
				RenderComponent();
		}
	}
	ImGui::End();
}

void InspectorWindow::NeedRender(bool render)
{
	renderInspector = render;
}

void InspectorWindow::RenderAdd()
{
	if (m_ComponentServices == nullptr)
		return;

	ImGui::Separator();

	if (ImGui::Button("添加"))
		ImGui::OpenPopup("addComp");

	if (ImGui::BeginPopup("addComp", ImGuiWindowFlags_NoMove))
	{
		if (ImGui::BeginMenu("碰撞体"))
		{
			if (ImGui::MenuItem("盒子"))
			{
				auto tmp = m_ComponentServices->FindServiceAs<PhysicsComponent>(L"PhysicsComponent");
				if (tmp != nullptr)
					tmp->AddBoxCollider(m_ComponentServices);
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();

		static std::vector<std::pair<std::wstring, std::wstring>> data;

		if (ImGui::BeginMenu("脚本"))
		{
			if (ImGui::IsWindowAppearing() && m_assetsWindow != nullptr)
				m_assetsWindow->GetFileNameFromProjectDir(EngineUtils::GetProjectDirPath(), FILEs::File_Type::LUAFILE, data);
			for (size_t i = 0; i < data.size(); i++)
				if (ImGui::MenuItem(SString::WstringToUTF8(data[i].second).c_str()))
				{
					auto tmp = m_ComponentServices->FindServiceAs<ScriptingComponent>(L"ScriptingComponent");
					if (tmp != nullptr)
						tmp->AddScript(data[i].first.c_str());
				}
			ImGui::EndMenu();
		}
		else
		{
			data.clear();
		}

		ImGui::EndPopup();
	}
}

void InspectorWindow::UpdateComponent()
{
	if (m_ComponentServices == nullptr)
		return;
}

void InspectorWindow::RenderComponent()
{
	if (m_ComponentServices == nullptr)
		return;

	// Render() 已保证当前确实有选中实体；
	// 这里再次兜底，避免后续独立调用 RenderComponent() 时误用空指针。
	SceneEntityBase* selectedEntity = m_ecs != nullptr ? m_ecs->GetSelectedEntity() : nullptr;
	if (selectedEntity == nullptr)
		return;

	auto generalComponent = m_ComponentServices->FindServiceAs<GeneralComponent>(L"GeneralComponent");
	auto cameraComponent = m_ComponentServices->FindServiceAs<CameraComponent>(L"CameraComponent");
	auto transformComponent = m_ComponentServices->FindServiceAs<TransformComponent>(L"TransformComponent");
	auto meshComponent = m_ComponentServices->FindServiceAs<MeshComponent>(L"MeshComponent");
	Transform editableLocalTransform{};
	const bool hasEditableLocalTransform =
		(m_ecs != nullptr && m_ecs->GetEntityEditableLocalTransform(selectedEntity, &editableLocalTransform));
	if (!hasEditableLocalTransform)
		editableLocalTransform = Transform{};
	const bool isSkyEntity = IsSkyMesh(meshComponent);
	const std::wstring entityTypeLabel = GetInspectorEntityTypeLabel(
		m_ecs,
		selectedEntity,
		meshComponent,
		cameraComponent,
		transformComponent);

	// GeneralComponent
	if (generalComponent != nullptr && ImGui::CollapsingHeader("一般", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("复制")) {}
			if (ImGui::MenuItem("粘贴")) {}
			ImGui::Separator();
			ImGui::MenuItem("移除", "", false, false);
			ImGui::EndPopup();
		}

		if (ImGui::BeginTable("GeneralComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			{
				ImGui::Text("名称");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text("类型");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text("可见");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text("静态");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text("标签");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
			}
			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
			{
				std::string tmp = "";

				std::wstring _Name = generalComponent->GetName();
				tmp = SString::WstringToUTF8(_Name);
				if (ImGui::InputText("##NameGeneralComponent", &tmp, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					const std::wstring newName = SString::UTF8ToWstring(tmp);
					selectedEntity->SetName(newName);
				}

				ImGui::TextUnformatted(SString::WstringToUTF8(entityTypeLabel).c_str());

				bool visible = generalComponent->IsVisible();
				if (ImGui::Checkbox("##VisibleGeneralComponent", &visible))
				{
					generalComponent->SetVisible(visible);
					if (m_dx != nullptr && m_ecs != nullptr)
						m_dx->RebuildRenderItemsFromEntities(m_ecs->GetRootEntities(), m_ecs);
				}

				_Static = generalComponent->IsStatic();
				if (ImGui::Checkbox("##StaticGeneralComponent", &_Static))
					selectedEntity->SetStatic(_Static);

				std::wstring _Tag = generalComponent->GetTag();
				tmp = SString::WstringToUTF8(_Tag);
				if (ImGui::InputText("##TagGeneralComponent", &tmp, ImGuiInputTextFlags_EnterReturnsTrue))
					selectedEntity->SetTag(SString::UTF8ToWstring(tmp));
			}
			ImGui::PopItemWidth();
			ImGui::EndTable();
		}
	}
	
	// TransformComponent
	if (transformComponent != nullptr && ImGui::CollapsingHeader("变换", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("复制")) {}
			if (ImGui::MenuItem("粘贴")) {}
			ImGui::Separator();
			ImGui::MenuItem("移除", "", false, false);
			ImGui::EndPopup();
		}

		/////////////////////////////////////////////////////////////

		if (ImGui::BeginTable("TransformComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			{
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
				ImGui::Text("位置");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text("旋转");
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2 + 5);
				ImGui::Text("缩放");
			}
			ImGui::TableNextColumn();

			const bool IsMesh = (meshComponent != nullptr);
			auto applyEditableLocalTransform = [&](const Transform& updatedTransform)
			{
				if (m_ecs != nullptr && m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
				{
					editableLocalTransform = updatedTransform;
					return;
				}
			};
			auto refreshRenderItemTransform = [&]()
			{
				if (m_dx != nullptr && m_ecs != nullptr)
				{
					m_dx->RebuildRenderItemsFromEntities(m_ecs->GetRootEntities(), m_ecs);
					return;
				}

				if (!IsMesh)
					return;

				meshComponent->UpdateMesh(
					m_ComponentServices,
					m_dx,
					editableLocalTransform,
					DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
			};
			// 位置
			{
				DirectX::XMFLOAT3 Position = editableLocalTransform.position;
				if (isSkyEntity)
					ImGui::BeginDisabled();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text("X");
				ImGui::PopStyleColor(1); 
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3 / (2 * 0.875));
				float X = Position.x;
				if (ImGui::DragFloat("##PotXTransformComp", &X, 0.5f, MININT, MAXINT))
				{
					Transform updatedTransform = editableLocalTransform;
					updatedTransform.position = DirectX::XMFLOAT3(X, Position.y, Position.z);
					applyEditableLocalTransform(updatedTransform);
					refreshRenderItemTransform();
				}

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text("Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2 / (2 * 0.875));
				float Y = Position.y;
				if (ImGui::DragFloat("##PotYTransformComp", &Y, 0.5f, MININT, MAXINT))
				{
					Transform updatedTransform = editableLocalTransform;
					updatedTransform.position = DirectX::XMFLOAT3(Position.x, Y, Position.z);
					applyEditableLocalTransform(updatedTransform);
					refreshRenderItemTransform();
				}
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text("Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1 / (2 * 0.875));
				float Z = Position.z;
				if (ImGui::DragFloat("##PotZTransformComp", &Z, 0.5f, MININT, MAXINT))
				{
					Transform updatedTransform = editableLocalTransform;
					updatedTransform.position = DirectX::XMFLOAT3(Position.x, Position.y, Z);
					applyEditableLocalTransform(updatedTransform);
					refreshRenderItemTransform();
				}
				if (isSkyEntity)
				{
					ImGui::EndDisabled();
					ImGui::TextDisabled("天空始终跟随相机，位置不可编辑");
				}
			}
			// 旋转
			{
				DirectX::XMFLOAT3 Rotation = editableLocalTransform.rotation;
				if (isSkyEntity)
					ImGui::TextDisabled("贴图旋转：");
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text("X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3 / (2 * 0.875));
				float X = Rotation.x;
				if (ImGui::DragFloat("##RotXTransformComp", &X, 0.5f, -180.0f, +180.0f))
				{
					Transform updatedTransform = editableLocalTransform;
					updatedTransform.rotation = DirectX::XMFLOAT3(X, Rotation.y, Rotation.z);
					applyEditableLocalTransform(updatedTransform);
					refreshRenderItemTransform();
				}

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text("Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2 / (2 * 0.875));
				float Y = Rotation.y;
				if (ImGui::DragFloat("##RotYTransformComp", &Y, 0.5f, -180.0f, +180.0f))
				{
					Transform updatedTransform = editableLocalTransform;
					updatedTransform.rotation = DirectX::XMFLOAT3(Rotation.x, Y, Rotation.z);
					applyEditableLocalTransform(updatedTransform);
					refreshRenderItemTransform();
				}

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text("Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1 / (2 * 0.875));
				float Z = Rotation.z;
				if (ImGui::DragFloat("##RotZTransformComp", &Z, 0.5f, -180.0f, +180.0f))
				{
					Transform updatedTransform = editableLocalTransform;
					updatedTransform.rotation = DirectX::XMFLOAT3(Rotation.x, Rotation.y, Z);
					applyEditableLocalTransform(updatedTransform);
					refreshRenderItemTransform();
				}
			}
			// 缩放
			{
				DirectX::XMFLOAT3 Scale = editableLocalTransform.scale;
				if (isSkyEntity)
					ImGui::TextDisabled("天空大小：");
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
				ImGui::Text("X");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3 / (2 * 0.875));
				float X = Scale.x;
				if (ImGui::DragFloat("##ScaXTransformComp", &X, 0.5f, 0.0f, MAXINT))
				{
					Transform updatedTransform = editableLocalTransform;
					updatedTransform.scale = DirectX::XMFLOAT3(X, Scale.y, Scale.z);
					applyEditableLocalTransform(updatedTransform);
					refreshRenderItemTransform();
				}
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
				ImGui::Text("Y");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2 / (2 * 0.875));
				float Y = Scale.y;
				if (ImGui::DragFloat("##ScaYTransformComp", &Y, 0.5f, 0.0f, MAXINT))
				{
					Transform updatedTransform = editableLocalTransform;
					updatedTransform.scale = DirectX::XMFLOAT3(Scale.x, Y, Scale.z);
					applyEditableLocalTransform(updatedTransform);
					refreshRenderItemTransform();
				}

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
				ImGui::Text("Z");
				ImGui::PopStyleColor(1);
				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 1 / (2 * 0.875));
				float Z = Scale.z;
				if (ImGui::DragFloat("##ScaZTransformComp", &Z, 0.5f, 0.0f, MAXINT))
				{
					Transform updatedTransform = editableLocalTransform;
					updatedTransform.scale = DirectX::XMFLOAT3(Scale.x, Scale.y, Z);
					applyEditableLocalTransform(updatedTransform);
					refreshRenderItemTransform();
				}
			}
			ImGui::PopItemWidth();
			ImGui::EndTable();
		}
	}

	if (meshComponent != nullptr && ImGui::CollapsingHeader("模型", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginTable("MeshComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("顶点");
			ImGui::Text("表面");
			ImGui::Text(isSkyEntity ? "贴图" : "材质");

			ImGui::TableNextColumn();
			ImGui::Text("%u", meshComponent->GetNumVertices());
			ImGui::Text("%u", meshComponent->GetNumFaces());
			const std::wstring currentMaterialName = meshComponent->GetMaterialName();
			if (isSkyEntity)
			{
				const std::wstring currentSkyTexturePath =
					m_dx != nullptr ? m_dx->GetSkyTexturePathByRuntimeMaterialName(currentMaterialName) : L"";
				if (m_skyTextureFileCache.empty())
				{
					ImGui::TextDisabled("未找到 DATA/HDRIs 下的天空贴图 png 文件");
				}
				else
				{
					size_t currentSkyIndex = 0;
					for (size_t i = 0; i < m_skyTextureFileCache.size(); ++i)
					{
						if (!currentSkyTexturePath.empty() && m_skyTextureFileCache[i] == currentSkyTexturePath)
						{
							currentSkyIndex = i;
							break;
						}
					}

					std::wstring previewText = std::filesystem::path(m_skyTextureFileCache[currentSkyIndex]).filename().wstring();
					if (!currentSkyTexturePath.empty())
						previewText = std::filesystem::path(currentSkyTexturePath).filename().wstring();

					if (ImGui::BeginCombo("##SkyTextureSelector", SString::WstringToUTF8(previewText).c_str()))
					{
						for (size_t i = 0; i < m_skyTextureFileCache.size(); ++i)
						{
							const bool isSelected = (!currentSkyTexturePath.empty() && m_skyTextureFileCache[i] == currentSkyTexturePath)
								|| (currentSkyTexturePath.empty() && i == currentSkyIndex);
							const std::string label = SString::WstringToUTF8(std::filesystem::path(m_skyTextureFileCache[i]).filename().wstring());
							if (ImGui::Selectable(label.c_str(), isSelected))
							{
								const std::wstring runtimeSkyMaterial = m_dx->GetOrCreateSkyMaterial(m_skyTextureFileCache[i]);
								if (!runtimeSkyMaterial.empty())
									meshComponent->SetMaterial(runtimeSkyMaterial);
							}

							if (isSelected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}

					const std::wstring updatedSkyTexturePath =
						m_dx != nullptr ? m_dx->GetSkyTexturePathByRuntimeMaterialName(meshComponent->GetMaterialName()) : L"";
					if (!updatedSkyTexturePath.empty())
					{
						ImGui::TextDisabled("路径：%s",
							SString::WstringToUTF8(updatedSkyTexturePath).c_str());
					}
				}

				if (ImGui::Button("刷新天空贴图列表"))
				{
					m_skyTextureFileCacheDirty = true;
					RefreshSkyTextureFileCache();
				}
			}
			else if (m_materialFileCache.empty())
			{
				ImGui::TextDisabled("ImportedAssets 中未找到 .wmat");
				if (ImGui::Button("刷新材质列表"))
				{
					m_materialFileCacheDirty = true;
					RefreshMaterialFileCache();
				}
			}
			else
			{
				size_t currentMaterialIndex = 0;
				const std::wstring currentMaterialFilePath =
					m_dx != nullptr ? m_dx->GetMaterialFilePathByRuntimeMaterialName(currentMaterialName) : L"";
				for (size_t i = 0; i < m_materialFileCache.size(); ++i)
				{
					if (!currentMaterialFilePath.empty() && m_materialFileCache[i].path == currentMaterialFilePath)
					{
						currentMaterialIndex = i;
						break;
					}
				}

				std::wstring previewText = m_materialFileCache[currentMaterialIndex].displayName;
				if (currentMaterialFilePath.empty())
					previewText = currentMaterialName.empty() ? L"<未绑定材质>" : currentMaterialName;

				const std::string preview = SString::WstringToUTF8(previewText);
				if (ImGui::BeginCombo("##MeshMaterialSelector", preview.c_str()))
				{
					for (size_t i = 0; i < m_materialFileCache.size(); ++i)
					{
						const bool isSelected = (!currentMaterialFilePath.empty() && m_materialFileCache[i].path == currentMaterialFilePath);
						const std::string materialUtf8 = SString::WstringToUTF8(m_materialFileCache[i].displayName);
						if (ImGui::Selectable(materialUtf8.c_str(), isSelected))
						{
							const std::wstring runtimeMaterialName =
								m_dx->GetOrCreateMaterialFromWMaterialFile(m_materialFileCache[i].path);
							if (!runtimeMaterialName.empty())
								meshComponent->SetMaterial(runtimeMaterialName);
						}

						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				const std::wstring updatedMaterialFilePath =
					m_dx != nullptr ? m_dx->GetMaterialFilePathByRuntimeMaterialName(meshComponent->GetMaterialName()) : L"";
				for (size_t i = 0; i < m_materialFileCache.size(); ++i)
				{
					if (!updatedMaterialFilePath.empty() && m_materialFileCache[i].path == updatedMaterialFilePath)
					{
						currentMaterialIndex = i;
						break;
					}
				}

				if (!updatedMaterialFilePath.empty())
				{
					ImGui::TextDisabled("路径：ImportedAssets/%s",
						SString::WstringToUTF8(m_materialFileCache[currentMaterialIndex].relativePath).c_str());
				}
				else
				{
					ImGui::TextDisabled("路径：当前材质未对应到 .wmat");
				}

				if (ImGui::Button("刷新材质列表"))
				{
					m_materialFileCacheDirty = true;
					RefreshMaterialFileCache();
				}
			}
			ImGui::EndTable();
		}
	}

	if (cameraComponent != nullptr && ImGui::CollapsingHeader("相机", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginTable("CameraComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("视角场");
			ImGui::Text("Near");
			ImGui::Text("Far");
			ImGui::Text("比例");
			ImGui::Text("");

			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
			float fov = cameraComponent->GetFov() * 256.0f;
			if (ImGui::SliderFloat("##FovCamComp", &fov, MIN_FOV, MAX_FOV))
				cameraComponent->SetFov(fov / 256.0f);

			float nearZ = cameraComponent->GetNear();
			if (ImGui::DragFloat("##NearCamComp", &nearZ, 0.01f, 0.001f, cameraComponent->GetFar()))
				cameraComponent->SetNear(nearZ);

			float farZ = cameraComponent->GetFar();
			if (ImGui::DragFloat("##FarCamComp", &farZ, 0.01f, cameraComponent->GetNear(), FLT_MAX))
				cameraComponent->SetFar(farZ);

			float scale = cameraComponent->GetScale();
			if (ImGui::DragFloat("##ScaleCamComp", &scale, 0.01f, 0.1f, FLT_MAX))
				cameraComponent->SetScale(scale);

			if (ImGui::Button("还原比例"))
				cameraComponent->RestoreScale();

			ImGui::PopItemWidth();
			ImGui::EndTable();
		}
	}
}
