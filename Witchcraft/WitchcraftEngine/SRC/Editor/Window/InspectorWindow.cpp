#include "InspectorWindow.h"

#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"
#include "Helpers/Helpers.h"

#include "D3DWindow/D3DWindow.h"
#include "ECS/WitchcraECS.h"
#include "Engine/Engine.h"
#include "Common/SceneEntityType.h"
#include "System/Animation/AnimationLayerMask.h"
#include "System/Assets.h"
#include "System/WitchcraftFile/WMaterialFile.h"

#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <cfloat>
#include <filesystem>
#include <functional>
#include <unordered_set>

void InspectorWindow::Init(D3DWindow* dx, AssetsWindow* assetsWindow, PhysicsSystem* physicsSystem, WitchcraECS* ecs, Engine* engine)
{
	m_dx = dx;
	m_assetsWindow = assetsWindow;
	m_physicsSystem = physicsSystem;
	m_ecs = ecs;
	m_engine = engine;
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
			{
				const bool playModeActive = m_engine != nullptr && m_engine->IsPlayModeActive();
				if (playModeActive)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.24f, 1.0f), "Play Mode：Inspector 只读，运行态修改会在 Stop 后回滚。");
					ImGui::BeginDisabled();
				}
				RenderComponent(selectedView);
				if (playModeActive)
					ImGui::EndDisabled();
			}
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
			if (ImGui::MenuItem("从文件添加脚本..."))
			{
				if (m_ecs != nullptr && selectedView.entity != nullptr)
				{
					std::wstring scriptPath;
					const std::wstring initialDir = EngineUtils::GetProjectDirPath();
					if (EngineHelpers::TryOpenFileDialog(
						m_dx != nullptr ? m_dx->GetHwnd() : nullptr,
						initialDir.c_str(),
						L"Lua 脚本 (*.lua)\0*.lua\0所有文件 (*.*)\0*.*\0\0",
						L"选择脚本文件",
						&scriptPath))
					{
						if (!m_ecs->AddScriptToSelectedEntity(scriptPath))
						{
							EngineHelpers::ShowMessageBox(
								m_dx != nullptr ? m_dx->GetHwnd() : nullptr,
								L"添加脚本失败。可能是该脚本已挂载，或者当前实体不允许挂载脚本。",
								L"实体信息",
								MB_OK | MB_ICONWARNING);
						}
					}
				}
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

	struct InspectorRenderToTextureCameraOption
	{
		SceneEntityBase* Entity = nullptr;
		std::wstring Name;
		EntityCameraComponentData Camera;
	};

	auto collectRenderToTextureCameraOptions = [&](bool requireRenderable)
	{
		std::vector<InspectorRenderToTextureCameraOption> options;
		std::unordered_set<SceneEntityBase*> visitedEntities;

		std::function<void(SceneEntityBase*)> traverse;
		traverse = [&](SceneEntityBase* entity)
		{
			if (entity == nullptr)
				return;
			if (!visitedEntities.insert(entity).second)
				return;

			EntityCameraComponentData cameraData;
			if (m_ecs->GetEntityCameraSnapshot(entity, &cameraData))
			{
				const bool renderableToTexture =
					cameraData.renderEnabled &&
					cameraData.renderToTextureEnabled &&
					cameraData.outputTargetId != 0;
				if (!requireRenderable || renderableToTexture)
				{
					InspectorRenderToTextureCameraOption option;
					option.Entity = entity;
					option.Name = m_ecs->GetEntityName(entity);
					option.Camera = cameraData;
					options.push_back(option);
				}
			}

			for (SceneEntityBase* childEntity : m_ecs->GetSceneChildren(entity))
				traverse(childEntity);
		};

		for (SceneEntityBase* rootEntity : m_ecs->GetSceneRootEntities())
			traverse(rootEntity);

		return options;
	};

	auto allocateRenderToTextureOutputId = [&]()
	{
		std::unordered_set<std::uint32_t> usedOutputIds;
		for (const InspectorRenderToTextureCameraOption& option : collectRenderToTextureCameraOptions(false))
		{
			if (option.Camera.outputTargetId != 0)
				usedOutputIds.insert(option.Camera.outputTargetId);
		}

		for (std::uint32_t outputId = 1; outputId < 1024; ++outputId)
		{
			if (usedOutputIds.find(outputId) == usedOutputIds.end())
				return outputId;
		}

		return 0u;
	};

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

	SceneEntityBase* animationControlEntity = ResolveAnimationControlEntity(selectedEntity);
	AnimatorComponent* animationControlComponent = nullptr;
	if (animationControlEntity != nullptr)
		animationControlComponent = m_ecs->GetComponent<AnimatorComponent>(animationControlEntity);
	if (animationControlComponent == nullptr)
		animationControlComponent = animatorComponent;

	if (BeginInspectorComponentHeader("动画控制", animationControlComponent))
	{
		RenderReadonlyComponentPopup();

		if (animationControlEntity != nullptr)
		{
			ImGui::Text("控制目标：%s",
				SString::WstringToUTF8(m_ecs->GetEntityName(animationControlEntity)).c_str());
			if (animationControlEntity != selectedEntity)
				ImGui::TextDisabled("当前选中实体上的 Animator 不一定就是实际驱动蒙皮的那个。");
		}

		AnimatorComponent* editableAnimatorComponent = animationControlComponent;
		if (animationControlEntity != nullptr)
		{
			(void)EnsureAnimationRuntimeForControl(animationControlEntity);
			editableAnimatorComponent = m_ecs->GetComponent<AnimatorComponent>(animationControlEntity);
		}

		const Witchcraft::Animation::SkeletonData* animationControlSkeletonData =
			(animationControlEntity != nullptr && m_ecs->HasSkeletonData(animationControlEntity))
			? m_ecs->GetSkeletonData(animationControlEntity)
			: nullptr;

		if (editableAnimatorComponent == nullptr)
		{
			ImGui::TextDisabled("未找到可操作的动画控制组件。");
		}
		else
		{
			const std::vector<AnimatorComponent::AnimationLayer>& layers = editableAnimatorComponent->GetLayers();
			auto syncAnimationLayerTransitionUiState = [&]()
			{
				m_animationLayerTransitionTargets.resize(layers.size());
				const std::size_t oldDurationCount = m_animationLayerTransitionDurations.size();
				m_animationLayerTransitionDurations.resize(layers.size(), 0.2f);
				for (std::size_t stateIndex = oldDurationCount; stateIndex < m_animationLayerTransitionDurations.size(); ++stateIndex)
					m_animationLayerTransitionDurations[stateIndex] = 0.2f;
			};
			syncAnimationLayerTransitionUiState();

			ImGui::Text("动画层数：%u", static_cast<unsigned>(layers.size()));
			if (ImGui::Button("新增动画层"))
			{
				(void)editableAnimatorComponent->AddLayer();
				editableAnimatorComponent->SetEvaluateWhenPaused(true);
				m_animationLayerTransitionTargets.emplace_back();
				m_animationLayerTransitionDurations.push_back(0.2f);
			}
			ImGui::SameLine();
			if (ImGui::Button("确保 Base 层"))
			{
				(void)editableAnimatorComponent->EnsureBaseLayer();
				editableAnimatorComponent->SetEvaluateWhenPaused(true);
				syncAnimationLayerTransitionUiState();
			}

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
							const std::wstring droppedClipPath = relativePath.empty() ? std::wstring(file->full_path) : relativePath;
							std::size_t layerIndex = editableAnimatorComponent->AddLayer(std::filesystem::path(droppedClipPath).stem().wstring());
							(void)editableAnimatorComponent->SetLayerClipAssetPath(layerIndex, droppedClipPath);
							editableAnimatorComponent->SetEvaluateWhenPaused(true);
							m_animationLayerTransitionTargets.emplace_back();
							m_animationLayerTransitionDurations.push_back(0.2f);
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (layers.empty())
			{
				ImGui::TextDisabled("暂无动画层。");
			}

			int removeLayerIndex = -1;
			int moveLayerFromIndex = -1;
			int moveLayerToIndex = -1;
			for (std::size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex)
			{
				const AnimatorComponent::AnimationLayer layer = layers[layerIndex];
				ImGui::PushID(static_cast<int>(layerIndex));
				ImGui::Separator();
				const std::string title = SString::WstringToUTF8(
					layer.Name.empty() ? (L"Layer " + std::to_wstring(layerIndex)) : layer.Name);
				if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					std::string layerNameUtf8 = SString::WstringToUTF8(layer.Name);
					if (ImGui::InputText("名称", &layerNameUtf8, ImGuiInputTextFlags_EnterReturnsTrue))
						(void)editableAnimatorComponent->SetLayerName(layerIndex, SString::UTF8ToWstring(layerNameUtf8));
					if (ImGui::IsItemDeactivatedAfterEdit())
						(void)editableAnimatorComponent->SetLayerName(layerIndex, SString::UTF8ToWstring(layerNameUtf8));

					std::string clipPathUtf8 = SString::WstringToUTF8(layer.ClipAssetPath);
					if (ImGui::InputText("动画路径", &clipPathUtf8, ImGuiInputTextFlags_EnterReturnsTrue))
					{
						(void)editableAnimatorComponent->SetLayerClipAssetPath(layerIndex, SString::UTF8ToWstring(clipPathUtf8));
						editableAnimatorComponent->SetEvaluateWhenPaused(true);
					}
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						(void)editableAnimatorComponent->SetLayerClipAssetPath(layerIndex, SString::UTF8ToWstring(clipPathUtf8));
						editableAnimatorComponent->SetEvaluateWhenPaused(true);
					}

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
									(void)editableAnimatorComponent->SetLayerClipAssetPath(
										layerIndex,
										relativePath.empty() ? std::wstring(file->full_path) : relativePath);
									editableAnimatorComponent->SetEvaluateWhenPaused(true);
								}
							}
						}
						ImGui::EndDragDropTarget();
					}

					(void)RenderAnimationLayerMaskRootPicker(
						editableAnimatorComponent,
						layerIndex,
						layer,
						animationControlSkeletonData);

					float layerTime = layer.Time;
					if (ImGui::DragFloat("时间", &layerTime, 0.01f, 0.0f, FLT_MAX, "%.3f"))
						(void)editableAnimatorComponent->SetLayerTime(layerIndex, layerTime);

					float layerSpeed = layer.Speed;
					if (ImGui::DragFloat("速度", &layerSpeed, 0.01f, -8.0f, 8.0f, "%.3f"))
						(void)editableAnimatorComponent->SetLayerSpeed(layerIndex, layerSpeed);

					float layerWeight = layer.Weight;
					if (ImGui::SliderFloat("权重", &layerWeight, 0.0f, 1.0f, "%.3f"))
						(void)editableAnimatorComponent->SetLayerWeight(layerIndex, layerWeight);

					std::string& transitionTargetUtf8 = m_animationLayerTransitionTargets[layerIndex];
					float& transitionDuration = m_animationLayerTransitionDurations[layerIndex];
					if (transitionDuration <= 0.0f)
						transitionDuration = 0.2f;
					(void)ImGui::InputText("过渡目标", &transitionTargetUtf8);
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
									transitionTargetUtf8 = SString::WstringToUTF8(
										relativePath.empty() ? std::wstring(file->full_path) : relativePath);
								}
							}
						}
						ImGui::EndDragDropTarget();
					}
					ImGui::PushItemWidth(140.0f);
					(void)ImGui::DragFloat("过渡时长", &transitionDuration, 0.01f, 0.0f, 10.0f, "%.3f");
					ImGui::PopItemWidth();
					ImGui::SameLine();
					ImGui::BeginDisabled(transitionTargetUtf8.empty());
					if (ImGui::Button("淡入到目标##LayerCrossFade"))
					{
						(void)editableAnimatorComponent->CrossFadeLayerTo(
							layerIndex,
							SString::UTF8ToWstring(transitionTargetUtf8),
							(std::max)(0.0f, transitionDuration),
							0.0f);
						editableAnimatorComponent->SetEvaluateWhenPaused(true);
					}
					ImGui::EndDisabled();
					if (!layer.TransitionClipAssetPath.empty())
					{
						ImGui::Text("过渡中：%s %.3f / %.3f",
							SString::WstringToUTF8(std::filesystem::path(layer.TransitionClipAssetPath).stem().wstring()).c_str(),
							layer.TransitionElapsed,
							layer.TransitionDuration);
					}

					const char* blendModeItems[] = { "Override", "Additive" };
					int blendModeIndex = layer.BlendMode == AnimatorComponent::AnimationLayerBlendMode::Additive ? 1 : 0;
					if (ImGui::Combo("模式", &blendModeIndex, blendModeItems, IM_ARRAYSIZE(blendModeItems)))
					{
						(void)editableAnimatorComponent->SetLayerBlendMode(
							layerIndex,
							blendModeIndex == 1
							? AnimatorComponent::AnimationLayerBlendMode::Additive
							: AnimatorComponent::AnimationLayerBlendMode::Override);
					}

					bool enabled = layer.Enabled;
					if (ImGui::Checkbox("启用", &enabled))
						(void)editableAnimatorComponent->SetLayerEnabled(layerIndex, enabled);
					ImGui::SameLine();
					bool loop = layer.Loop;
					if (ImGui::Checkbox("循环", &loop))
						(void)editableAnimatorComponent->SetLayerLoop(layerIndex, loop);
					ImGui::SameLine();
					bool playing = layer.Playing;
					if (ImGui::Checkbox("播放", &playing))
						(void)editableAnimatorComponent->SetLayerPlaying(layerIndex, playing);

					if (ImGui::Button("播放##LayerPlay"))
						(void)editableAnimatorComponent->PlayLayer(layerIndex);
					ImGui::SameLine();
					if (ImGui::Button("暂停##LayerPause"))
						(void)editableAnimatorComponent->SetLayerPlaying(layerIndex, false);
					ImGui::SameLine();
					if (ImGui::Button("停止##LayerStop"))
						(void)editableAnimatorComponent->StopLayer(layerIndex);
					ImGui::SameLine();
					ImGui::BeginDisabled(layerIndex == 0);
					if (ImGui::Button("上移##LayerMoveUp"))
					{
						moveLayerFromIndex = static_cast<int>(layerIndex);
						moveLayerToIndex = static_cast<int>(layerIndex - 1);
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(layerIndex + 1 >= layers.size());
					if (ImGui::Button("下移##LayerMoveDown"))
					{
						moveLayerFromIndex = static_cast<int>(layerIndex);
						moveLayerToIndex = static_cast<int>(layerIndex + 1);
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ImGui::Button("移除##LayerRemove"))
						removeLayerIndex = static_cast<int>(layerIndex);
				}
				ImGui::PopID();
			}

			if (removeLayerIndex >= 0)
			{
				(void)editableAnimatorComponent->RemoveLayer(static_cast<std::size_t>(removeLayerIndex));
				const std::size_t removeStateIndex = static_cast<std::size_t>(removeLayerIndex);
				if (removeStateIndex < m_animationLayerTransitionTargets.size())
					m_animationLayerTransitionTargets.erase(m_animationLayerTransitionTargets.begin() + static_cast<std::ptrdiff_t>(removeStateIndex));
				if (removeStateIndex < m_animationLayerTransitionDurations.size())
					m_animationLayerTransitionDurations.erase(m_animationLayerTransitionDurations.begin() + static_cast<std::ptrdiff_t>(removeStateIndex));
				editableAnimatorComponent->SetEvaluateWhenPaused(true);
			}
			else if (moveLayerFromIndex >= 0 && moveLayerToIndex >= 0)
			{
				const std::size_t fromLayerIndex = static_cast<std::size_t>(moveLayerFromIndex);
				const std::size_t toLayerIndex = static_cast<std::size_t>(moveLayerToIndex);
				if (editableAnimatorComponent->MoveLayer(fromLayerIndex, toLayerIndex))
				{
					if (fromLayerIndex < m_animationLayerTransitionTargets.size() &&
						toLayerIndex < m_animationLayerTransitionTargets.size())
					{
						std::string transitionTarget = std::move(m_animationLayerTransitionTargets[fromLayerIndex]);
						m_animationLayerTransitionTargets.erase(
							m_animationLayerTransitionTargets.begin() + static_cast<std::ptrdiff_t>(fromLayerIndex));
						m_animationLayerTransitionTargets.insert(
							m_animationLayerTransitionTargets.begin() + static_cast<std::ptrdiff_t>(toLayerIndex),
							std::move(transitionTarget));
					}
					if (fromLayerIndex < m_animationLayerTransitionDurations.size() &&
						toLayerIndex < m_animationLayerTransitionDurations.size())
					{
						const float transitionDuration = m_animationLayerTransitionDurations[fromLayerIndex];
						m_animationLayerTransitionDurations.erase(
							m_animationLayerTransitionDurations.begin() + static_cast<std::ptrdiff_t>(fromLayerIndex));
						m_animationLayerTransitionDurations.insert(
							m_animationLayerTransitionDurations.begin() + static_cast<std::ptrdiff_t>(toLayerIndex),
							transitionDuration);
					}
				}
				editableAnimatorComponent->SetEvaluateWhenPaused(true);
			}
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

					bool useReflection = Material->EnableReflection || editedProperties.UseSpecularTexture != 0;
					if (ImGui::Checkbox("使用镜面反射", &useReflection))
					{
						Material->EnableReflection = useReflection;
						editedProperties.UseSpecularTexture = useReflection ? 1u : 0u;
						if (!useReflection)
						{
							Material->ReflectionSource = MaterialReflectionSource::SkyIBL;
							Material->ReflectionRenderToTextureId = 0;
						}
						materialChanged = true;
					}
					if (useReflection)
					{
						const char* reflectionSourceItems[] =
						{
							"天空 IBL",
							"虚拟相机 RenderTexture"
						};
						int reflectionSourceIndex =
							Material->ReflectionSource == MaterialReflectionSource::RenderToTexture ? 1 : 0;
						if (ImGui::Combo("镜面来源", &reflectionSourceIndex, reflectionSourceItems, IM_ARRAYSIZE(reflectionSourceItems)))
						{
							Material->ReflectionSource = reflectionSourceIndex == 1
								? MaterialReflectionSource::RenderToTexture
								: MaterialReflectionSource::SkyIBL;
							if (Material->ReflectionSource == MaterialReflectionSource::SkyIBL)
								Material->ReflectionRenderToTextureId = 0;
							materialChanged = true;
						}
						if (Material->ReflectionSource == MaterialReflectionSource::RenderToTexture)
						{
							const std::vector<InspectorRenderToTextureCameraOption> cameraOptions =
								collectRenderToTextureCameraOptions(true);
							int selectedCameraIndex = -1;
							std::string selectedCameraLabel = "未绑定相机";
							for (int optionIndex = 0; optionIndex < static_cast<int>(cameraOptions.size()); ++optionIndex)
							{
								const InspectorRenderToTextureCameraOption& option = cameraOptions[optionIndex];
								if (option.Camera.outputTargetId == Material->ReflectionRenderToTextureId)
								{
									selectedCameraIndex = optionIndex;
									selectedCameraLabel =
										SString::WstringToUTF8(option.Name) +
										" (RenderToTexture)";
									break;
								}
							}

							if (cameraOptions.empty())
							{
								ImGui::TextDisabled("没有开启 RenderToTexture 输出的相机。");
							}
							else
							{
								if (Material->ReflectionRenderToTextureId != 0 && selectedCameraIndex < 0)
									ImGui::TextDisabled("当前镜面相机不可用，请重新选择。");

								if (ImGui::BeginCombo("镜面相机", selectedCameraLabel.c_str()))
								{
									for (int optionIndex = 0; optionIndex < static_cast<int>(cameraOptions.size()); ++optionIndex)
									{
										const InspectorRenderToTextureCameraOption& option = cameraOptions[optionIndex];
										const std::string optionLabel =
											SString::WstringToUTF8(option.Name) +
											" (RenderToTexture)";
										const bool selected = optionIndex == selectedCameraIndex;
										if (ImGui::Selectable(optionLabel.c_str(), selected))
										{
											Material->ReflectionRenderToTextureId = option.Camera.outputTargetId;
											materialChanged = true;
										}
										if (selected)
											ImGui::SetItemDefaultFocus();
									}
									ImGui::EndCombo();
								}
							}
						}
					}

					if (ImGui::Checkbox("使用透明纹理", &useOpacityTexture))
					{
						Material->OpacityTexture = useOpacityTexture ? Material->DiffuseTexture : nullptr;
						materialChanged = true;
					}

					if (materialChanged)
					{
						editedProperties.ReflectionSource =
							useReflection &&
							Material->ReflectionSource == MaterialReflectionSource::RenderToTexture &&
							Material->ReflectionRenderToTextureId != 0
							? 1u
							: 0u;
						Material->Properties = editedProperties;
						m_dx->NotifyMaterialChanged(Material->GetName());
						if (m_dx != nullptr)
						{
							const std::uint32_t receiverRenderToTextureId =
								useReflection &&
								Material->ReflectionSource == MaterialReflectionSource::RenderToTexture
								? Material->ReflectionRenderToTextureId
								: 0u;
							m_dx->SetEntityReflectionReceiverRenderToTexture(selectedEntity, receiverRenderToTextureId);
						}
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
			ImGui::Text("参与相机渲染");
			ImGui::Text("RenderToTexture 输出");

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

			EntityCameraComponentData cameraSnapshot;
			if (m_ecs->GetEntityCameraSnapshot(selectedEntity, &cameraSnapshot))
			{
				bool renderEnabled = cameraSnapshot.renderEnabled;
				if (ImGui::Checkbox("##RenderEnabledCamComp", &renderEnabled))
				{
					cameraSnapshot.renderEnabled = renderEnabled;
					m_ecs->SetEntityCameraSnapshot(selectedEntity, cameraSnapshot);
				}

				bool renderToTextureEnabled = cameraSnapshot.renderToTextureEnabled;
				if (ImGui::Checkbox("##RenderToTextureEnabledCamComp", &renderToTextureEnabled))
				{
					cameraSnapshot.renderToTextureEnabled = renderToTextureEnabled;
					if (cameraSnapshot.renderToTextureEnabled && cameraSnapshot.outputTargetId == 0)
						cameraSnapshot.outputTargetId = allocateRenderToTextureOutputId();
					m_ecs->SetEntityCameraSnapshot(selectedEntity, cameraSnapshot);
				}

				if (!cameraSnapshot.renderToTextureEnabled)
				{
					ImGui::TextDisabled("未启用");
				}
				else
				{
					bool outputIdDuplicated = false;
					UINT enabledRenderToTextureCameraCount = 0;
					for (const InspectorRenderToTextureCameraOption& option : collectRenderToTextureCameraOptions(false))
					{
						if (option.Camera.renderEnabled &&
							option.Camera.renderToTextureEnabled &&
							option.Camera.outputTargetId != 0)
						{
							++enabledRenderToTextureCameraCount;
						}
						if (option.Entity != selectedEntity &&
							option.Camera.outputTargetId != 0 &&
							option.Camera.outputTargetId == cameraSnapshot.outputTargetId)
						{
							outputIdDuplicated = true;
						}
					}
					if (outputIdDuplicated)
					{
						const std::uint32_t repairedOutputId = allocateRenderToTextureOutputId();
						if (repairedOutputId != 0)
						{
							cameraSnapshot.outputTargetId = repairedOutputId;
							m_ecs->SetEntityCameraSnapshot(selectedEntity, cameraSnapshot);
						}
						else
						{
							ImGui::TextDisabled("内部输出目标不足，当前相机暂时不会输出。");
						}
					}
					if (enabledRenderToTextureCameraCount > MaxRenderToTextureCount)
						ImGui::TextDisabled("当前启用数量超过渲染端预留槽位，部分相机不会输出。");
				}
			}

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

bool InspectorWindow::RenderAnimationLayerMaskRootPicker(
	AnimatorComponent* animatorComponent,
	std::size_t layerIndex,
	const AnimatorComponent::AnimationLayer& layer,
	const Witchcraft::Animation::SkeletonData* skeletonData)
{
	if (animatorComponent == nullptr)
		return false;

	bool changed = false;
	if (skeletonData == nullptr || skeletonData->Topology.Bones.empty())
	{
		std::string maskRootUtf8 = SString::WstringToUTF8(layer.MaskRootBoneName);
		if (ImGui::InputText("Mask Root", &maskRootUtf8, ImGuiInputTextFlags_EnterReturnsTrue))
			changed = animatorComponent->SetLayerMaskRootBoneName(layerIndex, SString::UTF8ToWstring(maskRootUtf8));
		if (ImGui::IsItemDeactivatedAfterEdit())
			changed = animatorComponent->SetLayerMaskRootBoneName(layerIndex, SString::UTF8ToWstring(maskRootUtf8)) || changed;
		ImGui::TextDisabled("未找到骨架数据，暂时只能手动输入骨骼名。");
		return changed;
	}

	const Witchcraft::Animation::SkeletonTopology& topology = skeletonData->Topology;
	const bool isFullBody = layer.MaskRootBoneName.empty();
	const bool isValidMaskRoot = Witchcraft::Animation::IsAnimationLayerMaskRootValid(topology, layer.MaskRootBoneName);
	std::wstring previewText = isFullBody ? L"<Full Body>" : layer.MaskRootBoneName;
	if (!isFullBody && !isValidMaskRoot)
		previewText += L" (Invalid)";

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("Mask Root", SString::WstringToUTF8(previewText).c_str()))
	{
		const bool fullBodySelected = layer.MaskRootBoneName.empty();
		if (ImGui::Selectable("<Full Body>", fullBodySelected))
			changed = animatorComponent->SetLayerMaskRootBoneName(layerIndex, L"");
		if (fullBodySelected)
			ImGui::SetItemDefaultFocus();

		ImGui::Separator();
		static ImGuiTextFilter maskRootFilter;
		maskRootFilter.Draw("Search", -FLT_MIN);
		ImGui::Separator();
		for (std::size_t boneIndex = 0; boneIndex < topology.Bones.size(); ++boneIndex)
		{
			const Witchcraft::Animation::SkeletonBone& bone = topology.Bones[boneIndex];
			std::wstring labelText = bone.Name.empty() ? (L"Bone " + std::to_wstring(boneIndex)) : bone.Name;
			if (bone.ParentIndex >= 0)
				labelText = L"  " + labelText;
			const std::string optionLabel = SString::WstringToUTF8(labelText);
			if (!maskRootFilter.PassFilter(optionLabel.c_str()))
				continue;

			const bool isSelected = !layer.MaskRootBoneName.empty() && bone.Name == layer.MaskRootBoneName;
			if (ImGui::Selectable(optionLabel.c_str(), isSelected))
				changed = animatorComponent->SetLayerMaskRootBoneName(layerIndex, bone.Name);
			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	if (isValidMaskRoot)
	{
		const std::size_t maskedBoneCount = Witchcraft::Animation::CountAnimationLayerMaskedBones(topology, layer.MaskRootBoneName);
		if (isFullBody)
		{
			ImGui::TextDisabled(
				"Mask: Full body (%u bones)",
				static_cast<unsigned>(maskedBoneCount));
		}
		else
		{
			ImGui::TextDisabled(
				"Mask: %s subtree (%u / %u bones)",
				SString::WstringToUTF8(layer.MaskRootBoneName).c_str(),
				static_cast<unsigned>(maskedBoneCount),
				static_cast<unsigned>(topology.Bones.size()));
		}
	}
	else
	{
		ImGui::TextColored(
			ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
			"Invalid mask root: %s",
			SString::WstringToUTF8(layer.MaskRootBoneName).c_str());
	}

	return changed;
}

SceneEntityBase* InspectorWindow::ResolveAnimationControlEntity(SceneEntityBase* selectedEntity) const
{
	if (m_ecs == nullptr || selectedEntity == nullptr || !m_ecs->HasEntity(selectedEntity))
		return nullptr;

	if (m_ecs->IsSkeletonHierarchyEntity(selectedEntity))
	{
		SceneEntityBase* boundOwnerEntity = nullptr;
		std::int32_t boundBoneIndex = -1;
		if (m_ecs->TryGetSkeletonHierarchyBinding(selectedEntity, &boundOwnerEntity, &boundBoneIndex) &&
			boundOwnerEntity != nullptr &&
			m_ecs->HasEntity(boundOwnerEntity))
		{
			return boundOwnerEntity;
		}
	}

	for (SceneEntityBase* current = selectedEntity; current != nullptr; current = m_ecs->GetParentEntity(current))
	{
		if (m_ecs->HasSkeletonData(current))
			return current;
	}

	return selectedEntity;
}

bool InspectorWindow::EnsureAnimationRuntimeForControl(SceneEntityBase* animationControlEntity)
{
	if (m_ecs == nullptr || animationControlEntity == nullptr || !m_ecs->HasEntity(animationControlEntity))
		return false;

	if (m_ecs->HasSkeletonData(animationControlEntity) &&
		m_ecs->GetComponent<SkinningRuntimeComponent>(animationControlEntity) == nullptr)
	{
		(void)m_ecs->AddComponent<SkinningRuntimeComponent>(animationControlEntity);
	}

	if (m_ecs->GetComponent<AnimatorComponent>(animationControlEntity) == nullptr)
		(void)m_ecs->AddComponent<AnimatorComponent>(animationControlEntity);

	return m_ecs->GetComponent<AnimatorComponent>(animationControlEntity) != nullptr;
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
