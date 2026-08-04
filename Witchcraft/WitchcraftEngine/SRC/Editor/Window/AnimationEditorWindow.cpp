#include "AnimationEditorWindow.h"

#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/SkinnedMeshComponent.h"
#include "ECS/Component/SkinningRuntimeComponent.h"
#include "ECS/WitchcraECS.h"
#include "String/SStringUtils.h"

void AnimationEditorWindow::Init(WitchcraECS* ecs)
{
	m_ecs = ecs;
}

void AnimationEditorWindow::Render()
{
	if (!renderAnimationEditor)
		return;

	std::string title = "动画编辑器##AnimationEditorWindow";
	if (!ImGui::Begin(title.c_str(), &renderAnimationEditor))
	{
		ImGui::End();
		return;
	}

	if (!m_hasLoadedAnimation)
	{
		ImGui::TextDisabled("未打开动画文件。");
		ImGui::End();
		return;
	}

	const std::wstring displayName =
		m_data.Clip.Name.empty() ? m_currentPath.stem().wstring() : m_data.Clip.Name;
	ImGui::TextWrapped("当前动画：%s", SString::WstringToUTF8(displayName).c_str());
	ImGui::TextWrapped("文件：%s", SString::WstringToUTF8(m_currentPath.wstring()).c_str());

	SceneEntityBase* selectedEntity = nullptr;
	AnimatorComponent* selectedAnimatorComponent = nullptr;
	if (m_ecs != nullptr && m_ecs->HasSelectedEntity())
	{
		selectedEntity = m_ecs->GetSelectedEntity();
		selectedAnimatorComponent = m_ecs->GetComponent<AnimatorComponent>(selectedEntity);
	}

	if (selectedEntity != nullptr)
	{
		ImGui::TextWrapped(
			"当前选中实体：%s",
			SString::WstringToUTF8(m_ecs->GetEntityName(selectedEntity)).c_str());
		const std::wstring appliedAnimation = selectedAnimatorComponent != nullptr
			? selectedAnimatorComponent->GetClipAssetPath()
			: std::wstring();
		ImGui::TextWrapped(
			"该实体当前应用动画：%s",
			SString::WstringToUTF8(appliedAnimation.empty() ? std::wstring(L"无") : appliedAnimation).c_str());
	}
	else
	{
		ImGui::TextDisabled("当前未选中任何实体。");
	}

	auto syncPreviewToEntities =
		[&](SceneEntityBase* entity, bool createIfMissing) -> AnimatorComponent*
	{
		if (m_ecs == nullptr || entity == nullptr)
			return nullptr;

		std::vector<SceneEntityBase*> targetEntities;
		targetEntities.reserve(4);
		CollectAnimationOwnerEntities(m_ecs, entity, targetEntities);
		if (targetEntities.empty())
			targetEntities.push_back(entity);

		AnimatorComponent* firstAnimatorComponent = nullptr;
		for (SceneEntityBase* targetEntity : targetEntities)
		{
			if (m_ecs->HasSkeletonData(targetEntity) &&
				m_ecs->GetComponent<SkinningRuntimeComponent>(targetEntity) == nullptr)
			{
				// AnimationSystem 以 SkeletonData + Animator + Runtime 为同一实体的
				// 求值契约；为旧场景的骨架根补齐缺失的运行时容器。
				(void)m_ecs->AddComponent<SkinningRuntimeComponent>(targetEntity);
			}

			AnimatorComponent* animatorComponent = m_ecs->GetComponent<AnimatorComponent>(targetEntity);
			if (animatorComponent == nullptr && createIfMissing)
				animatorComponent = m_ecs->AddComponent<AnimatorComponent>(targetEntity);
			if (animatorComponent == nullptr)
				continue;

			animatorComponent->SetClipAssetPath(m_currentPath.wstring());
			animatorComponent->SetTime(m_previewTime);
			animatorComponent->SetLoop(m_data.Clip.Loop);
			animatorComponent->SetPlaying(m_previewPlaying);
			animatorComponent->SetEvaluateWhenPaused(true);
			if (firstAnimatorComponent == nullptr)
				firstAnimatorComponent = animatorComponent;
		}

		return firstAnimatorComponent;
	};

	auto setPreviewTarget = [&](SceneEntityBase* entity)
	{
		if (entity != nullptr && m_ecs != nullptr && m_ecs->HasEntity(entity))
			m_previewTargetEntity = entity;
	};

	if (!m_statusMessage.empty())
	{
		const std::string utf8Status = SString::WstringToUTF8(m_statusMessage);
		ImGui::TextColored(m_statusColor, "%s", utf8Status.c_str());
	}
	if (m_isDirty)
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "有未保存修改");

	if (ImGui::Button("保存"))
	{
		m_data.Clip.Name = ReadUtf8Buffer(m_clipName);
		for (auto& track : m_data.Clip.Tracks)
		{
			SortKeysByTime(track.TranslationKeys);
			SortKeysByTime(track.RotationKeys);
			SortKeysByTime(track.ScaleKeys);
		}

		if (WAnimationFile::SaveToFile(m_currentPath, m_data))
		{
			m_isDirty = false;
			m_statusMessage = L"保存成功";
			m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
		}
		else
		{
			m_statusMessage = L"保存失败";
			m_statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("重新加载"))
		OpenAnimationFile(m_currentPath.wstring());

	ImGui::SameLine();
	if (ImGui::Button("应用到当前选中实体"))
	{
		if (selectedEntity == nullptr)
		{
			m_statusMessage = L"当前没有选中实体";
			m_statusColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
		}
		else
		{
			setPreviewTarget(selectedEntity);
			AnimatorComponent* animatorComponent = syncPreviewToEntities(selectedEntity, true);
			if (animatorComponent == nullptr)
			{
				m_statusMessage = L"无法为当前实体创建动画控制组件";
				m_statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
			}
			else
			{
				m_statusMessage = L"已应用到当前选中实体";
				m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
				selectedAnimatorComponent = animatorComponent;
			}
		}
	}

	ImGui::Separator();

	if (m_previewPlaying)
	{
		const float ticksPerSecond = m_data.Clip.TicksPerSecond > 0.0f
			? m_data.Clip.TicksPerSecond
			: 1.0f;
		m_previewTime += ImGui::GetIO().DeltaTime * ticksPerSecond;
		if (m_data.Clip.Duration > 0.0f)
		{
			if (m_data.Clip.Loop)
			{
				while (m_previewTime > m_data.Clip.Duration)
					m_previewTime -= m_data.Clip.Duration;
			}
			else if (m_previewTime > m_data.Clip.Duration)
			{
				m_previewTime = m_data.Clip.Duration;
				m_previewPlaying = false;
			}
		}
	}

	float previewDuration = (std::max)(m_data.Clip.Duration, 0.0f);
	if (previewDuration <= 0.0f)
		previewDuration = 1.0f;

	bool previewChanged = false;
	previewChanged |= ImGui::SliderFloat("预览时间", &m_previewTime, 0.0f, previewDuration, "%.3f");
	ImGui::SameLine();
	ImGui::Text(" / %.3f", m_data.Clip.Duration);
	ImGui::SameLine();
	if (ImGui::Button("-1t"))
	{
		m_previewTime = (std::max)(0.0f, m_previewTime - 1.0f);
		previewChanged = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("+1t"))
	{
		m_previewTime = (std::min)(previewDuration, m_previewTime + 1.0f);
		previewChanged = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("-10t"))
	{
		m_previewTime = (std::max)(0.0f, m_previewTime - 10.0f);
		previewChanged = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("+10t"))
	{
		m_previewTime = (std::min)(previewDuration, m_previewTime + 10.0f);
		previewChanged = true;
	}

	if (ImGui::Button(m_previewPlaying ? "暂停预览" : "播放预览"))
	{
		// 仅在尚未选择过预览目标时绑定当前实体；暂停后切换层级选择不应改变目标。
		if (!m_previewPlaying && m_previewTargetEntity == nullptr)
			setPreviewTarget(selectedEntity);
		m_previewPlaying = !m_previewPlaying;
		previewChanged = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("停止预览"))
	{
		m_previewPlaying = false;
		m_previewTime = 0.0f;
		previewChanged = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("同步到当前实体") && selectedEntity != nullptr)
	{
		setPreviewTarget(selectedEntity);
		AnimatorComponent* animatorComponent = syncPreviewToEntities(selectedEntity, true);
		if (animatorComponent != nullptr)
		{
			m_statusMessage = L"已同步预览到当前实体";
			m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
			selectedAnimatorComponent = animatorComponent;
		}
	}

	if (m_previewTargetEntity != nullptr &&
		(m_ecs == nullptr || !m_ecs->HasEntity(m_previewTargetEntity)))
	{
		m_previewTargetEntity = nullptr;
	}

	if ((m_previewPlaying || previewChanged) && m_previewTargetEntity != nullptr)
	{
		// 预览只写回明确绑定的目标，不能随层级窗口的当前选择漂移。
		AnimatorComponent* previewAnimatorComponent =
			m_ecs != nullptr ? m_ecs->GetComponent<AnimatorComponent>(m_previewTargetEntity) : nullptr;
		const bool needCreate =
			previewAnimatorComponent == nullptr ||
			previewAnimatorComponent->GetClipAssetPath() != m_currentPath.wstring();
		(void)syncPreviewToEntities(m_previewTargetEntity, needCreate);
	}

	ImGui::Separator();

	bool changed = false;
	changed |= ImGui::InputText("动画名称", m_clipName, IM_ARRAYSIZE(m_clipName));
	changed |= ImGui::DragFloat("时长", &m_data.Clip.Duration, 0.01f, 0.0f, FLT_MAX, "%.3f");
	changed |= ImGui::DragFloat("TicksPerSecond", &m_data.Clip.TicksPerSecond, 0.01f, 0.0f, FLT_MAX, "%.3f");
	changed |= ImGui::Checkbox("循环", &m_data.Clip.Loop);

	ImGui::Separator();
	ImGui::Text("轨道数量：%u", static_cast<unsigned>(m_data.Clip.Tracks.size()));

	if (ImGui::BeginChild("AnimationTracks", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
	{
		for (size_t trackIndex = 0; trackIndex < m_data.Clip.Tracks.size(); ++trackIndex)
		{
			auto& track = m_data.Clip.Tracks[trackIndex];
			ImGui::PushID(static_cast<int>(trackIndex));

			std::wstring header = track.BoneName.empty()
				? (L"Track " + std::to_wstring(trackIndex))
				: track.BoneName;

			if (ImGui::CollapsingHeader(SString::WstringToUTF8(header).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				char boneName[256] = {};
				WriteUtf8Buffer(track.BoneName, boneName, IM_ARRAYSIZE(boneName));
				if (ImGui::InputText("骨骼名", boneName, IM_ARRAYSIZE(boneName)))
				{
					track.BoneName = ReadUtf8Buffer(boneName);
					changed = true;
				}

				int boneIndex = track.BoneIndex;
				if (ImGui::InputInt("骨骼索引", &boneIndex))
				{
					track.BoneIndex = boneIndex;
					changed = true;
				}

				if (ImGui::TreeNode("平移关键帧"))
				{
					for (size_t keyIndex = 0; keyIndex < track.TranslationKeys.size(); ++keyIndex)
					{
						auto& key = track.TranslationKeys[keyIndex];
						ImGui::PushID(static_cast<int>(keyIndex));
						if (ImGui::TreeNode(("Key##T" + std::to_string(keyIndex)).c_str()))
						{
							changed |= ImGui::DragFloat("时间", &key.Time, 0.01f, 0.0f, FLT_MAX, "%.3f");
							changed |= ImGui::DragFloat3("值", &key.Value.x, 0.01f);
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("旋转关键帧"))
				{
					for (size_t keyIndex = 0; keyIndex < track.RotationKeys.size(); ++keyIndex)
					{
						auto& key = track.RotationKeys[keyIndex];
						ImGui::PushID(static_cast<int>(keyIndex));
						if (ImGui::TreeNode(("Key##R" + std::to_string(keyIndex)).c_str()))
						{
							changed |= ImGui::DragFloat("时间", &key.Time, 0.01f, 0.0f, FLT_MAX, "%.3f");
							changed |= ImGui::DragFloat4("值", &key.Value.x, 0.01f);
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("缩放关键帧"))
				{
					for (size_t keyIndex = 0; keyIndex < track.ScaleKeys.size(); ++keyIndex)
					{
						auto& key = track.ScaleKeys[keyIndex];
						ImGui::PushID(static_cast<int>(keyIndex));
						if (ImGui::TreeNode(("Key##S" + std::to_string(keyIndex)).c_str()))
						{
							changed |= ImGui::DragFloat("时间", &key.Time, 0.01f, 0.0f, FLT_MAX, "%.3f");
							changed |= ImGui::DragFloat3("值", &key.Value.x, 0.01f);
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
			}

			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	if (changed)
	{
		m_isDirty = true;
		m_statusMessage.clear();
	}

	ImGui::End();
}

void AnimationEditorWindow::NeedRender(bool render)
{
	renderAnimationEditor = render;
}

bool AnimationEditorWindow::OpenAnimationFile(const std::wstring& path)
{
	WAnimationFileData loadedData;
	if (!WAnimationFile::LoadFromFile(path, &loadedData))
	{
		m_statusMessage = L"打开动画失败";
		m_statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		return false;
	}

	m_currentPath = path;
	m_data = std::move(loadedData);
	m_hasLoadedAnimation = true;
	m_isDirty = false;
	m_previewPlaying = false;
	m_previewTime = 0.0f;
	m_previewTargetEntity = nullptr;
	renderAnimationEditor = true;
	m_statusMessage = L"已加载动画";
	m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
	WriteUtf8Buffer(m_data.Clip.Name, m_clipName, IM_ARRAYSIZE(m_clipName));
	return true;
}

void AnimationEditorWindow::WriteUtf8Buffer(const std::wstring& text, char* buffer, size_t bufferSize)
{
	if (buffer == nullptr || bufferSize == 0)
		return;

	const std::string utf8Text = SString::WstringToUTF8(text);
	strncpy_s(buffer, bufferSize, utf8Text.c_str(), _TRUNCATE);
}

std::wstring AnimationEditorWindow::ReadUtf8Buffer(const char* buffer)
{
	if (buffer == nullptr)
		return L"";

	return SString::UTF8ToWstring(buffer);
}

bool AnimationEditorWindow::HasAnimationRelatedComponent(WitchcraECS* ecs, SceneEntityBase* entity)
{
	if (ecs == nullptr || entity == nullptr)
		return false;

	return
		ecs->GetComponent<AnimatorComponent>(entity) != nullptr ||
		ecs->GetComponent<SkinnedMeshComponent>(entity) != nullptr ||
		ecs->HasSkeletonData(entity) ||
		ecs->GetComponent<SkinningRuntimeComponent>(entity) != nullptr;
}

void AnimationEditorWindow::CollectAnimationOwnerEntities(WitchcraECS* ecs, SceneEntityBase* entity, std::vector<SceneEntityBase*>& outEntities)
{
	if (ecs == nullptr || entity == nullptr)
		return;

	std::unordered_set<SceneEntityBase*> uniqueEntities;
	for (SceneEntityBase* current = entity; current != nullptr; current = ecs->GetParentEntity(current))
	{
		// 预览的权威归属是骨架数据；Animator/Runtime 只是其运行时组件。
		// 旧场景可能尚未创建后两者，不能因此把 Animator 错挂到子网格。
		if (ecs->HasSkeletonData(current) ||
			ecs->GetComponent<AnimatorComponent>(current) != nullptr ||
			ecs->GetComponent<SkinningRuntimeComponent>(current) != nullptr)
			uniqueEntities.insert(current);
	}

	if (uniqueEntities.empty())
		uniqueEntities.insert(entity);

	outEntities.assign(uniqueEntities.begin(), uniqueEntities.end());
}

template<typename TKey>
inline void AnimationEditorWindow::SortKeysByTime(std::vector<TKey>& keys)
{
	std::sort(keys.begin(), keys.end(),
		[](const TKey& lhs, const TKey& rhs)
		{
			return lhs.Time < rhs.Time;
		});
}
