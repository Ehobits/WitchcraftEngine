#include "InspectorWindow.h"

#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/LightComponent.h"
#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/ScriptingComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
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
	constexpr const char* kTemporarilyDisabledReason = "暂时禁用：桥（功能）尚未完成。";

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

	constexpr float kDirectionalShaderLightType = 0.0f;
	constexpr float kPointShaderLightType = 1.0f;
	constexpr float kSpotShaderLightType = 2.0f;

	const char* GetInspectorLightKindLabel(LightKind kind)
	{
		switch (kind)
		{
		case LightKind::Ambient:
			return "环境光";
		case LightKind::Directional:
			return "平行光";
		case LightKind::Spot:
			return "聚光灯";
		case LightKind::Point:
			return "点光源";
		default:
			return "未知";
		}
	}

	float ResolveInspectorLightShaderType(LightKind kind, float fallbackType)
	{
		switch (kind)
		{
		case LightKind::Directional:
			return kDirectionalShaderLightType;
		case LightKind::Point:
			return kPointShaderLightType;
		case LightKind::Spot:
			return kSpotShaderLightType;
		case LightKind::Ambient:
		default:
			return fallbackType;
		}
	}

	bool SaveRuntimeMaterialToMaterialFile(const std::filesystem::path& materialFilePath, Material& material)
	{
		if (materialFilePath.empty())
			return false;

		WMaterialFileData materialData;
		if (!WMaterialFile::LoadFromFile(materialFilePath, &materialData))
			materialData.MaterialName = material.GetName();

		materialData.DiffuseColor = material.Properties.DiffuseAlbedo;
		materialData.Emissive = material.Properties.Emissive;
		materialData.UseNormalTexture = material.Properties.UseNormalTexture != 0;
		materialData.UseMetallicTexture = material.Properties.UseMetallicTexture != 0;
		materialData.UseRoughnessTexture = material.Properties.UseRoughnessTexture != 0;
		materialData.Metallic = material.Properties.Metallic;
		materialData.Roughness = material.Properties.Roughness;
		materialData.Opacity = material.Properties.DiffuseAlbedo.w;

		return WMaterialFile::SaveToFile(materialFilePath, materialData);
	}

	template<typename TComponent>
	bool BeginInspectorComponentHeader(const char* label, const TComponent* component)
	{
		return component != nullptr && ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
	}

	bool RenderReadonlyComponentPopup()
	{
		if (!ImGui::BeginPopupContextItem())
			return false;

		if (ImGui::MenuItem("复制")) {}
		if (ImGui::MenuItem("粘贴")) {}
		ImGui::Separator();
		ImGui::MenuItem("移除", "", false, false);
		ImGui::EndPopup();
		return false;
	}

	template<typename OnRebuild, typename OnRemove>
	bool RenderReplaceableComponentPopup(OnRebuild&& onRebuild, OnRemove&& onRemove)
	{
		if (!ImGui::BeginPopupContextItem())
			return false;

		if (ImGui::MenuItem("重建"))
		{
			onRebuild();
			ImGui::EndPopup();
			return true;
		}

		if (ImGui::MenuItem("移除"))
		{
			onRemove();
			ImGui::EndPopup();
			return true;
		}

		ImGui::EndPopup();
		return false;
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

		if (!probe.has_parent_path())
			break;
		probe = probe.parent_path();
	}

	if (skyTextureDir.empty() || !std::filesystem::exists(skyTextureDir))
	{
		m_skyTextureFileCacheDirty = false;
		return;
	}

	for (const auto& entry : std::filesystem::directory_iterator(skyTextureDir))
	{
		if (!entry.is_regular_file())
			continue;

		std::wstring extension = entry.path().extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
		if (extension == L".png")
			m_skyTextureFileCache.push_back(entry.path().wstring());
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
		EntityComponentView selectedView;
		if (m_ecs == nullptr || !m_ecs->BuildSelectedEntityComponentView(&selectedView) || selectedView.entity == nullptr)
		{
			ImGui::TextDisabled("未选中实体。");
		}
		else
		{
			ImGui::TextWrapped("当前实体：%s", SString::WstringToUTF8(m_ecs->GetEntityName(selectedView.entity)).c_str());
			ImGui::Separator();

			if (!selectedView.hasInspectableComponents)
				ImGui::TextDisabled("没有选择任何实体。");
			else
				RenderComponent(selectedView);
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
	ImGui::Separator();

	if (ImGui::Button("添加组件"))
		ImGui::OpenPopup("addComp");

	if (ImGui::BeginPopup("addComp", ImGuiWindowFlags_NoMove))
	{
		EntityComponentView selectedView;
		if (m_ecs == nullptr || !m_ecs->BuildSelectedEntityComponentView(&selectedView) || selectedView.entity == nullptr)
		{
			ImGui::EndPopup();
			return;
		}

		ImGui::MenuItem("刚体", "", false, false);
		ImGui::TextDisabled("%s", kTemporarilyDisabledReason);

		ImGui::Separator();

		if (ImGui::BeginMenu("物理"))
		{
			ImGui::MenuItem("盒体碰撞器", "", false, false);
			ImGui::TextDisabled("%s", kTemporarilyDisabledReason);
			ImGui::EndMenu();
		}

		static std::vector<std::pair<std::wstring, std::wstring>> data;
		if (ImGui::BeginMenu("脚本"))
		{
			data.clear();
			ImGui::TextDisabled("%s", kTemporarilyDisabledReason);
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
}

void InspectorWindow::RenderComponent(const EntityComponentView& context)
{
	if (m_ecs == nullptr || context.entity == nullptr)
		return;

	SceneEntityBase* selectedEntity = context.entity;
	auto* generalComponent = context.generalComponent;
	auto* cameraComponent = context.cameraComponent;
	auto* transformComponent = context.transformComponent;
	auto* meshComponent = context.meshComponent;
	auto* lightComponent = context.lightComponent;
	auto* physicsComponent = context.physicsComponent;
	auto* scriptingComponent = context.scriptingComponent;
	auto* rigidbodyComponent = context.rigidbodyComponent;
	Transform editableLocalTransform = context.editableLocalTransform;
	const bool isSkyEntity = context.isSkyEntity;
	const std::wstring& entityTypeLabel = context.entityTypeLabel;

	auto refreshRenderItemTransform = [&]()
	{
		if (m_dx != nullptr)
			m_dx->UpdateRenderItemsTransformFromEntity(selectedEntity, m_ecs);
	};

	// 常规组件
	if (BeginInspectorComponentHeader("常规", generalComponent))
	{
		RenderReadonlyComponentPopup();

		if (ImGui::BeginTable("GeneralComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("名称");
			ImGui::Text("类型");
			ImGui::Text("可见");
			ImGui::Text("静态");
			ImGui::Text("标签");

			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

			std::string entityName = SString::WstringToUTF8(m_ecs->GetEntityName(selectedEntity));
			if (ImGui::InputText("##NameGeneralComponent", &entityName, ImGuiInputTextFlags_EnterReturnsTrue))
				m_ecs->RenameSelectedEntity(SString::UTF8ToWstring(entityName));

			ImGui::TextUnformatted(SString::WstringToUTF8(entityTypeLabel).c_str());

			bool visible = m_ecs->IsEntityVisible(selectedEntity);
			if (ImGui::Checkbox("##VisibleGeneralComponent", &visible))
			{
				m_ecs->SetSelectedEntityVisible(visible);
				if (m_dx != nullptr)
				{
					if (visible)
						m_dx->AddRenderItemsFromEntity(selectedEntity, m_ecs);
					else
						m_dx->RemoveRenderItemsFromEntity(selectedEntity, m_ecs);
				}
			}

			_Static = m_ecs->IsEntityStatic(selectedEntity);
			if (ImGui::Checkbox("##StaticGeneralComponent", &_Static))
				m_ecs->SetSelectedEntityStatic(_Static);

			std::string entityTag = SString::WstringToUTF8(m_ecs->GetEntityTag(selectedEntity));
			if (ImGui::InputText("##TagGeneralComponent", &entityTag, ImGuiInputTextFlags_EnterReturnsTrue))
				m_ecs->SetSelectedEntityTag(SString::UTF8ToWstring(entityTag));

			ImGui::PopItemWidth();
			ImGui::EndTable();
		}
	}

	// 变换组件
	if (BeginInspectorComponentHeader("变换", transformComponent))
	{
		RenderReadonlyComponentPopup();

		if (ImGui::BeginTable("TransformComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("位置");
			ImGui::Text("旋转");
			ImGui::Text("缩放");

			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

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
					if (m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
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
					if (m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
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
					if (m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
						refreshRenderItemTransform();
				}
				if (isSkyEntity)
					ImGui::EndDisabled();
			}
			// 旋转
			{
				DirectX::XMFLOAT3 Rotation = editableLocalTransform.rotation;
				if (isSkyEntity)
					ImGui::TextDisabled("天空实体不使用旋转");
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
					if (m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
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
					if (m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
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
					if (m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
						refreshRenderItemTransform();
				}
			}
			// 缩放
			{
				DirectX::XMFLOAT3 Scale = editableLocalTransform.scale;
				if (isSkyEntity)
					ImGui::TextDisabled("天空实体不使用缩放");
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
					if (m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
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
					if (m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
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
					if (m_ecs->SetEntityEditableLocalTransform(selectedEntity, updatedTransform))
						refreshRenderItemTransform();
				}
			}
			// 天空实体在运行时仍会跟随相机。

			ImGui::PopItemWidth();
			ImGui::EndTable();
		}
	}

	// 网格组件
	if (BeginInspectorComponentHeader("网格", meshComponent))
	{
		if (RenderReplaceableComponentPopup(
			[&]() { return m_ecs->RebuildMeshComponentOnEntity(selectedEntity); },
			[&]() { return m_ecs->RemoveMeshComponentFromEntity(selectedEntity); }))
			return;

		if (ImGui::BeginTable("MeshComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("顶点数");
			ImGui::Text("面数");
			ImGui::Text(isSkyEntity ? "天空纹理" : "材质");

			ImGui::TableNextColumn();
			ImGui::Text("%u", meshComponent->GetNumVertices());
			ImGui::Text("%u", meshComponent->GetNumFaces());

			const std::wstring currentMaterialName = meshComponent->GetMaterialName();
			if (isSkyEntity)
			{
				const std::wstring currentSkyTexturePath = m_dx != nullptr ? m_dx->GetSkyTexturePathByRuntimeMaterialName(currentMaterialName) : L"";
				if (m_skyTextureFileCache.empty())
				{
					ImGui::TextDisabled("在 DATA/HDRIs 下未找到 PNG 文件。");
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

					const std::string preview = SString::WstringToUTF8(previewText);
					if (ImGui::BeginCombo("##SkyTextureSelector", preview.c_str()))
					{
						for (size_t i = 0; i < m_skyTextureFileCache.size(); ++i)
						{
							const std::wstring filename = std::filesystem::path(m_skyTextureFileCache[i]).filename().wstring();
							const bool isSelected = (i == currentSkyIndex);
							if (ImGui::Selectable(SString::WstringToUTF8(filename).c_str(), isSelected) && m_dx != nullptr)
							{
								const std::wstring runtimeMaterialName = m_dx->GetOrCreateSkyMaterial(m_skyTextureFileCache[i]);
								meshComponent->SetMaterial(runtimeMaterialName);
							}

							if (isSelected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				if (ImGui::Button("刷新天空纹理列表"))
				{
					m_skyTextureFileCacheDirty = true;
					RefreshSkyTextureFileCache();
				}
			}
			else if (m_materialFileCache.empty())
			{
				ImGui::TextDisabled("在 ImportedAssets 下未找到 .wmat 文件。");
				if (ImGui::Button("刷新材质列表"))
				{
					m_materialFileCacheDirty = true;
					RefreshMaterialFileCache();
				}
			}
			else
			{
				size_t currentMaterialIndex = 0;
				const std::wstring currentMaterialFilePath = m_dx != nullptr ? m_dx->GetMaterialFilePathByRuntimeMaterialName(currentMaterialName) : L"";
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
					previewText = currentMaterialName.empty() ? L"<未绑定>" : currentMaterialName;

				const std::string preview = SString::WstringToUTF8(previewText);
				if (ImGui::BeginCombo("##MeshMaterialSelector", preview.c_str()))
				{
					for (size_t i = 0; i < m_materialFileCache.size(); ++i)
					{
						const bool isSelected = (i == currentMaterialIndex);
						if (ImGui::Selectable(SString::WstringToUTF8(m_materialFileCache[i].displayName).c_str(), isSelected) && m_dx != nullptr)
						{
							const std::wstring runtimeMaterialName = m_dx->GetOrCreateMaterialFromWMaterialFile(m_materialFileCache[i].path);
							meshComponent->SetMaterial(runtimeMaterialName);
							currentMaterialIndex = i;
						}

						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				if (m_dx != nullptr)
				{
					const std::wstring updatedMaterialFilePath = m_dx->GetMaterialFilePathByRuntimeMaterialName(meshComponent->GetMaterialName());
					if (!updatedMaterialFilePath.empty())
						ImGui::TextDisabled("路径：ImportedAssets/%s", SString::WstringToUTF8(m_materialFileCache[currentMaterialIndex].relativePath).c_str());
					else
						ImGui::TextDisabled("路径：当前材质未绑定到 .wmat 文件。");
				}

				if (ImGui::Button("刷新材质列表"))
				{
					m_materialFileCacheDirty = true;
					RefreshMaterialFileCache();
				}
			}

			ImGui::EndTable();
		}

		if (!isSkyEntity && m_dx != nullptr)
		{
			const std::wstring boundRuntimeMaterialName = meshComponent->GetMaterialName();
			const std::wstring boundMaterialFilePath = m_dx->GetMaterialFilePathByRuntimeMaterialName(boundRuntimeMaterialName);
			Material* runtimeMaterial = m_dx->GetMaterialByRuntimeMaterialName(boundRuntimeMaterialName);
			if (runtimeMaterial != nullptr)
			{
				ImGui::Separator();
				ImGui::TextDisabled("运行时材质：%s", SString::WstringToUTF8(runtimeMaterial->GetName()).c_str());
				if (!boundMaterialFilePath.empty())
					ImGui::Checkbox("同步修改到 .wmat", &m_syncMaterialChangesToFile);

				MaterialConstants editedProperties = runtimeMaterial->Properties;
				bool materialChanged = false;

				materialChanged |= ImGui::ColorEdit4("漫反射反照率", &editedProperties.DiffuseAlbedo.x);
				materialChanged |= ImGui::ColorEdit3("菲涅尔 R0", &editedProperties.FresnelR0.x);
				materialChanged |= ImGui::ColorEdit3("自发光", &editedProperties.Emissive.x);
				materialChanged |= ImGui::DragFloat("金属度", &editedProperties.Metallic, 0.005f, 0.0f, 1.0f, "%.3f");
				materialChanged |= ImGui::DragFloat("粗糙度", &editedProperties.Roughness, 0.005f, 0.0f, 1.0f, "%.3f");
				materialChanged |= ImGui::DragFloat("清漆层厚度", &editedProperties.ClearCoatThickness, 0.005f, 0.0f, 1.0f, "%.3f");
				materialChanged |= ImGui::DragFloat("清漆层粗糙度", &editedProperties.ClearCoatRoughness, 0.005f, 0.0f, 1.0f, "%.3f");

				bool useNormalTexture = editedProperties.UseNormalTexture != 0;
				if (ImGui::Checkbox("使用法线纹理", &useNormalTexture))
				{
					editedProperties.UseNormalTexture = useNormalTexture ? 1u : 0u;
					materialChanged = true;
				}

				bool useMetallicTexture = editedProperties.UseMetallicTexture != 0;
				if (ImGui::Checkbox("使用金属度纹理", &useMetallicTexture))
				{
					editedProperties.UseMetallicTexture = useMetallicTexture ? 1u : 0u;
					materialChanged = true;
				}

				bool useRoughnessTexture = editedProperties.UseRoughnessTexture != 0;
				if (ImGui::Checkbox("使用粗糙度纹理", &useRoughnessTexture))
				{
					editedProperties.UseRoughnessTexture = useRoughnessTexture ? 1u : 0u;
					materialChanged = true;
				}

				if (materialChanged)
				{
					runtimeMaterial->Properties = editedProperties;
					m_dx->NotifyRuntimeMaterialChanged(runtimeMaterial->GetName());
					if (m_syncMaterialChangesToFile && !boundMaterialFilePath.empty())
						SaveRuntimeMaterialToMaterialFile(boundMaterialFilePath, *runtimeMaterial);
				}
			}
		}
	}

	// 灯光组件
	if (BeginInspectorComponentHeader("灯光", lightComponent))
	{
		RenderReadonlyComponentPopup();

		if (ImGui::BeginTable("LightComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("类型");
			ImGui::Text("颜色");
			ImGui::Text("强度");
			ImGui::Text("投射阴影");
			ImGui::Text("提示");

			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

			LightKind lightKind = lightComponent->GetKind();
			if (ImGui::BeginCombo("##LightKindComp", GetInspectorLightKindLabel(lightKind)))
			{
				for (LightKind option : { LightKind::Ambient, LightKind::Directional, LightKind::Spot, LightKind::Point })
				{
					const bool isSelected = (lightKind == option);
					if (ImGui::Selectable(GetInspectorLightKindLabel(option), isSelected))
					{
						const LightKind previousLightKind = lightKind;
						lightKind = option;
						lightComponent->SetKind(option);
						lightComponent->SetType(ResolveInspectorLightShaderType(option, lightComponent->GetType()));
						if (option == LightKind::Ambient)
							lightComponent->SetCastShadow(false);
						else if (previousLightKind == LightKind::Ambient)
							lightComponent->SetCastShadow(true);
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			DirectX::XMFLOAT3 lightColor = lightComponent->GetColor();
			if (ImGui::ColorEdit3("##LightColorComp", &lightColor.x))
				lightComponent->SetColor(lightColor);

			float lightPower = lightComponent->GetPower();
			if (ImGui::DragFloat("##LightPowerComp", &lightPower, 0.01f, 0.0f, 1000.0f, "%.3f"))
				lightComponent->SetPower((std::max)(0.0f, lightPower));

			bool castShadow = lightComponent->GetCastShadow();
			if (lightKind == LightKind::Ambient)
			{
				castShadow = false;
				ImGui::BeginDisabled();
				ImGui::Checkbox("##LightCastShadowComp", &castShadow);
				ImGui::EndDisabled();
			}
			else if (ImGui::Checkbox("##LightCastShadowComp", &castShadow))
			{
				lightComponent->SetCastShadow(castShadow);
			}

			if (lightKind == LightKind::Ambient)
				ImGui::TextDisabled("环境光不受变换影响。");
			else if (lightKind == LightKind::Directional)
				ImGui::TextDisabled("使用变换旋转来编辑方向。");
			else if (lightKind == LightKind::Point)
				ImGui::TextDisabled("使用变换位置来编辑位置。");
			else
				ImGui::TextDisabled("使用变换位置和旋转进行编辑。");

			ImGui::PopItemWidth();
			ImGui::EndTable();
		}
	}

	// 相机组件
	if (BeginInspectorComponentHeader("相机", cameraComponent))
	{
		if (RenderReplaceableComponentPopup(
			[&]() { return m_ecs->RebuildCameraComponentOnEntity(selectedEntity); },
			[&]() { return m_ecs->RemoveCameraComponentFromEntity(selectedEntity); }))
			return;

		if (ImGui::BeginTable("CameraComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("视场角");
			ImGui::Text("近裁剪面");
			ImGui::Text("远裁剪面");
			ImGui::Text("缩放");

			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

			float fov = 0.0f;
			float nearZ = 0.0f;
			float farZ = 0.0f;
			float scale = 0.0f;
			const bool hasFov = m_ecs->GetSelectedEntityCameraFov(&fov);
			const bool hasNear = m_ecs->GetSelectedEntityCameraNear(&nearZ);
			const bool hasFar = m_ecs->GetSelectedEntityCameraFar(&farZ);
			const bool hasScale = m_ecs->GetSelectedEntityCameraScale(&scale);

			if (hasFov)
			{
				float fovDegrees = fov * 256.0f;
				if (ImGui::SliderFloat("##FovCamComp", &fovDegrees, 1.0f, 179.0f))
					m_ecs->SetSelectedEntityCameraFov(fovDegrees / 256.0f);
			}

			if (hasNear && hasFar)
			{
				if (ImGui::DragFloat("##NearCamComp", &nearZ, 0.01f, 0.001f, farZ))
					m_ecs->SetSelectedEntityCameraNear(nearZ);

				if (ImGui::DragFloat("##FarCamComp", &farZ, 0.01f, nearZ, FLT_MAX))
					m_ecs->SetSelectedEntityCameraFar(farZ);
			}

			if (hasScale && ImGui::DragFloat("##ScaleCamComp", &scale, 0.01f, 0.1f, FLT_MAX))
				m_ecs->SetSelectedEntityCameraScale(scale);

			if (ImGui::Button("恢复默认缩放"))
				m_ecs->RestoreSelectedEntityCameraScale();

			ImGui::PopItemWidth();
			ImGui::EndTable();
		}
	}

	// 物理组件
	if (BeginInspectorComponentHeader("物理", physicsComponent))
	{
		RenderReadonlyComponentPopup();
		ImGui::TextDisabled("%s", kTemporarilyDisabledReason);
		ImGui::BeginDisabled();

		ImGui::Text("盒体碰撞器：%u", static_cast<UINT>(physicsComponent->GetBoxColliderCount()));
		for (size_t colliderIndex = 0; colliderIndex < physicsComponent->GetBoxColliderCount(); ++colliderIndex)
		{
			EntityPhysicsComponentData::ColliderSnapshot colliderSnapshot;
			if (!m_ecs->GetSelectedEntityPhysicsColliderSnapshot(colliderIndex, &colliderSnapshot))
				continue;

			ImGui::PushID(static_cast<int>(colliderIndex));
			if (ImGui::TreeNode("盒体碰撞器"))
			{
				bool colliderChanged = false;
				colliderChanged |= ImGui::Checkbox("启用", &colliderSnapshot.activeComponent);
				colliderChanged |= ImGui::DragFloat("静摩擦系数", &colliderSnapshot.staticFriction, 0.01f, 0.0f, FLT_MAX);
				colliderChanged |= ImGui::DragFloat("动摩擦系数", &colliderSnapshot.dynamicFriction, 0.01f, 0.0f, FLT_MAX);
				colliderChanged |= ImGui::DragFloat("恢复系数", &colliderSnapshot.restitution, 0.01f, 0.0f, FLT_MAX);
				colliderChanged |= ImGui::DragFloat3("中心", &colliderSnapshot.center.x, 0.01f);
				colliderChanged |= ImGui::DragFloat3("尺寸", &colliderSnapshot.size.x, 0.01f);
				if (colliderChanged)
					m_ecs->SetSelectedEntityPhysicsColliderSnapshot(colliderIndex, colliderSnapshot);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::EndDisabled();
	}

	// 脚本组件
	if (BeginInspectorComponentHeader("脚本", scriptingComponent))
	{
		RenderReadonlyComponentPopup();
		ImGui::TextDisabled("%s", kTemporarilyDisabledReason);
		ImGui::BeginDisabled();

		EntityScriptingComponentData scriptingSnapshot;
		if (m_ecs->GetSelectedEntityScriptingSnapshot(&scriptingSnapshot))
		{
			ImGui::Text("脚本数量：%u", static_cast<UINT>(scriptingSnapshot.scriptCount));
			for (size_t scriptIndex = 0; scriptIndex < scriptingSnapshot.scriptCount; ++scriptIndex)
			{
				EntityScriptingComponentData::ScriptSnapshot scriptSnapshot;
				if (!m_ecs->GetSelectedEntityScriptSnapshot(scriptIndex, &scriptSnapshot))
					continue;

				ImGui::PushID(static_cast<int>(scriptIndex));
				const std::wstring& displayName = scriptSnapshot.fileName.empty() ? scriptSnapshot.filePath : scriptSnapshot.fileName;
				ImGui::BulletText("%s", SString::WstringToUTF8(displayName).c_str());
				bool active = scriptSnapshot.activeComponent;
				if (ImGui::Checkbox("启用", &active))
					m_ecs->SetSelectedEntityScriptActive(scriptIndex, active);
				ImGui::SameLine();
				if (ImGui::SmallButton("移除"))
				{
					m_ecs->RemoveSelectedEntityScript(scriptIndex);
					ImGui::PopID();
					break;
				}
				ImGui::TextDisabled("路径：%s", SString::WstringToUTF8(scriptSnapshot.filePath).c_str());
				ImGui::PopID();
			}
		}
		ImGui::EndDisabled();
	}

	// 刚体组件
	if (BeginInspectorComponentHeader("刚体", rigidbodyComponent))
	{
		RenderReadonlyComponentPopup();
		ImGui::TextDisabled("%s", kTemporarilyDisabledReason);
		ImGui::BeginDisabled();

		EntityRigidBodyComponentData rigidBodySnapshot;
		if (m_ecs->GetSelectedEntityRigidBodySnapshot(&rigidBodySnapshot))
		{
			bool rigidBodyChanged = false;
			rigidBodyChanged |= ImGui::DragFloat("质量", &rigidBodySnapshot.mass, 0.01f, 0.0f, FLT_MAX);
			rigidBodyChanged |= ImGui::DragFloat("线性阻尼", &rigidBodySnapshot.linearDamping, 0.01f, 0.0f, FLT_MAX);
			rigidBodyChanged |= ImGui::DragFloat("角阻尼", &rigidBodySnapshot.angularDamping, 0.01f, 0.0f, FLT_MAX);
			rigidBodyChanged |= ImGui::Checkbox("使用重力", &rigidBodySnapshot.useGravity);
			rigidBodyChanged |= ImGui::Checkbox("运动学", &rigidBodySnapshot.kinematic);

			if (ImGui::TreeNode("线性锁定"))
			{
				rigidBodyChanged |= ImGui::Checkbox("锁定 X##Linear", &rigidBodySnapshot.linearLockX);
				rigidBodyChanged |= ImGui::Checkbox("锁定 Y##Linear", &rigidBodySnapshot.linearLockY);
				rigidBodyChanged |= ImGui::Checkbox("锁定 Z##Linear", &rigidBodySnapshot.linearLockZ);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("角度锁定"))
			{
				rigidBodyChanged |= ImGui::Checkbox("锁定 X##Angular", &rigidBodySnapshot.angularLockX);
				rigidBodyChanged |= ImGui::Checkbox("锁定 Y##Angular", &rigidBodySnapshot.angularLockY);
				rigidBodyChanged |= ImGui::Checkbox("锁定 Z##Angular", &rigidBodySnapshot.angularLockZ);
				ImGui::TreePop();
			}

			if (rigidBodyChanged)
				m_ecs->SetSelectedEntityRigidBodySnapshot(rigidBodySnapshot);
		}
		ImGui::EndDisabled();
	}

	RenderAdd();
}
