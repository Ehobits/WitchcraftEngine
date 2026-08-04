#include "InspectorWindow.h"

#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"

#include "D3DWindow/D3DWindow.h"
#include "ECS/WitchcraECS.h"
#include "Common/SceneEntityType.h"
#include "System/Assets.h"
#include "System/WitchcraftFile/WMaterialFile.h"

#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <cfloat>
#include <filesystem>

static constexpr const char* kTemporarilyDisabledReason = "暂时禁用：桥（功能）尚未完成。";

void InspectorWindow::Init(D3DWindow* dx, AssetsWindow* assetsWindow, PhysicsSystem* physicsSystem, WitchcraECS* ecs)
{
	m_dx = dx;
	m_assetsWindow = assetsWindow;
	m_physicsSystem = physicsSystem;
	m_ecs = ecs;
	_Static = false;
}

void InspectorWindow::Render()
{
	if (!renderInspector)
		return;

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

		const bool hasRigidBody = (selectedView.rigidbodyComponent != nullptr);
		const bool hasPlaneCollider = (m_ecs != nullptr) ? m_ecs->HasPlaneColliderOnEntity(selectedView.entity) : false;
		const bool canAddRigidBody = !hasRigidBody && !hasPlaneCollider;
		if (ImGui::MenuItem("刚体", "", false, canAddRigidBody))
		{
			m_ecs->AddRigidbodyToSelectedEntity();
		}
		else if (hasRigidBody)
		{
			ImGui::TextDisabled("已添加");
		}
		else if (hasPlaneCollider)
		{
			ImGui::TextDisabled("平面碰撞器不支持刚体");
		}

		ImGui::Separator();

		if (ImGui::BeginMenu("物理"))
		{
			const bool canAddCollider = (selectedView.transformComponent != nullptr);
			if (ImGui::MenuItem("盒体碰撞器", "", false, canAddCollider))
			{
				m_ecs->AddBoxColliderToSelectedEntity();
			}
			if (ImGui::MenuItem("平面碰撞器", "", false, canAddCollider))
			{
				m_ecs->AddPlaneColliderToSelectedEntity();
			}
			else if (!canAddCollider)
			{
				ImGui::TextDisabled("需要变换组件");
			}
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
	auto* skeletonComponent = context.skeletonComponent;
	auto* skeletonData = context.skeletonData;
	auto* animatorComponent = context.animatorComponent;
	auto* skinnedMeshComponent = context.skinnedMeshComponent;
	auto* skinningRuntimeComponent = context.skinningRuntimeComponent;
	auto* lightComponent = context.lightComponent;
	auto* billboardComponent = context.billboardComponent;
	auto* physicsComponent = context.physicsComponent;
	auto* scriptingComponent = context.scriptingComponent;
	auto* rigidbodyComponent = context.rigidbodyComponent;
	Transform editableLocalTransform = context.editableLocalTransform;
	const std::vector<EditorAssetCache::MaterialFileEntry>& materialFileCache = EditorAssetCache::GetMaterialFiles();
	const std::vector<std::wstring>& skyTextureFileCache = EditorAssetCache::GetSkyTextures();
	const bool isSkyEntity = context.isSkyEntity;
	const bool isAmbientLightEntity = m_ecs->IsAmbientLightEntity(selectedEntity);
	const std::wstring& entityTypeLabel = context.entityTypeLabel;

	auto refreshRenderItemTransform = [&]()
		{
			if (m_dx != nullptr)
				m_dx->UpdateRenderItemsTransformFromEntity(selectedEntity, m_ecs);
		};
	auto refreshAfterLightChange = [&]()
		{
			if (m_dx != nullptr)
			{
				m_dx->FreshenLightCBs();
				m_dx->FreshenMaterialCBs();
				m_dx->FreshenObjectCBs();
			}
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
			ImGui::Text("实体类型");
			ImGui::Text("可见");
			ImGui::Text("静态");
			ImGui::Text("标签");

			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

			std::string entityName = SString::WstringToUTF8(m_ecs->GetEntityName(selectedEntity));
			if (ImGui::InputText("##NameGeneralComponent", &entityName, ImGuiInputTextFlags_EnterReturnsTrue))
				m_ecs->RenameSelectedEntity(SString::UTF8ToWstring(entityName));

			ImGui::TextUnformatted(SString::WstringToUTF8(entityTypeLabel).c_str());

			SceneEntityType sceneType = context.sceneEntityType;
			std::string sceneTypePreview = SString::WstringToUTF8(SceneEntityTypeToDisplayName(sceneType));
			if (ImGui::BeginCombo("##SceneEntityTypeGeneralComponent", sceneTypePreview.c_str()))
			{
				for (std::uint32_t typeIndex = 0; typeIndex < static_cast<std::uint32_t>(SceneEntityType::Count); ++typeIndex)
				{
					const SceneEntityType option = static_cast<SceneEntityType>(typeIndex);
					const bool isSelected = option == sceneType;
					const std::string optionLabel = SString::WstringToUTF8(SceneEntityTypeToDisplayName(option));
					if (ImGui::Selectable(optionLabel.c_str(), isSelected))
					{
						sceneType = option;
						m_ecs->SetSelectedEntitySceneType(option);
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			bool visible = m_ecs->IsEntitySelfVisible(selectedEntity);
			if (ImGui::Checkbox("##VisibleGeneralComponent", &visible))
			{
				m_ecs->SetSelectedEntityVisible(visible);
				if (m_dx != nullptr)
				{
					// 可见性是层级语义：即使当前实体自己没有 Mesh / Light，
					// 也可能影响整棵子树里的渲染项与灯光。
					if (visible)
						m_dx->AddRenderItemsFromEntity(selectedEntity, m_ecs);
					else
						m_dx->RemoveRenderItemsFromEntity(selectedEntity, m_ecs);

					refreshAfterLightChange();
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
	if (!isAmbientLightEntity && BeginInspectorComponentHeader("变换", transformComponent))
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

			ImGui::TableNextColumn();
			ImGui::Text("%u", meshComponent->GetNumVertices());
			ImGui::Text("%u", meshComponent->GetNumFaces());

			ImGui::EndTable();
		}

	}

	// 骨架实例组件
	if ((skeletonData != nullptr || skeletonComponent != nullptr) &&
		ImGui::CollapsingHeader("骨架实例", ImGuiTreeNodeFlags_DefaultOpen))
	{
		RenderReadonlyComponentPopup();
		ImGui::Text("骨架资源：%s",
			SString::WstringToUTF8(
				skeletonData != nullptr
				? skeletonData->SkeletonAssetPath
				: (skeletonComponent != nullptr ? skeletonComponent->GetSkeletonAssetPath() : std::wstring())).c_str());
		ImGui::Text("骨骼数量：%u",
			static_cast<UINT>(
				skeletonData != nullptr
				? skeletonData->BoneNames.size()
				: (skeletonComponent != nullptr ? skeletonComponent->GetBoneNames().size() : 0)));
		ImGui::Text("姿态待更新：%s",
			(skeletonData != nullptr
				? skeletonData->Dirty
				: (skeletonComponent != nullptr && skeletonComponent->IsDirty())) ? "是" : "否");
	}

	if (BeginInspectorComponentHeader("蒙皮网格", skinnedMeshComponent))
	{
		RenderReadonlyComponentPopup();
		ImGui::Text("蒙皮资源：%s",
			SString::WstringToUTF8(skinnedMeshComponent->GetSkinnedMeshAssetPath()).c_str());
		ImGui::Text("骨架资源：%s",
			SString::WstringToUTF8(skinnedMeshComponent->GetSkeletonAssetPath()).c_str());
		ImGui::Text("材质槽数量：%u",
			static_cast<UINT>(skinnedMeshComponent->GetMaterialSlots().size()));
	}

	if (BeginInspectorComponentHeader("蒙皮结果", skinningRuntimeComponent))
	{
		RenderReadonlyComponentPopup();
		ImGui::Text("最终骨矩阵数：%u",
			static_cast<UINT>(skinningRuntimeComponent->GetPalette().FinalBoneMatrices.size()));
		ImGui::Text("结果版本：%llu",
			static_cast<unsigned long long>(skinningRuntimeComponent->GetPalette().Revision));
		ImGui::Text("Palette 待更新：%s",
			skinningRuntimeComponent->IsPaletteDirty() ? "是" : "否");
	}

	if (BeginInspectorComponentHeader("动画控制", animatorComponent))
	{
		RenderReadonlyComponentPopup();

		std::string clipAssetPath = SString::WstringToUTF8(animatorComponent->GetClipAssetPath());
		const std::wstring currentAnimationName = animatorComponent->GetClipAssetPath().empty()
			? std::wstring()
			: std::filesystem::path(animatorComponent->GetClipAssetPath()).stem().wstring();
		ImGui::Text("当前动画：%s",
			SString::WstringToUTF8(currentAnimationName.empty() ? std::wstring(L"无") : currentAnimationName).c_str());
		if (ImGui::InputText("动画资源", &clipAssetPath, ImGuiInputTextFlags_EnterReturnsTrue))
			animatorComponent->SetClipAssetPath(SString::UTF8ToWstring(clipAssetPath));
		if (ImGui::IsItemDeactivatedAfterEdit())
			animatorComponent->SetClipAssetPath(SString::UTF8ToWstring(clipAssetPath));

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_DEMO_ASS"))
			{
				if (payload->Data != nullptr && payload->DataSize == static_cast<int>(sizeof(AssetDragPayload)))
				{
					const AssetDragPayload* file = static_cast<const AssetDragPayload*>(payload->Data);
					if (file != nullptr &&
						!file->is_dir &&
						file->file_type == FILEs::File_Type::WANIMFILE)
					{
						const std::wstring relativePath = ToProjectRelativePath(std::filesystem::path(file->full_path));
						animatorComponent->SetClipAssetPath(relativePath.empty() ? std::wstring(file->full_path) : relativePath);
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		float currentTime = animatorComponent->GetTime();
		if (ImGui::DragFloat("时间", &currentTime, 0.01f, 0.0f, FLT_MAX, "%.3f"))
			animatorComponent->SetTime((std::max)(0.0f, currentTime));

		float speed = animatorComponent->GetSpeed();
		if (ImGui::DragFloat("速度", &speed, 0.01f, -8.0f, 8.0f, "%.3f"))
			animatorComponent->SetSpeed(speed);

		bool loop = animatorComponent->IsLoop();
		if (ImGui::Checkbox("循环", &loop))
			animatorComponent->SetLoop(loop);

		bool playing = animatorComponent->IsPlaying();
		if (ImGui::Checkbox("播放", &playing))
			animatorComponent->SetPlaying(playing);

		if (ImGui::Button("播放##AnimatorPlay"))
			animatorComponent->SetPlaying(true);
		ImGui::SameLine();
		if (ImGui::Button("暂停##AnimatorPause"))
			animatorComponent->SetPlaying(false);
		ImGui::SameLine();
		if (ImGui::Button("停止##AnimatorStop"))
		{
			animatorComponent->SetPlaying(false);
			animatorComponent->SetTime(0.0f);
		}
	}

	// 所用材质
	if (BeginInspectorComponentHeader("所用材质", meshComponent))
	{
		RenderReadonlyComponentPopup();

		const std::wstring currentMaterialName = meshComponent->GetMaterialName();
		if (isSkyEntity)
		{
			if (skyTextureFileCache.empty())
			{
				ImGui::TextDisabled("在 DATA/HDRIs 下未找到 PNG 文件。");
			}
			else
			{
				const std::wstring currentSkyTexturePath = m_dx != nullptr
					? m_dx->GetSkyTexturePathByMaterialName(currentMaterialName)
					: L"";
				size_t currentSkyIndex = 0;
				for (size_t i = 0; i < skyTextureFileCache.size(); ++i)
				{
					if (!currentSkyTexturePath.empty() && skyTextureFileCache[i] == currentSkyTexturePath)
					{
						currentSkyIndex = i;
						break;
					}
				}

				std::wstring previewText = std::filesystem::path(skyTextureFileCache[currentSkyIndex]).filename().wstring();
				if (!currentSkyTexturePath.empty())
					previewText = std::filesystem::path(currentSkyTexturePath).filename().wstring();

				const std::string preview = SString::WstringToUTF8(previewText);
				if (ImGui::BeginCombo("##SkyTextureSelector", preview.c_str()))
				{
					for (size_t i = 0; i < skyTextureFileCache.size(); ++i)
					{
						const std::wstring filename = std::filesystem::path(skyTextureFileCache[i]).filename().wstring();
						const bool isSelected = (i == currentSkyIndex);
						if (ImGui::Selectable(SString::WstringToUTF8(filename).c_str(), isSelected) && m_dx != nullptr)
						{
							const std::wstring MaterialName = m_dx->GetOrCreateSkyMaterial(skyTextureFileCache[i]);
							meshComponent->SetMaterial(MaterialName);
						}

						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}

			if (ImGui::Button("刷新天空纹理列表"))
			{
				EditorAssetCache::MarkSkyTexturesDirty();
			}
		}
		else
		{
			size_t currentMaterialIndex = 0;
			const std::wstring currentMaterialFilePath = m_dx != nullptr
				? m_dx->GetMaterialFilePathByMaterialName(currentMaterialName)
				: L"";
			for (size_t i = 0; i < materialFileCache.size(); ++i)
			{
				if (!currentMaterialFilePath.empty() && materialFileCache[i].path == currentMaterialFilePath)
				{
					currentMaterialIndex = i;
					break;
				}
			}

			if (materialFileCache.empty())
			{
				ImGui::TextDisabled("在 ImportedAssets 下未找到 .wmat 文件。");
			}
			else
			{
				std::wstring previewText = materialFileCache[currentMaterialIndex].displayName;
				if (currentMaterialFilePath.empty())
					previewText = currentMaterialName.empty() ? L"<未绑定>" : currentMaterialName;
				else
				{
					const std::wstring fileName = std::filesystem::path(materialFileCache[currentMaterialIndex].path).filename().wstring();
					previewText += L" (" + fileName + L")";
				}

				const std::string preview = SString::WstringToUTF8(previewText);
				if (ImGui::BeginCombo("##MeshMaterialSelector", preview.c_str()))
				{
					for (size_t i = 0; i < materialFileCache.size(); ++i)
					{
						const bool isSelected = (i == currentMaterialIndex);
						ImGui::PushID(static_cast<int>(i));
						const std::wstring fileName = std::filesystem::path(materialFileCache[i].path).filename().wstring();
						const std::wstring itemText = materialFileCache[i].displayName + L" (" + fileName + L")";
						const std::string displayName = SString::WstringToUTF8(itemText);
						if (ImGui::Selectable(displayName.c_str(), isSelected) && m_dx != nullptr)
						{
							const std::wstring MaterialName = m_dx->GetOrCreateMaterialFromWMaterialFile(materialFileCache[i].path);
							meshComponent->SetMaterial(MaterialName);
							currentMaterialIndex = i;
						}

						if (isSelected)
							ImGui::SetItemDefaultFocus();
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}

				if (m_dx != nullptr)
				{
					const std::wstring updatedMaterialFilePath = m_dx->GetMaterialFilePathByMaterialName(meshComponent->GetMaterialName());
					if (!updatedMaterialFilePath.empty())
						ImGui::TextDisabled("路径：ImportedAssets/%s", SString::WstringToUTF8(materialFileCache[currentMaterialIndex].relativePath).c_str());
					else
						ImGui::TextDisabled("路径：当前材质未绑定到 .wmat 文件。");
				}
			}

			if (ImGui::Button("刷新材质列表"))
			{
				EditorAssetCache::MarkMaterialFilesDirty();
			}

			if (m_dx != nullptr)
			{
				const std::wstring boundMaterialName = meshComponent->GetMaterialName();
				const std::wstring boundMaterialFilePath = m_dx->GetMaterialFilePathByMaterialName(boundMaterialName);
				Material* Material = m_dx->GetMaterialByMaterialName(boundMaterialName);
				if (Material != nullptr)
				{
					ImGui::Separator();
					ImGui::TextDisabled("运行时材质：%s", SString::WstringToUTF8(Material->GetName()).c_str());
					if (!boundMaterialFilePath.empty())
						ImGui::Checkbox("同步修改到 .wmat", &m_syncMaterialChangesToFile);

					MaterialConstants editedProperties = Material->Properties;
					bool useOpacityTexture = Material->OpacityTexture != nullptr && Material->DiffuseTexture != nullptr;
					bool materialChanged = false;
					materialChanged |= ImGui::DragFloat("不透明度", &editedProperties.Opacity, 0.005f, 0.0f, 1.0f, "%.3f");
					materialChanged |= ImGui::ColorEdit4("漫反射颜色", &editedProperties.DiffuseAlbedo.x);
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

					bool useSpecularTexture = editedProperties.UseSpecularTexture != 0;
					if (ImGui::Checkbox("使用镜面纹理", &useSpecularTexture))
					{
						editedProperties.UseSpecularTexture = useSpecularTexture ? 1u : 0u;
						materialChanged = true;
					}

					if (ImGui::Checkbox("使用透明纹理", &useOpacityTexture))
					{
						Material->OpacityTexture = useOpacityTexture ? Material->DiffuseTexture : nullptr;
						materialChanged = true;
					}

					if (materialChanged)
					{
						Material->Properties = editedProperties;
						m_dx->NotifyMaterialChanged(Material->GetName());
						if (m_syncMaterialChangesToFile && !boundMaterialFilePath.empty())
							SaveMaterialToMaterialFile(boundMaterialFilePath, *Material);
					}

					if (!boundMaterialFilePath.empty())
					{
						static std::wstring s_textureEditMaterialPath;
						static std::string s_diffuseTextureUtf8;
						static std::string s_normalTextureUtf8;
						static std::string s_metallicTextureUtf8;
						static std::string s_roughnessTextureUtf8;
						static std::string s_opacityTextureUtf8;

						if (s_textureEditMaterialPath != boundMaterialFilePath)
						{
							WMaterialFileData loadedMaterialData;
							if (WMaterialFile::LoadFromFile(boundMaterialFilePath, &loadedMaterialData))
							{
								s_diffuseTextureUtf8 = SString::WstringToUTF8(
									ResolveTextureDisplayPath(loadedMaterialData.DiffuseTexture, boundMaterialFilePath));
								s_normalTextureUtf8 = SString::WstringToUTF8(
									ResolveTextureDisplayPath(loadedMaterialData.NormalTexture, boundMaterialFilePath));
								s_metallicTextureUtf8 = SString::WstringToUTF8(
									ResolveTextureDisplayPath(loadedMaterialData.MetallicTexture, boundMaterialFilePath));
								s_roughnessTextureUtf8 = SString::WstringToUTF8(
									ResolveTextureDisplayPath(loadedMaterialData.RoughnessTexture, boundMaterialFilePath));
								s_opacityTextureUtf8 = SString::WstringToUTF8(
									ResolveTextureDisplayPath(loadedMaterialData.OpacityTexture, boundMaterialFilePath));
							}
							else
							{
								s_diffuseTextureUtf8.clear();
								s_normalTextureUtf8.clear();
								s_metallicTextureUtf8.clear();
								s_roughnessTextureUtf8.clear();
								s_opacityTextureUtf8.clear();
							}
							s_textureEditMaterialPath = boundMaterialFilePath;
						}

						ImGui::Separator();
						if (ImGui::TreeNode("贴图"))
						{
							ImGui::TextDisabled("支持：基础色 / 法线 / 金属度 / 粗糙度 / 透明。");
							ImGui::TextDisabled("可填写绝对路径，或相对项目目录 / 当前 .wmat 的路径。");
							bool textureChanged = false;
							textureChanged |= ImGui::InputText("基础色贴图", &s_diffuseTextureUtf8);
							textureChanged |= AcceptTextureAssetDrop(&s_diffuseTextureUtf8, boundMaterialFilePath);
							textureChanged |= ImGui::InputText("法线贴图", &s_normalTextureUtf8);
							textureChanged |= AcceptTextureAssetDrop(&s_normalTextureUtf8, boundMaterialFilePath);
							textureChanged |= ImGui::InputText("金属度贴图", &s_metallicTextureUtf8);
							textureChanged |= AcceptTextureAssetDrop(&s_metallicTextureUtf8, boundMaterialFilePath);
							textureChanged |= ImGui::InputText("粗糙度贴图", &s_roughnessTextureUtf8);
							textureChanged |= AcceptTextureAssetDrop(&s_roughnessTextureUtf8, boundMaterialFilePath);
							textureChanged |= ImGui::InputText("透明贴图", &s_opacityTextureUtf8);
							textureChanged |= AcceptTextureAssetDrop(&s_opacityTextureUtf8, boundMaterialFilePath);

							if (textureChanged)
							{
								WMaterialFileData editedMaterialData;
								if (!WMaterialFile::LoadFromFile(boundMaterialFilePath, &editedMaterialData))
									editedMaterialData.MaterialName = Material->GetName();

								editedMaterialData.DiffuseTexture = NormalizeToGenericPathString(SString::UTF8ToWstring(s_diffuseTextureUtf8));
								editedMaterialData.NormalTexture = NormalizeToGenericPathString(SString::UTF8ToWstring(s_normalTextureUtf8));
								editedMaterialData.MetallicTexture = NormalizeToGenericPathString(SString::UTF8ToWstring(s_metallicTextureUtf8));
								editedMaterialData.RoughnessTexture = NormalizeToGenericPathString(SString::UTF8ToWstring(s_roughnessTextureUtf8));
								editedMaterialData.OpacityTexture = NormalizeToGenericPathString(SString::UTF8ToWstring(s_opacityTextureUtf8));

								editedMaterialData.UseNormalTexture = Material->Properties.UseNormalTexture != 0;
								editedMaterialData.UseMetallicTexture = Material->Properties.UseMetallicTexture != 0;
								editedMaterialData.UseRoughnessTexture = Material->Properties.UseRoughnessTexture != 0;
								editedMaterialData.UseOpacityTexture = useOpacityTexture;

								if (m_dx->ApplyMaterialPbrTexturesFromWMaterialData(
									boundMaterialName,
									boundMaterialFilePath,
									editedMaterialData))
								{
									if (m_syncMaterialChangesToFile)
										WMaterialFile::SaveToFile(boundMaterialFilePath, editedMaterialData);
								}
							}

							if (ImGui::Button("重载 .wmat 贴图"))
							{
								WMaterialFileData loadedMaterialData;
								if (WMaterialFile::LoadFromFile(boundMaterialFilePath, &loadedMaterialData))
								{
									s_diffuseTextureUtf8 = SString::WstringToUTF8(
										ResolveTextureDisplayPath(loadedMaterialData.DiffuseTexture, boundMaterialFilePath));
									s_normalTextureUtf8 = SString::WstringToUTF8(
										ResolveTextureDisplayPath(loadedMaterialData.NormalTexture, boundMaterialFilePath));
									s_metallicTextureUtf8 = SString::WstringToUTF8(
										ResolveTextureDisplayPath(loadedMaterialData.MetallicTexture, boundMaterialFilePath));
									s_roughnessTextureUtf8 = SString::WstringToUTF8(
										ResolveTextureDisplayPath(loadedMaterialData.RoughnessTexture, boundMaterialFilePath));
									s_opacityTextureUtf8 = SString::WstringToUTF8(
										ResolveTextureDisplayPath(loadedMaterialData.OpacityTexture, boundMaterialFilePath));
									m_dx->ApplyMaterialPbrTexturesFromWMaterialData(
										boundMaterialName,
										boundMaterialFilePath,
										loadedMaterialData);
								}
							}

							ImGui::TreePop();
						}
					}
				}
			}
		}
	}

	// Billboard 组件
	if (BeginInspectorComponentHeader("告示牌", billboardComponent))
	{
		RenderReadonlyComponentPopup();

		if (ImGui::BeginTable("BillboardComponentTable", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("模式");
			ImGui::Text("朝向");
			ImGui::Text("宽度");
			ImGui::Text("高度");
			ImGui::Text("屏幕尺寸");
			ImGui::Text("偏移");
			ImGui::Text("材质");
			ImGui::Text("提示");

			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

			BillboardMode billboardMode = billboardComponent->GetMode();
			const char* billboardModeLabel = billboardMode == BillboardMode::ScreenSize ? "屏幕尺寸" : "世界尺寸";
			if (ImGui::BeginCombo("##BillboardModeComp", billboardModeLabel))
			{
				const bool worldSelected = billboardMode == BillboardMode::WorldSize;
				if (ImGui::Selectable("世界尺寸", worldSelected))
				{
					billboardComponent->SetMode(BillboardMode::WorldSize);
					refreshRenderItemTransform();
				}
				if (worldSelected)
					ImGui::SetItemDefaultFocus();

				const bool screenSelected = billboardMode == BillboardMode::ScreenSize;
				if (ImGui::Selectable("屏幕尺寸", screenSelected))
				{
					billboardComponent->SetMode(BillboardMode::ScreenSize);
					refreshRenderItemTransform();
				}
				if (screenSelected)
					ImGui::SetItemDefaultFocus();

				ImGui::EndCombo();
			}

			BillboardFacingMode facingMode = billboardComponent->GetFacingMode();
			const char* facingModeLabel = facingMode == BillboardFacingMode::YAxisOnly ? "仅 Y 轴" : "面向相机";
			if (ImGui::BeginCombo("##BillboardFacingModeComp", facingModeLabel))
			{
				const bool faceCameraSelected = facingMode == BillboardFacingMode::FaceCamera;
				if (ImGui::Selectable("面向相机", faceCameraSelected))
				{
					billboardComponent->SetFacingMode(BillboardFacingMode::FaceCamera);
					refreshRenderItemTransform();
				}
				if (faceCameraSelected)
					ImGui::SetItemDefaultFocus();

				const bool yAxisSelected = facingMode == BillboardFacingMode::YAxisOnly;
				if (ImGui::Selectable("仅 Y 轴", yAxisSelected))
				{
					billboardComponent->SetFacingMode(BillboardFacingMode::YAxisOnly);
					refreshRenderItemTransform();
				}
				if (yAxisSelected)
					ImGui::SetItemDefaultFocus();

				ImGui::EndCombo();
			}

			float billboardWidth = billboardComponent->GetWidth();
			if (ImGui::DragFloat("##BillboardWidthComp", &billboardWidth, 0.01f, 0.001f, 10000.0f, "%.3f"))
			{
				billboardComponent->SetSize(billboardWidth, billboardComponent->GetHeight());
				refreshRenderItemTransform();
			}

			float billboardHeight = billboardComponent->GetHeight();
			if (ImGui::DragFloat("##BillboardHeightComp", &billboardHeight, 0.01f, 0.001f, 10000.0f, "%.3f"))
			{
				billboardComponent->SetSize(billboardComponent->GetWidth(), billboardHeight);
				refreshRenderItemTransform();
			}

			float billboardScreenSize = billboardComponent->GetScreenSize();
			if (billboardMode != BillboardMode::ScreenSize)
			{
				ImGui::BeginDisabled();
				ImGui::DragFloat("##BillboardScreenSizeComp", &billboardScreenSize, 1.0f, 1.0f, 4096.0f, "%.1f");
				ImGui::EndDisabled();
			}
			else if (ImGui::DragFloat("##BillboardScreenSizeComp", &billboardScreenSize, 1.0f, 1.0f, 4096.0f, "%.1f"))
			{
				billboardComponent->SetScreenSize(billboardScreenSize);
				refreshRenderItemTransform();
			}

			DirectX::XMFLOAT3 billboardOffset = billboardComponent->GetOffset();
			if (ImGui::DragFloat3("##BillboardOffsetComp", &billboardOffset.x, 0.01f))
			{
				billboardComponent->SetOffset(billboardOffset);
				refreshRenderItemTransform();
			}

			const std::wstring currentBillboardMaterialName = billboardComponent->GetMaterialName();
			size_t currentMaterialIndex = 0;
			const std::wstring currentBillboardMaterialFilePath = m_dx != nullptr
				? m_dx->GetMaterialFilePathByMaterialName(currentBillboardMaterialName)
				: L"";
			for (size_t i = 0; i < materialFileCache.size(); ++i)
			{
				if (!currentBillboardMaterialFilePath.empty() && materialFileCache[i].path == currentBillboardMaterialFilePath)
				{
					currentMaterialIndex = i;
					break;
				}
			}

			if (materialFileCache.empty())
			{
				ImGui::TextDisabled("在 ImportedAssets 下未找到 .wmat 文件。");
			}
			else
			{
				std::wstring previewText = currentBillboardMaterialName.empty() ? L"autoMat（默认）" : currentBillboardMaterialName;
				if (!currentBillboardMaterialFilePath.empty())
				{
					const std::wstring fileName = std::filesystem::path(materialFileCache[currentMaterialIndex].path).filename().wstring();
					previewText = materialFileCache[currentMaterialIndex].displayName + L" (" + fileName + L")";
				}

				if (ImGui::BeginCombo("##BillboardMaterialSelector", SString::WstringToUTF8(previewText).c_str()))
				{
					const bool defaultSelected = currentBillboardMaterialName.empty();
					if (ImGui::Selectable("autoMat（默认）", defaultSelected))
					{
						billboardComponent->SetMaterialName(L"");
						refreshRenderItemTransform();
					}
					if (defaultSelected)
						ImGui::SetItemDefaultFocus();

					for (size_t i = 0; i < materialFileCache.size(); ++i)
					{
						ImGui::PushID(static_cast<int>(i));
						const bool isSelected = (!currentBillboardMaterialFilePath.empty() && i == currentMaterialIndex);
						const std::wstring fileName = std::filesystem::path(materialFileCache[i].path).filename().wstring();
						const std::wstring itemText = materialFileCache[i].displayName + L" (" + fileName + L")";
						if (ImGui::Selectable(SString::WstringToUTF8(itemText).c_str(), isSelected) && m_dx != nullptr)
						{
							const std::wstring materialName = m_dx->GetOrCreateMaterialFromWMaterialFile(materialFileCache[i].path);
							billboardComponent->SetMaterialName(materialName);
							refreshRenderItemTransform();
						}
						if (isSelected)
							ImGui::SetItemDefaultFocus();
						ImGui::PopID();
					}

					ImGui::EndCombo();
				}
			}

			ImGui::TextDisabled("当前实现：复用透明通路，CPU 每帧更新朝向。");

			ImGui::PopItemWidth();
			ImGui::EndTable();
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
			ImGui::Text("体积光");
			ImGui::Text("体积强度");
			ImGui::Text("衰减距离");
			ImGui::Text("提示");

			ImGui::TableNextColumn();
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

			LightKind lightKind = lightComponent->GetKind();
			if (isAmbientLightEntity)
			{
				ImGui::TextUnformatted(GetInspectorLightKindLabel(lightKind));
			}
			else if (ImGui::BeginCombo("##LightKindComp", GetInspectorLightKindLabel(lightKind)))
			{
				// 普通灯组件只允许在平行光 / 聚光 / 点光之间切换，环境光由环境实体独立管理。
				for (LightKind option : { LightKind::Directional, LightKind::Spot, LightKind::Point })
				{
					const bool isSelected = (lightKind == option);
					if (ImGui::Selectable(GetInspectorLightKindLabel(option), isSelected))
					{
						lightKind = option;
						lightComponent->SetKind(option);
						lightComponent->SetType(ResolveInspectorLightShaderType(option, lightComponent->GetType()));
						refreshAfterLightChange();
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			DirectX::XMFLOAT3 lightColor = lightComponent->GetColor();
			if (ImGui::ColorEdit3("##LightColorComp", &lightColor.x))
			{
				lightComponent->SetColor(lightColor);
				refreshAfterLightChange();
			}

			float lightPower = lightComponent->GetPower();
			if (ImGui::DragFloat("##LightPowerComp", &lightPower, 0.01f, 0.0f, 1000.0f, "%.3f"))
			{
				lightComponent->SetPower((std::max)(0.0f, lightPower));
				refreshAfterLightChange();
			}

			bool castShadow = lightComponent->GetCastShadow();
			if (lightKind == LightKind::Ambient)
			{
				castShadow = false;
				ImGui::BeginDisabled();
				ImGui::Checkbox("##LightCastShadowComp", &castShadow);
				ImGui::EndDisabled();
			}
			else if (lightKind != LightKind::Directional && lightKind != LightKind::Spot && lightKind != LightKind::Point)
			{
				castShadow = false;
				ImGui::BeginDisabled();
				ImGui::Checkbox("##LightCastShadowComp", &castShadow);
				ImGui::EndDisabled();
			}
			else if (ImGui::Checkbox("##LightCastShadowComp", &castShadow))
			{
				lightComponent->SetCastShadow(castShadow);
				refreshAfterLightChange();
			}

			bool enableVolumetric = lightComponent->GetEnableVolumetric();
			if (lightKind == LightKind::Ambient)
			{
				enableVolumetric = false;
				ImGui::BeginDisabled();
				ImGui::Checkbox("##LightEnableVolumetricComp", &enableVolumetric);
				ImGui::EndDisabled();
			}
			else if (lightKind != LightKind::Directional && lightKind != LightKind::Spot && lightKind != LightKind::Point)
			{
				enableVolumetric = false;
				ImGui::BeginDisabled();
				ImGui::Checkbox("##LightEnableVolumetricComp", &enableVolumetric);
				ImGui::EndDisabled();
			}
			else if (ImGui::Checkbox("##LightEnableVolumetricComp", &enableVolumetric))
			{
				lightComponent->SetEnableVolumetric(enableVolumetric);
				refreshAfterLightChange();
			}

			float volumetricIntensity = lightComponent->GetVolumetricIntensity();
			const bool volumetricParametersEditable =
				(lightKind == LightKind::Directional || lightKind == LightKind::Spot || lightKind == LightKind::Point) &&
				enableVolumetric;
			if (!volumetricParametersEditable)
			{
				ImGui::BeginDisabled();
				ImGui::DragFloat("##LightVolumetricIntensityComp", &volumetricIntensity, 0.01f, 0.0f, 8.0f, "%.3f");
				ImGui::EndDisabled();
			}
			else if (ImGui::DragFloat("##LightVolumetricIntensityComp", &volumetricIntensity, 0.01f, 0.0f, 8.0f, "%.3f"))
			{
				lightComponent->SetVolumetricIntensity(volumetricIntensity);
				refreshAfterLightChange();
			}

			float volumetricAttenuationDistance = lightComponent->GetVolumetricAttenuationDistance();
			if (!volumetricParametersEditable)
			{
				ImGui::BeginDisabled();
				ImGui::DragFloat("##LightVolumetricAttenuationDistanceComp", &volumetricAttenuationDistance, 0.1f, 0.1f, 500.0f, "%.2f");
				ImGui::EndDisabled();
			}
			else if (ImGui::DragFloat("##LightVolumetricAttenuationDistanceComp", &volumetricAttenuationDistance, 0.1f, 0.1f, 500.0f, "%.2f"))
			{
				lightComponent->SetVolumetricAttenuationDistance(volumetricAttenuationDistance);
				refreshAfterLightChange();
			}

			if (lightKind == LightKind::Ambient)
				ImGui::TextDisabled("环境光不受变换影响。");
			else if (lightKind == LightKind::Directional)
				ImGui::TextDisabled("使用变换旋转来编辑方向。");
			else if (lightKind == LightKind::Point)
				ImGui::TextDisabled("使用变换位置来编辑位置；点光已支持六面阴影。");
			else
				ImGui::TextDisabled("使用变换位置和旋转进行编辑；聚光已支持阴影。");

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

		ImGui::Text("碰撞器：%u", static_cast<UINT>(physicsComponent->GetBoxColliderCount()));
		for (size_t colliderIndex = 0; colliderIndex < physicsComponent->GetBoxColliderCount(); ++colliderIndex)
		{
			EntityPhysicsComponentData::ColliderSnapshot colliderSnapshot;
			if (!m_ecs->GetSelectedEntityPhysicsColliderSnapshot(colliderIndex, &colliderSnapshot))
				continue;

			ImGui::PushID(static_cast<int>(colliderIndex));
			if (ImGui::TreeNode("碰撞器"))
			{
				bool colliderChanged = false;
				int colliderType = static_cast<int>(colliderSnapshot.colliderType);
				const char* colliderTypeItems[] = { "盒体", "平面" };
				if (ImGui::Combo("类型", &colliderType, colliderTypeItems, IM_ARRAYSIZE(colliderTypeItems)))
				{
					colliderType = (std::max)(0, (std::min)(colliderType, 1));
					colliderSnapshot.colliderType = static_cast<std::uint32_t>(colliderType);
					if (colliderSnapshot.colliderType == static_cast<std::uint32_t>(PhysicsColliderType::Plane))
					{
						if (colliderSnapshot.size.x <= 0.0f)
							colliderSnapshot.size.x = 10.0f;
						colliderSnapshot.size.y = 0.0f;
						colliderSnapshot.size.z = colliderSnapshot.size.x;
					}
					else
					{
						if (colliderSnapshot.size.x <= 0.0f) colliderSnapshot.size.x = 1.0f;
						if (colliderSnapshot.size.y <= 0.0f) colliderSnapshot.size.y = 1.0f;
						if (colliderSnapshot.size.z <= 0.0f) colliderSnapshot.size.z = 1.0f;
					}
					colliderChanged = true;
				}

				colliderChanged |= ImGui::Checkbox("启用", &colliderSnapshot.activeComponent);
				colliderChanged |= ImGui::DragFloat("静摩擦系数", &colliderSnapshot.staticFriction, 0.01f, 0.0f, FLT_MAX);
				colliderChanged |= ImGui::DragFloat("动摩擦系数", &colliderSnapshot.dynamicFriction, 0.01f, 0.0f, FLT_MAX);
				colliderChanged |= ImGui::DragFloat("恢复系数", &colliderSnapshot.restitution, 0.01f, 0.0f, FLT_MAX);
				colliderChanged |= ImGui::DragFloat3("中心", &colliderSnapshot.center.x, 0.01f);
				if (colliderSnapshot.colliderType == static_cast<std::uint32_t>(PhysicsColliderType::Plane))
				{
					colliderChanged |= ImGui::DragFloat("半径", &colliderSnapshot.size.x, 0.01f, 0.1f, FLT_MAX);
					colliderSnapshot.size.y = 0.0f;
					colliderSnapshot.size.z = colliderSnapshot.size.x;
				}
				else
				{
					colliderChanged |= ImGui::DragFloat3("尺寸", &colliderSnapshot.size.x, 0.01f);
				}
				if (colliderChanged)
					m_ecs->SetSelectedEntityPhysicsColliderSnapshot(colliderIndex, colliderSnapshot);

				if (ImGui::Button("移除"))
				{
					if (m_ecs->RemoveSelectedEntityPhysicsCollider(colliderIndex))
					{
						ImGui::TreePop();
						ImGui::PopID();
						break;
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
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
		if (RenderReplaceableComponentPopup(
			[&]() { return m_ecs->RebuildRigidbodyComponentOnEntity(selectedEntity); },
			[&]() { return m_ecs->RemoveRigidbodyComponentFromEntity(selectedEntity); }))
			return;

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

			if (ImGui::Button("移除##Rigidbody"))
			{
				m_ecs->RemoveRigidbodyComponentFromEntity(selectedEntity);
				return;
			}
		}
	}

	RenderAdd();
}

bool InspectorWindow::IsSkyMesh(const MeshComponent* meshComponent)
{
	return meshComponent != nullptr && meshComponent->GetRenderLayerIndex() == 天空渲染项目;
}

//std::wstring InspectorWindow::GetInspectorEntityTypeLabel(WitchcraECS* ecs, SceneEntityBase* entity, const MeshComponent* meshComponent, const CameraComponent* cameraComponent, const TransformComponent* transformComponent)
//{
//	if (IsSkyMesh(meshComponent))
//		return L"天空";
//
//	if (ecs != nullptr && entity != nullptr)
//	{
//		const std::wstring flecsTypeLabel = ecs->GetEntityTypeLabel(entity);
//		if (!flecsTypeLabel.empty() && flecsTypeLabel != L"未知")
//			return flecsTypeLabel;
//	}
//
//	if (meshComponent != nullptr)
//		return L"网格";
//	if (cameraComponent != nullptr)
//		return L"相机";
//	if (transformComponent != nullptr)
//		return L"空实体";
//	return L"未知";
//}

const char* InspectorWindow::GetInspectorLightKindLabel(LightKind kind)
{
	switch (kind)
	{
	case LightKind::Ambient:
		return "环境光";
	case LightKind::Directional:
		return "平行光";
	case LightKind::Spot:
		return "聚光";
	case LightKind::Point:
		return "点光";
	default:
		return "未知";
	}
}

float InspectorWindow::ResolveInspectorLightShaderType(LightKind kind, float fallbackType)
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

bool InspectorWindow::SaveMaterialToMaterialFile(const std::filesystem::path& materialFilePath, Material& material)
{
	if (materialFilePath.empty())
		return false;

	WMaterialFileData materialData;
	if (!WMaterialFile::LoadFromFile(materialFilePath, &materialData))
		materialData.MaterialName = material.GetName();

	materialData.DiffuseColor = material.Properties.DiffuseAlbedo;
	materialData.FresnelR0 = material.Properties.FresnelR0;
	materialData.Emissive = material.Properties.Emissive;
	materialData.UseNormalTexture = material.Properties.UseNormalTexture != 0;
	materialData.UseMetallicTexture = material.Properties.UseMetallicTexture != 0;
	materialData.UseRoughnessTexture = material.Properties.UseRoughnessTexture != 0;
	materialData.UseSpecularTexture = material.Properties.UseSpecularTexture != 0;
	materialData.UseOpacityTexture = material.OpacityTexture != nullptr && material.DiffuseTexture != nullptr;
	materialData.Metallic = material.Properties.Metallic;
	materialData.Roughness = material.Properties.Roughness;
	materialData.Opacity = material.Properties.Opacity;
	if (!materialData.UseOpacityTexture)
		materialData.OpacityTexture.clear();
	else if (materialData.OpacityTexture.empty())
		materialData.OpacityTexture = materialData.DiffuseTexture;

	return WMaterialFile::SaveToFile(materialFilePath, materialData);
}

std::wstring InspectorWindow::NormalizeToGenericPathString(const std::wstring& pathText)
{
	if (pathText.empty())
		return std::wstring();
	return std::filesystem::path(pathText).lexically_normal().generic_wstring();
}

std::wstring InspectorWindow::ToProjectRelativePath(const std::filesystem::path& sourcePath)
{
	if (sourcePath.empty())
		return std::wstring();

	const std::filesystem::path normalizedPath = sourcePath.lexically_normal();
	if (normalizedPath.is_relative())
		return normalizedPath.generic_wstring();

	const std::filesystem::path projectRoot =
		std::filesystem::path(EngineUtils::GetProjectDirPath()).lexically_normal();
	if (!projectRoot.empty())
	{
		std::error_code relativeError;
		const std::filesystem::path relativePath = std::filesystem::relative(normalizedPath, projectRoot, relativeError);
		if (!relativeError)
		{
			const std::wstring relativeText = relativePath.generic_wstring();
			if (!relativeText.empty() && relativeText != L"." && relativeText.rfind(L"..", 0) != 0)
				return relativeText;
		}
	}

	return normalizedPath.generic_wstring();
}

std::wstring InspectorWindow::ResolveTextureDisplayPath(const std::wstring& storedPath, const std::filesystem::path& materialFilePath)
{
	if (storedPath.empty())
		return std::wstring();

	std::filesystem::path texturePath(storedPath);
	texturePath = texturePath.lexically_normal();
	if (!texturePath.is_relative())
		return ToProjectRelativePath(texturePath);

	const std::filesystem::path projectRoot = std::filesystem::path(EngineUtils::GetProjectDirPath());
	if (!projectRoot.empty())
	{
		const std::filesystem::path projectRelativeCandidate = projectRoot / texturePath;
		if (std::filesystem::exists(projectRelativeCandidate))
			return ToProjectRelativePath(projectRelativeCandidate);
	}

	if (texturePath.has_parent_path())
	{
		if (!materialFilePath.empty())
		{
			const std::filesystem::path materialRelativeCandidate = materialFilePath.parent_path() / texturePath;
			if (std::filesystem::exists(materialRelativeCandidate))
				return ToProjectRelativePath(materialRelativeCandidate);
		}
		return texturePath.generic_wstring();
	}

	std::filesystem::path resolvedPath = texturePath;
	if (!materialFilePath.empty())
	{
		const std::filesystem::path texturesCandidate = materialFilePath.parent_path().parent_path() / L"Textures" / texturePath;
		if (std::filesystem::exists(texturesCandidate))
			resolvedPath = texturesCandidate;
		else
			resolvedPath = materialFilePath.parent_path() / texturePath;
	}

	return ToProjectRelativePath(resolvedPath);
}

bool InspectorWindow::AcceptTextureAssetDrop(std::string* targetPathUtf8, const std::filesystem::path& materialFilePath)
{
	if (targetPathUtf8 == nullptr)
		return false;

	bool changed = false;
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_DEMO_ASS"))
		{
			if (payload->Data != nullptr && payload->DataSize == static_cast<int>(sizeof(AssetDragPayload)))
			{
				const AssetDragPayload* file = static_cast<const AssetDragPayload*>(payload->Data);
				if (file != nullptr &&
					!file->is_dir &&
					(file->file_type == FILEs::File_Type::PNGFILE || file->file_type == FILEs::File_Type::DDSFILE))
				{
					const std::wstring displayPath = ResolveTextureDisplayPath(file->full_path, materialFilePath);
					const std::string displayPathUtf8 = SString::WstringToUTF8(displayPath);
					if (*targetPathUtf8 != displayPathUtf8)
					{
						*targetPathUtf8 = displayPathUtf8;
						changed = true;
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	return changed;
}

bool InspectorWindow::RenderReadonlyComponentPopup()
{
	if (!ImGui::BeginPopupContextItem())
		return false;

	ImGui::TextDisabled("当前组件暂无可用操作");
	ImGui::EndPopup();
	return false;
}

template<typename TComponent>
inline bool InspectorWindow::BeginInspectorComponentHeader(const char* label, const TComponent* component)
{
	return component != nullptr && ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
}

template<typename OnRebuild, typename OnRemove>
bool InspectorWindow::RenderReplaceableComponentPopup(OnRebuild&& onRebuild, OnRemove&& onRemove)
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
