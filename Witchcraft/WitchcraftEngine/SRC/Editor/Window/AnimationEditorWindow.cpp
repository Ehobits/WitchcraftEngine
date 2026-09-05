#include "AnimationEditorWindow.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <string>
#include <unordered_set>

#include "ECS/Component/AnimatorComponent.h"
#include "ECS/Component/SkinnedMeshComponent.h"
#include "ECS/Component/SkinningRuntimeComponent.h"
#include "ECS/WitchcraECS.h"
#include "../Editor.h"
#include "Engine/EngineUtils.h"
#include "Helpers/Helpers.h"
#include "D3DWindow/D3DWindow.h"
#include "String/SStringUtils.h"
#include "System/Assets.h"
#include "System/Animation/AnimationClipEditing.h"

namespace AnimationEditorWindowDetail
{
	struct TimelineMarkerHit
	{
		float Time = 0.0f;
		const char* ChannelName = nullptr;
		const char* InterpolationName = nullptr;
		Witchcraft::Animation::AnimationKeyChannel Channel = Witchcraft::Animation::AnimationKeyChannel::Translation;
		std::size_t KeyIndex = 0;
		ImU32 Color = 0;
		bool HasHit = false;
	};

	template<typename TKey, typename TValue>
	TValue GetKeyInsertValueAtTime(
		const std::vector<TKey>& keys,
		float time,
		const TValue& fallbackValue)
	{
		if (keys.empty())
			return fallbackValue;

		const TKey* bestKey = nullptr;
		for (const TKey& key : keys)
		{
			if (key.Time <= time && (bestKey == nullptr || key.Time >= bestKey->Time))
				bestKey = &key;
		}

		if (bestKey == nullptr)
			bestKey = &keys.front();

		return bestKey->Value;
	}

	inline float Clamp01(float value)
	{
		return (std::max)(0.0f, (std::min)(1.0f, value));
	}

	inline const char* GetTimelineChannelName(Witchcraft::Animation::AnimationKeyChannel channel)
	{
		switch (channel)
		{
		case Witchcraft::Animation::AnimationKeyChannel::Translation:
			return "T";
		case Witchcraft::Animation::AnimationKeyChannel::Rotation:
			return "R";
		case Witchcraft::Animation::AnimationKeyChannel::Scale:
			return "S";
		case Witchcraft::Animation::AnimationKeyChannel::Matrix:
			return "M";
		default:
			return "?";
		}
	}

	inline const char* GetInterpolationTypeName(Witchcraft::Animation::AnimationInterpolationType interpolation)
	{
		switch (interpolation)
		{
		case Witchcraft::Animation::AnimationInterpolationType::Step:
			return "Step";
		case Witchcraft::Animation::AnimationInterpolationType::Linear:
			return "Linear";
		case Witchcraft::Animation::AnimationInterpolationType::CubicSpline:
			return "CubicSpline";
		default:
			return "Linear";
		}
	}

	inline int GetInterpolationTypeIndex(Witchcraft::Animation::AnimationInterpolationType interpolation)
	{
		switch (interpolation)
		{
		case Witchcraft::Animation::AnimationInterpolationType::Step:
			return 0;
		case Witchcraft::Animation::AnimationInterpolationType::Linear:
			return 1;
		case Witchcraft::Animation::AnimationInterpolationType::CubicSpline:
			return 2;
		default:
			return 1;
		}
	}

	inline Witchcraft::Animation::AnimationInterpolationType GetInterpolationTypeFromIndex(int index)
	{
		switch (index)
		{
		case 0:
			return Witchcraft::Animation::AnimationInterpolationType::Step;
		case 2:
			return Witchcraft::Animation::AnimationInterpolationType::CubicSpline;
		case 1:
		default:
			return Witchcraft::Animation::AnimationInterpolationType::Linear;
		}
	}

	bool DrawInterpolationTypeCombo(Witchcraft::Animation::AnimationInterpolationType& interpolation)
	{
		const char* interpolationItems[] = { "Step", "Linear", "CubicSpline" };
		int currentIndex = GetInterpolationTypeIndex(interpolation);
		if (!ImGui::Combo("插值", &currentIndex, interpolationItems, IM_ARRAYSIZE(interpolationItems)))
			return false;

		interpolation = GetInterpolationTypeFromIndex(currentIndex);
		return true;
	}

	template<typename TKey>
	bool SetKeyTimeNoSort(std::vector<TKey>& keys, std::size_t keyIndex, float time)
	{
		if (keyIndex >= keys.size())
			return false;

		keys[keyIndex].Time = std::isfinite(time) ? (std::max)(0.0f, time) : 0.0f;
		return true;
	}

	bool SetTrackKeyTimeNoSort(
		Witchcraft::Animation::BoneAnimationTrack& track,
		Witchcraft::Animation::AnimationKeyChannel channel,
		std::size_t keyIndex,
		float time)
	{
		switch (channel)
		{
		case Witchcraft::Animation::AnimationKeyChannel::Translation:
			return SetKeyTimeNoSort(track.TranslationKeys, keyIndex, time);
		case Witchcraft::Animation::AnimationKeyChannel::Rotation:
			return SetKeyTimeNoSort(track.RotationKeys, keyIndex, time);
		case Witchcraft::Animation::AnimationKeyChannel::Scale:
			return SetKeyTimeNoSort(track.ScaleKeys, keyIndex, time);
		case Witchcraft::Animation::AnimationKeyChannel::Matrix:
			return SetKeyTimeNoSort(track.MatrixKeys, keyIndex, time);
		default:
			return false;
		}
	}

	template<typename TKey>
	bool GetKeyInterpolation(
		const std::vector<TKey>& keys,
		std::size_t keyIndex,
		Witchcraft::Animation::AnimationInterpolationType* outInterpolation)
	{
		if (outInterpolation == nullptr || keyIndex >= keys.size())
			return false;

		*outInterpolation = keys[keyIndex].Interpolation;
		return true;
	}

	bool GetTrackKeyInterpolation(
		const Witchcraft::Animation::BoneAnimationTrack& track,
		Witchcraft::Animation::AnimationKeyChannel channel,
		std::size_t keyIndex,
		Witchcraft::Animation::AnimationInterpolationType* outInterpolation)
	{
		switch (channel)
		{
		case Witchcraft::Animation::AnimationKeyChannel::Translation:
			return GetKeyInterpolation(track.TranslationKeys, keyIndex, outInterpolation);
		case Witchcraft::Animation::AnimationKeyChannel::Rotation:
			return GetKeyInterpolation(track.RotationKeys, keyIndex, outInterpolation);
		case Witchcraft::Animation::AnimationKeyChannel::Scale:
			return GetKeyInterpolation(track.ScaleKeys, keyIndex, outInterpolation);
		case Witchcraft::Animation::AnimationKeyChannel::Matrix:
			return GetKeyInterpolation(track.MatrixKeys, keyIndex, outInterpolation);
		default:
			return false;
		}
	}

	template<typename TKey>
	bool SetKeyInterpolation(
		std::vector<TKey>& keys,
		std::size_t keyIndex,
		Witchcraft::Animation::AnimationInterpolationType interpolation)
	{
		if (keyIndex >= keys.size() || keys[keyIndex].Interpolation == interpolation)
			return false;

		keys[keyIndex].Interpolation = interpolation;
		return true;
	}

	bool SetTrackKeyInterpolation(
		Witchcraft::Animation::BoneAnimationTrack& track,
		Witchcraft::Animation::AnimationKeyChannel channel,
		std::size_t keyIndex,
		Witchcraft::Animation::AnimationInterpolationType interpolation)
	{
		switch (channel)
		{
		case Witchcraft::Animation::AnimationKeyChannel::Translation:
			return SetKeyInterpolation(track.TranslationKeys, keyIndex, interpolation);
		case Witchcraft::Animation::AnimationKeyChannel::Rotation:
			return SetKeyInterpolation(track.RotationKeys, keyIndex, interpolation);
		case Witchcraft::Animation::AnimationKeyChannel::Scale:
			return SetKeyInterpolation(track.ScaleKeys, keyIndex, interpolation);
		case Witchcraft::Animation::AnimationKeyChannel::Matrix:
			return SetKeyInterpolation(track.MatrixKeys, keyIndex, interpolation);
		default:
			return false;
		}
	}

	template<typename TKey>
	bool HasKey(const std::vector<TKey>& keys, std::size_t keyIndex)
	{
		return keyIndex < keys.size();
	}

	bool HasTrackKey(
		const Witchcraft::Animation::BoneAnimationTrack& track,
		Witchcraft::Animation::AnimationKeyChannel channel,
		std::size_t keyIndex)
	{
		switch (channel)
		{
		case Witchcraft::Animation::AnimationKeyChannel::Translation:
			return HasKey(track.TranslationKeys, keyIndex);
		case Witchcraft::Animation::AnimationKeyChannel::Rotation:
			return HasKey(track.RotationKeys, keyIndex);
		case Witchcraft::Animation::AnimationKeyChannel::Scale:
			return HasKey(track.ScaleKeys, keyIndex);
		case Witchcraft::Animation::AnimationKeyChannel::Matrix:
			return HasKey(track.MatrixKeys, keyIndex);
		default:
			return false;
		}
	}

	bool TrackHasAnyKeys(const Witchcraft::Animation::BoneAnimationTrack& track)
	{
		return
			!track.TranslationKeys.empty() ||
			!track.RotationKeys.empty() ||
			!track.ScaleKeys.empty() ||
			!track.MatrixKeys.empty();
	}

	template<typename TKey>
	bool HasNeighborKeyNearTime(
		const std::vector<TKey>& keys,
		std::size_t keyIndex,
		float time,
		float epsilon)
	{
		for (std::size_t otherKeyIndex = 0; otherKeyIndex < keys.size(); ++otherKeyIndex)
		{
			if (otherKeyIndex == keyIndex)
				continue;
			if (std::abs(keys[otherKeyIndex].Time - time) <= epsilon)
				return true;
		}

		return false;
	}

	template<typename TKey>
	bool SnapKeyTimesInRange(
		std::vector<TKey>& keys,
		float snapStep,
		float rangeStart,
		float rangeEnd)
	{
		if (!std::isfinite(snapStep) || snapStep <= 0.0f)
			return false;

		bool changed = false;
		const float epsilon = Witchcraft::Animation::AnimationClipEditing::DefaultKeyTimeEpsilon;
		for (std::size_t keyIndex = 0; keyIndex < keys.size(); ++keyIndex)
		{
			TKey& key = keys[keyIndex];
			if (key.Time < rangeStart || key.Time > rangeEnd)
				continue;

			const float snappedTime = (std::max)(0.0f, std::round(key.Time / snapStep) * snapStep);
			if (std::abs(key.Time - snappedTime) <= epsilon)
				continue;
			if (HasNeighborKeyNearTime(keys, keyIndex, snappedTime, epsilon))
				continue;

			key.Time = snappedTime;
			changed = true;
		}

		return changed;
	}

	bool SnapTrackKeyTimesInRange(
		Witchcraft::Animation::BoneAnimationTrack& track,
		float snapStep,
		float rangeStart,
		float rangeEnd)
	{
		bool changed = false;
		changed |= SnapKeyTimesInRange(track.TranslationKeys, snapStep, rangeStart, rangeEnd);
		changed |= SnapKeyTimesInRange(track.RotationKeys, snapStep, rangeStart, rangeEnd);
		changed |= SnapKeyTimesInRange(track.ScaleKeys, snapStep, rangeStart, rangeEnd);
		changed |= SnapKeyTimesInRange(track.MatrixKeys, snapStep, rangeStart, rangeEnd);
		if (changed)
			Witchcraft::Animation::AnimationClipEditing::SortTrackKeys(track);
		return changed;
	}

	template<typename TKey>
	void AccumulateFirstKeyTime(const std::vector<TKey>& keys, float* inOutFirstTime)
	{
		if (inOutFirstTime == nullptr)
			return;

		for (const TKey& key : keys)
			*inOutFirstTime = (std::min)(*inOutFirstTime, (std::max)(0.0f, key.Time));
	}

	bool TryGetFirstTrackKeyTime(
		const Witchcraft::Animation::BoneAnimationTrack& track,
		float* outFirstTime)
	{
		if (outFirstTime == nullptr || !TrackHasAnyKeys(track))
			return false;

		float firstTime = FLT_MAX;
		AccumulateFirstKeyTime(track.TranslationKeys, &firstTime);
		AccumulateFirstKeyTime(track.RotationKeys, &firstTime);
		AccumulateFirstKeyTime(track.ScaleKeys, &firstTime);
		AccumulateFirstKeyTime(track.MatrixKeys, &firstTime);
		if (firstTime == FLT_MAX)
			return false;

		*outFirstTime = firstTime;
		return true;
	}

	template<typename TKey>
	bool ShiftKeyTimes(std::vector<TKey>& keys, float deltaTime)
	{
		if (!std::isfinite(deltaTime) || keys.empty())
			return false;

		bool changed = false;
		const float epsilon = Witchcraft::Animation::AnimationClipEditing::DefaultKeyTimeEpsilon;
		for (TKey& key : keys)
		{
			const float shiftedTime = (std::max)(0.0f, key.Time + deltaTime);
			if (std::abs(key.Time - shiftedTime) <= epsilon)
				continue;

			key.Time = shiftedTime;
			changed = true;
		}

		return changed;
	}

	bool ShiftTrackKeyTimes(
		Witchcraft::Animation::BoneAnimationTrack& track,
		float deltaTime)
	{
		bool changed = false;
		changed |= ShiftKeyTimes(track.TranslationKeys, deltaTime);
		changed |= ShiftKeyTimes(track.RotationKeys, deltaTime);
		changed |= ShiftKeyTimes(track.ScaleKeys, deltaTime);
		changed |= ShiftKeyTimes(track.MatrixKeys, deltaTime);
		if (changed)
			Witchcraft::Animation::AnimationClipEditing::SortTrackKeys(track);
		return changed;
	}

	template<typename TKey>
	void DrawTimelineChannel(
		ImDrawList* drawList,
		const std::vector<TKey>& keys,
		float visibleStart,
		float visibleEnd,
		float minX,
		float maxX,
		float centerY,
		ImU32 color,
		const char* channelName,
		Witchcraft::Animation::AnimationKeyChannel channel,
		std::size_t trackIndex,
		bool hasSelection,
		std::size_t selectedTrackIndex,
		Witchcraft::Animation::AnimationKeyChannel selectedChannel,
		std::size_t selectedKeyIndex,
		const ImVec2& mousePos,
		TimelineMarkerHit* outHit)
	{
		if (drawList == nullptr)
			return;

		const float radius = 4.0f;
		const float visibleSpan = (std::max)(visibleEnd - visibleStart, 0.0001f);
		std::size_t keyIndex = 0;
		for (const TKey& key : keys)
		{
			const float alpha = (key.Time - visibleStart) / visibleSpan;
			if (alpha < 0.0f || alpha > 1.0f)
			{
				++keyIndex;
				continue;
			}

			const float x = minX + (maxX - minX) * AnimationEditorWindowDetail::Clamp01(alpha);
			drawList->AddCircleFilled(ImVec2(x, centerY), radius, color, 10);
			drawList->AddCircle(ImVec2(x, centerY), radius + 0.5f, IM_COL32(0, 0, 0, 160), 10, 1.0f);
			if (hasSelection &&
				selectedTrackIndex == trackIndex &&
				selectedChannel == channel &&
				selectedKeyIndex == keyIndex)
			{
				drawList->AddCircle(ImVec2(x, centerY), radius + 3.0f, IM_COL32(255, 255, 255, 230), 12, 1.6f);
			}

			if (!outHit->HasHit &&
				std::abs(mousePos.x - x) <= radius + 3.0f &&
				std::abs(mousePos.y - centerY) <= radius + 4.0f)
			{
				outHit->HasHit = true;
				outHit->Time = key.Time;
				outHit->ChannelName = channelName;
				outHit->InterpolationName = AnimationEditorWindowDetail::GetInterpolationTypeName(key.Interpolation);
				outHit->Channel = channel;
				outHit->KeyIndex = keyIndex;
				outHit->Color = color;
			}
			++keyIndex;
		}
	}
}

void AnimationEditorWindow::Init(WitchcraECS* ecs, Editor* editor)
{
	m_ecs = ecs;
	m_editor = editor;
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
		const AnimatorComponent::AnimationLayer* appliedBaseLayer =
			selectedAnimatorComponent != nullptr ? selectedAnimatorComponent->GetBaseLayer() : nullptr;
		const std::wstring appliedAnimation =
			appliedBaseLayer != nullptr ? appliedBaseLayer->ClipAssetPath : std::wstring();
		ImGui::TextWrapped(
			"该实体当前应用动画：%s",
			SString::WstringToUTF8(appliedAnimation.empty() ? std::wstring(L"无") : appliedAnimation).c_str());
	}
	else
	{
		ImGui::TextDisabled("当前未选中任何实体。");
	}

	SceneEntityBase* currentSkeletonOwnerEntity = nullptr;
	const Witchcraft::Animation::SkeletonData* currentSkeletonData =
		ResolveCurrentSkeletonData(&currentSkeletonOwnerEntity);
	if (currentSkeletonData != nullptr && currentSkeletonOwnerEntity != nullptr)
	{
		ImGui::TextWrapped(
			"当前骨架来源：%s（%u 根骨骼）",
			SString::WstringToUTF8(m_ecs->GetEntityName(currentSkeletonOwnerEntity)).c_str(),
			static_cast<unsigned>(currentSkeletonData->Topology.Bones.size()));
	}
	else
	{
		ImGui::TextDisabled("当前没有可用于初始化轨道的骨架。");
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

			AnimatorComponent::AnimationLayer& baseLayer = animatorComponent->EnsureBaseLayer();
			baseLayer.ClipAssetPath = m_currentPath.wstring();
			baseLayer.Time = m_previewTime;
			baseLayer.Loop = m_data.Clip.Loop;
			baseLayer.Playing = m_previewPlaying;
			baseLayer.Enabled = true;
			baseLayer.Weight = 1.0f;
			baseLayer.MaskRootBoneName.clear();
			baseLayer.BlendMode = AnimatorComponent::AnimationLayerBlendMode::Override;
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
		if (m_currentPath.empty())
			(void)SaveAnimationAs();
		else
			(void)SaveAnimationFile(m_currentPath, false);
	}

	ImGui::SameLine();
	if (ImGui::Button("另存为"))
		(void)SaveAnimationAs();

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

	ImGui::PushItemWidth(300.0f);
	(void)ImGui::InputText("过渡目标", m_previewTransitionClip, IM_ARRAYSIZE(m_previewTransitionClip));
	ImGui::PopItemWidth();
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
					WriteUtf8Buffer(file->full_path, m_previewTransitionClip, IM_ARRAYSIZE(m_previewTransitionClip));
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SameLine();
	ImGui::PushItemWidth(120.0f);
	(void)ImGui::DragFloat("淡入时间", &m_previewTransitionFadeTime, 0.01f, 0.0f, 10.0f, "%.3f");
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::BeginDisabled(m_previewTransitionClip[0] == '\0');
	if (ImGui::Button("预览过渡"))
	{
		SceneEntityBase* transitionEntity = selectedEntity != nullptr ? selectedEntity : m_previewTargetEntity;
		if (transitionEntity != nullptr)
		{
			setPreviewTarget(transitionEntity);
			AnimatorComponent* animatorComponent = syncPreviewToEntities(transitionEntity, true);
			if (animatorComponent != nullptr)
			{
				const std::wstring transitionPath = ReadUtf8Buffer(m_previewTransitionClip);
				AnimatorComponent::AnimationLayer& baseLayer = animatorComponent->EnsureBaseLayer();
				baseLayer.ClipAssetPath = m_currentPath.wstring();
				baseLayer.Time = m_previewTime;
				baseLayer.Loop = m_data.Clip.Loop;
				baseLayer.Playing = true;
				baseLayer.Enabled = true;
				animatorComponent->SetEvaluateWhenPaused(true);
				(void)animatorComponent->CrossFadeLayerTo(0, transitionPath, (std::max)(0.0f, m_previewTransitionFadeTime), 0.0f);
				m_previewPlaying = true;
				previewChanged = true;
				m_statusMessage = L"已开始过渡预览";
				m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
			}
			else
			{
				m_statusMessage = L"无法为过渡预览创建动画控制组件";
				m_statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
			}
		}
		else
		{
			m_statusMessage = L"没有可用于过渡预览的实体";
			m_statusColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
		}
	}
	ImGui::EndDisabled();

	previewChanged |= RenderTimelineOverview(previewDuration);

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
		const AnimatorComponent::AnimationLayer* previewBaseLayer =
			previewAnimatorComponent != nullptr ? previewAnimatorComponent->GetBaseLayer() : nullptr;
		const bool needCreate =
			previewAnimatorComponent == nullptr ||
			previewBaseLayer == nullptr ||
			previewBaseLayer->ClipAssetPath != m_currentPath.wstring();
		(void)syncPreviewToEntities(m_previewTargetEntity, needCreate);
	}

	ImGui::Separator();

	bool changed = false;
	bool editStatusSet = false;
	changed |= ImGui::InputText("动画名称", m_clipName, IM_ARRAYSIZE(m_clipName));
	changed |= ImGui::DragFloat("时长", &m_data.Clip.Duration, 0.01f, 0.0f, FLT_MAX, "%.3f");
	ImGui::SameLine();
	if (ImGui::Button("重算时长"))
	{
		Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
		changed = true;
	}
	changed |= ImGui::DragFloat("TicksPerSecond", &m_data.Clip.TicksPerSecond, 0.01f, 0.0f, FLT_MAX, "%.3f");
	changed |= ImGui::Checkbox("循环", &m_data.Clip.Loop);

	ImGui::Separator();
	ImGui::Text("轨道数量：%u", static_cast<unsigned>(m_data.Clip.Tracks.size()));
	const bool canInitializeTracks =
		currentSkeletonData != nullptr &&
		!currentSkeletonData->Topology.Bones.empty();
	ImGui::SameLine();
	if (!canInitializeTracks)
		ImGui::BeginDisabled();
	if (ImGui::Button("从当前骨架补齐轨道"))
	{
		const std::size_t touchedTrackCount =
			Witchcraft::Animation::AnimationClipEditing::InitializeTracksFromSkeletonTopology(
				m_data.Clip,
				currentSkeletonData->Topology,
				true);
		if (touchedTrackCount > 0)
		{
			Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
			m_isDirty = true;
			m_statusMessage = L"已从当前骨架补齐动画轨道";
			m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
			changed = true;
			editStatusSet = true;
		}
		else
		{
			m_statusMessage = L"当前动画已包含该骨架的全部轨道";
			m_statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
			editStatusSet = true;
		}
	}
	if (!canInitializeTracks)
		ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("将缺失骨骼的 T/R/S key 置于 t=0");

	if (ImGui::CollapsingHeader("轨道详情", ImGuiTreeNodeFlags_None))
	{
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
					if (ImGui::Button("在当前时间插入/覆盖"))
					{
						Witchcraft::Animation::BoneLocalPose currentPose;
						const bool useCurrentPose = TryGetCurrentTrackPose(track, &currentPose);
						const DirectX::XMFLOAT3 insertValue = useCurrentPose
							? currentPose.Translation
							: AnimationEditorWindowDetail::GetKeyInsertValueAtTime(
								track.TranslationKeys,
								m_previewTime,
								DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
						Witchcraft::Animation::AnimationClipEditing::InsertTranslationKey(
							track,
							m_previewTime,
							insertValue);
						Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
						m_statusMessage = useCurrentPose ? L"已从当前姿态插入/覆盖平移关键帧" : L"已复制最近平移关键帧";
						m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
						editStatusSet = true;
						changed = true;
					}
					for (size_t keyIndex = 0; keyIndex < track.TranslationKeys.size();)
					{
						auto& key = track.TranslationKeys[keyIndex];
						ImGui::PushID(static_cast<int>(keyIndex));
						const bool keyOpen = ImGui::TreeNode(("Key##T" + std::to_string(keyIndex)).c_str());
						ImGui::SameLine();
						if (ImGui::SmallButton("删除"))
						{
							Witchcraft::Animation::AnimationClipEditing::DeleteKey(
								track,
								Witchcraft::Animation::AnimationKeyChannel::Translation,
								keyIndex);
							Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
							changed = true;
							if (keyOpen)
								ImGui::TreePop();
							ImGui::PopID();
							continue;
						}
						if (keyOpen)
						{
							changed |= ImGui::DragFloat("时间", &key.Time, 0.01f, 0.0f, FLT_MAX, "%.3f");
							changed |= ImGui::DragFloat3("值", &key.Value.x, 0.01f);
							changed |= AnimationEditorWindowDetail::DrawInterpolationTypeCombo(key.Interpolation);
							ImGui::TreePop();
						}
						ImGui::PopID();
						++keyIndex;
					}
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("旋转关键帧"))
				{
					if (ImGui::Button("在当前时间插入/覆盖"))
					{
						Witchcraft::Animation::BoneLocalPose currentPose;
						const bool useCurrentPose = TryGetCurrentTrackPose(track, &currentPose);
						const DirectX::XMFLOAT4 insertValue = useCurrentPose
							? currentPose.Rotation
							: AnimationEditorWindowDetail::GetKeyInsertValueAtTime(
								track.RotationKeys,
								m_previewTime,
								DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
						Witchcraft::Animation::AnimationClipEditing::InsertRotationKey(
							track,
							m_previewTime,
							insertValue);
						Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
						m_statusMessage = useCurrentPose ? L"已从当前姿态插入/覆盖旋转关键帧" : L"已复制最近旋转关键帧";
						m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
						editStatusSet = true;
						changed = true;
					}
					for (size_t keyIndex = 0; keyIndex < track.RotationKeys.size();)
					{
						auto& key = track.RotationKeys[keyIndex];
						ImGui::PushID(static_cast<int>(keyIndex));
						const bool keyOpen = ImGui::TreeNode(("Key##R" + std::to_string(keyIndex)).c_str());
						ImGui::SameLine();
						if (ImGui::SmallButton("删除"))
						{
							Witchcraft::Animation::AnimationClipEditing::DeleteKey(
								track,
								Witchcraft::Animation::AnimationKeyChannel::Rotation,
								keyIndex);
							Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
							changed = true;
							if (keyOpen)
								ImGui::TreePop();
							ImGui::PopID();
							continue;
						}
						if (keyOpen)
						{
							changed |= ImGui::DragFloat("时间", &key.Time, 0.01f, 0.0f, FLT_MAX, "%.3f");
							changed |= ImGui::DragFloat4("值", &key.Value.x, 0.01f);
							changed |= AnimationEditorWindowDetail::DrawInterpolationTypeCombo(key.Interpolation);
							ImGui::TreePop();
						}
						ImGui::PopID();
						++keyIndex;
					}
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("缩放关键帧"))
				{
					if (ImGui::Button("在当前时间插入/覆盖"))
					{
						Witchcraft::Animation::BoneLocalPose currentPose;
						const bool useCurrentPose = TryGetCurrentTrackPose(track, &currentPose);
						const DirectX::XMFLOAT3 insertValue = useCurrentPose
							? currentPose.Scale
							: AnimationEditorWindowDetail::GetKeyInsertValueAtTime(
								track.ScaleKeys,
								m_previewTime,
								DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
						Witchcraft::Animation::AnimationClipEditing::InsertScaleKey(
							track,
							m_previewTime,
							insertValue);
						Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
						m_statusMessage = useCurrentPose ? L"已从当前姿态插入/覆盖缩放关键帧" : L"已复制最近缩放关键帧";
						m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
						editStatusSet = true;
						changed = true;
					}
					for (size_t keyIndex = 0; keyIndex < track.ScaleKeys.size();)
					{
						auto& key = track.ScaleKeys[keyIndex];
						ImGui::PushID(static_cast<int>(keyIndex));
						const bool keyOpen = ImGui::TreeNode(("Key##S" + std::to_string(keyIndex)).c_str());
						ImGui::SameLine();
						if (ImGui::SmallButton("删除"))
						{
							Witchcraft::Animation::AnimationClipEditing::DeleteKey(
								track,
								Witchcraft::Animation::AnimationKeyChannel::Scale,
								keyIndex);
							Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
							changed = true;
							if (keyOpen)
								ImGui::TreePop();
							ImGui::PopID();
							continue;
						}
						if (keyOpen)
						{
							changed |= ImGui::DragFloat("时间", &key.Time, 0.01f, 0.0f, FLT_MAX, "%.3f");
							changed |= ImGui::DragFloat3("值", &key.Value.x, 0.01f);
							changed |= AnimationEditorWindowDetail::DrawInterpolationTypeCombo(key.Interpolation);
							ImGui::TreePop();
						}
						ImGui::PopID();
						++keyIndex;
					}
					ImGui::TreePop();
				}
			}

			ImGui::PopID();
		}
		}
		ImGui::EndChild();
	}

	if (changed)
	{
		m_isDirty = true;
		if (!editStatusSet)
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
	m_timelineZoom = 1.0f;
	m_timelineSnapStep = 1.0f;
	m_timelineTrackFilter[0] = '\0';
	m_timelineOnlyShowTracksWithKeys = false;
	m_previewTransitionClip[0] = '\0';
	m_previewTransitionFadeTime = 0.2f;
	m_previewTargetEntity = nullptr;
	ResetTimelineInteractionState();
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

std::filesystem::path AnimationEditorWindow::EnsureAnimationFileExtension(const std::filesystem::path& path)
{
	if (path.empty())
		return path;

	std::filesystem::path normalizedPath = path;
	std::wstring extension = normalizedPath.extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
	if (extension != WAnimationFile::Extension)
		normalizedPath.replace_extension(WAnimationFile::Extension);

	return normalizedPath;
}

bool AnimationEditorWindow::SaveAnimationFile(const std::filesystem::path& path, bool switchCurrentPath)
{
	if (!m_hasLoadedAnimation)
	{
		m_statusMessage = L"当前没有可保存的动画";
		m_statusColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
		return false;
	}

	const std::filesystem::path savePath = EnsureAnimationFileExtension(path);
	if (savePath.empty())
	{
		m_statusMessage = L"保存路径无效";
		m_statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		return false;
	}

	m_data.Clip.Name = ReadUtf8Buffer(m_clipName);
	Witchcraft::Animation::AnimationClipEditing::NormalizeClip(m_data.Clip, false);

	if (!WAnimationFile::SaveToFile(savePath, m_data))
	{
		m_statusMessage = L"保存失败";
		m_statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		return false;
	}

	if (switchCurrentPath)
		m_currentPath = savePath;

	WriteUtf8Buffer(m_data.Clip.Name, m_clipName, IM_ARRAYSIZE(m_clipName));
	m_isDirty = false;
	renderAnimationEditor = true;
	m_hasLoadedAnimation = true;
	m_statusMessage = switchCurrentPath ? L"另存为成功" : L"保存成功";
	m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
	return true;
}

bool AnimationEditorWindow::SaveAnimationAs()
{
	std::wstring outputPath;
	const std::wstring initialDir =
		m_currentPath.empty()
		? EngineUtils::GetProjectDirPath()
		: m_currentPath.parent_path().wstring();
	if (!EngineHelpers::TrySaveFileDialog(
		m_editor->GetD3DWindow()->GetHwnd(),
		initialDir.c_str(),
		L"Witchcraft Animation (*.wanim)\0*.wanim\0All Files (*.*)\0*.*\0\0",
		L"动画另存为",
		L"wanim",
		&outputPath))
	{
		return false;
	}

	return SaveAnimationFile(outputPath, true);
}

void AnimationEditorWindow::ResetTimelineInteractionState()
{
	m_timelineKeySelection = TimelineKeySelection{};
	m_timelineKeyDragging = false;
	m_timelineContextTime = 0.0f;
}

bool AnimationEditorWindow::InsertKeyOnTrack(
	Witchcraft::Animation::BoneAnimationTrack& track,
	Witchcraft::Animation::AnimationKeyChannel channel,
	float time)
{
	Witchcraft::Animation::BoneLocalPose currentPose;
	const bool useCurrentPose = TryGetCurrentTrackPose(track, &currentPose);
	switch (channel)
	{
	case Witchcraft::Animation::AnimationKeyChannel::Translation:
	{
		const DirectX::XMFLOAT3 insertValue = useCurrentPose
			? currentPose.Translation
			: AnimationEditorWindowDetail::GetKeyInsertValueAtTime(
				track.TranslationKeys,
				time,
				DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
		Witchcraft::Animation::AnimationClipEditing::InsertTranslationKey(track, time, insertValue);
		m_statusMessage = useCurrentPose ? L"已从当前姿态插入/覆盖平移关键帧" : L"已复制最近平移关键帧";
		break;
	}
	case Witchcraft::Animation::AnimationKeyChannel::Rotation:
	{
		const DirectX::XMFLOAT4 insertValue = useCurrentPose
			? currentPose.Rotation
			: AnimationEditorWindowDetail::GetKeyInsertValueAtTime(
				track.RotationKeys,
				time,
				DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
		Witchcraft::Animation::AnimationClipEditing::InsertRotationKey(track, time, insertValue);
		m_statusMessage = useCurrentPose ? L"已从当前姿态插入/覆盖旋转关键帧" : L"已复制最近旋转关键帧";
		break;
	}
	case Witchcraft::Animation::AnimationKeyChannel::Scale:
	{
		const DirectX::XMFLOAT3 insertValue = useCurrentPose
			? currentPose.Scale
			: AnimationEditorWindowDetail::GetKeyInsertValueAtTime(
				track.ScaleKeys,
				time,
				DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
		Witchcraft::Animation::AnimationClipEditing::InsertScaleKey(track, time, insertValue);
		m_statusMessage = useCurrentPose ? L"已从当前姿态插入/覆盖缩放关键帧" : L"已复制最近缩放关键帧";
		break;
	}
	default:
		return false;
	}

	Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
	m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
	m_isDirty = true;
	return true;
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

SceneEntityBase* AnimationEditorWindow::ResolveSkeletonOwnerEntity(SceneEntityBase* entity) const
{
	if (m_ecs == nullptr || entity == nullptr || !m_ecs->HasEntity(entity))
		return nullptr;

	SceneEntityBase* ownerEntity = entity;
	if (m_ecs->IsSkeletonHierarchyEntity(entity))
	{
		SceneEntityBase* boundOwnerEntity = nullptr;
		std::int32_t boundBoneIndex = -1;
		if (m_ecs->TryGetSkeletonHierarchyBinding(entity, &boundOwnerEntity, &boundBoneIndex) &&
			boundOwnerEntity != nullptr &&
			m_ecs->HasEntity(boundOwnerEntity))
		{
			ownerEntity = boundOwnerEntity;
		}
	}

	while (ownerEntity != nullptr && !m_ecs->HasSkeletonData(ownerEntity))
		ownerEntity = m_ecs->GetParentEntity(ownerEntity);

	return ownerEntity != nullptr && m_ecs->HasSkeletonData(ownerEntity)
		? ownerEntity
		: nullptr;
}

const Witchcraft::Animation::SkeletonData* AnimationEditorWindow::ResolveCurrentSkeletonData(SceneEntityBase** outOwnerEntity) const
{
	if (outOwnerEntity != nullptr)
		*outOwnerEntity = nullptr;

	if (m_ecs == nullptr)
		return nullptr;

	SceneEntityBase* sourceEntity = nullptr;
	if (m_previewTargetEntity != nullptr && m_ecs->HasEntity(m_previewTargetEntity))
		sourceEntity = m_previewTargetEntity;
	else if (m_ecs->HasSelectedEntity())
		sourceEntity = m_ecs->GetSelectedEntity();

	if (sourceEntity == nullptr)
		return nullptr;

	SceneEntityBase* ownerEntity = ResolveSkeletonOwnerEntity(sourceEntity);
	if (outOwnerEntity != nullptr)
		*outOwnerEntity = ownerEntity;

	return ownerEntity != nullptr ? m_ecs->GetSkeletonData(ownerEntity) : nullptr;
}

bool AnimationEditorWindow::TryGetCurrentTrackPose(
	const Witchcraft::Animation::BoneAnimationTrack& track,
	Witchcraft::Animation::BoneLocalPose* outPose) const
{
	if (m_ecs == nullptr || outPose == nullptr)
		return false;

	SceneEntityBase* sourceEntity = nullptr;
	if (m_previewTargetEntity != nullptr && m_ecs->HasEntity(m_previewTargetEntity))
		sourceEntity = m_previewTargetEntity;
	else if (m_ecs->HasSelectedEntity())
		sourceEntity = m_ecs->GetSelectedEntity();

	if (sourceEntity == nullptr || !m_ecs->HasEntity(sourceEntity))
		return false;

	std::int32_t selectedHierarchyBoneIndex = -1;
	if (m_ecs->IsSkeletonHierarchyEntity(sourceEntity))
	{
		SceneEntityBase* boundOwnerEntity = nullptr;
		std::int32_t boundBoneIndex = -1;
		if (m_ecs->TryGetSkeletonHierarchyBinding(sourceEntity, &boundOwnerEntity, &boundBoneIndex) &&
			boundOwnerEntity != nullptr &&
			m_ecs->HasEntity(boundOwnerEntity))
		{
			selectedHierarchyBoneIndex = boundBoneIndex;
		}
	}

	SceneEntityBase* ownerEntity = ResolveSkeletonOwnerEntity(sourceEntity);
	if (ownerEntity == nullptr)
		return false;

	const Witchcraft::Animation::SkeletonData* skeletonData = m_ecs->GetSkeletonData(ownerEntity);
	if (skeletonData == nullptr)
		return false;

	std::int32_t boneIndex = track.BoneIndex;
	if (boneIndex < 0 && !track.BoneName.empty())
	{
		const auto boneIt = skeletonData->Topology.BoneNameToIndex.find(track.BoneName);
		if (boneIt != skeletonData->Topology.BoneNameToIndex.end())
			boneIndex = static_cast<std::int32_t>(boneIt->second);
	}

	if (boneIndex < 0 && selectedHierarchyBoneIndex >= 0)
		boneIndex = selectedHierarchyBoneIndex;

	if (boneIndex < 0 || boneIndex >= static_cast<std::int32_t>(skeletonData->LocalPose.size()))
		return false;

	*outPose = skeletonData->LocalPose[static_cast<std::size_t>(boneIndex)];
	return true;
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

bool AnimationEditorWindow::RenderTimelineOverview(float previewDuration)
{
	using AnimationKeyChannel = Witchcraft::Animation::AnimationKeyChannel;

	const float duration = previewDuration > 0.0f ? previewDuration : 1.0f;
	bool previewChanged = false;

	if (!ImGui::CollapsingHeader("时间轴总览", ImGuiTreeNodeFlags_DefaultOpen))
		return false;

	if (m_data.Clip.Tracks.empty())
	{
		ImGui::TextDisabled("暂无轨道。");
		return false;
	}

	if (m_timelineKeySelection.Valid &&
		m_timelineKeySelection.TrackIndex >= m_data.Clip.Tracks.size())
	{
		ResetTimelineInteractionState();
	}

	m_timelineZoom = std::clamp(m_timelineZoom, 1.0f, 16.0f);
	ImGui::PushItemWidth(220.0f);
	if (ImGui::SliderFloat("缩放", &m_timelineZoom, 1.0f, 16.0f, "%.1fx"))
	{
		m_timelineZoom = std::clamp(m_timelineZoom, 1.0f, 16.0f);
	}
	ImGui::PopItemWidth();
	ImGui::SameLine();
	if (ImGui::SmallButton("1x"))
	{
		m_timelineZoom = 1.0f;
	}

	ImGui::SameLine();
	ImGui::PushItemWidth(180.0f);
	(void)ImGui::InputTextWithHint("##TimelineTrackFilter", "过滤轨道...", m_timelineTrackFilter, IM_ARRAYSIZE(m_timelineTrackFilter));
	ImGui::PopItemWidth();
	ImGui::SameLine();
	(void)ImGui::Checkbox("只看有 key", &m_timelineOnlyShowTracksWithKeys);
	ImGui::SameLine();
	ImGui::PushItemWidth(96.0f);
	(void)ImGui::DragFloat("##TimelineSnapStep", &m_timelineSnapStep, 0.01f, 0.001f, 1024.0f, "吸附 %.3f");
	ImGui::PopItemWidth();

	const float visibleSpan = (std::max)(duration / (std::max)(m_timelineZoom, 1.0f), 0.0001f);
	const float visibleStart = duration > visibleSpan
		? std::clamp(m_previewTime - visibleSpan * 0.5f, 0.0f, duration - visibleSpan)
		: 0.0f;
	const float visibleEnd = visibleStart + visibleSpan;

	std::vector<std::size_t> visibleTrackIndices;
	visibleTrackIndices.reserve(m_data.Clip.Tracks.size());
	for (std::size_t trackIndex = 0; trackIndex < m_data.Clip.Tracks.size(); ++trackIndex)
	{
		const auto& track = m_data.Clip.Tracks[trackIndex];
		if (m_timelineOnlyShowTracksWithKeys && !AnimationEditorWindowDetail::TrackHasAnyKeys(track))
			continue;

		if (m_timelineTrackFilter[0] != '\0')
		{
			const std::string filterText(m_timelineTrackFilter);
			const std::string trackNameUtf8 = SString::WstringToUTF8(track.BoneName);
			const std::string trackIndexText = std::to_string(trackIndex);
			if (trackNameUtf8.find(filterText) == std::string::npos &&
				trackIndexText.find(filterText) == std::string::npos)
			{
				continue;
			}
		}

		visibleTrackIndices.push_back(trackIndex);
	}

	ImGui::Separator();
	ImGui::BeginDisabled(visibleTrackIndices.empty());
	if (ImGui::Button("吸附可见 key 到步长"))
	{
		const float snapStep = (std::max)(0.001f, m_timelineSnapStep);
		bool changedTracks = false;
		for (std::size_t trackIndex : visibleTrackIndices)
		{
			auto& track = m_data.Clip.Tracks[trackIndex];
			changedTracks |= AnimationEditorWindowDetail::SnapTrackKeyTimesInRange(track, snapStep, visibleStart, visibleEnd);
		}
		if (changedTracks)
		{
			Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
			m_previewTime = std::clamp(m_previewTime, 0.0f, (std::max)(m_data.Clip.Duration, 0.0f));
			ResetTimelineInteractionState();
			m_isDirty = true;
			m_statusMessage = L"已吸附可见关键帧";
			m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
			previewChanged = true;
		}
		else
		{
			m_statusMessage = L"没有可吸附的关键帧";
			m_statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("对齐可见轨道到当前时间"))
	{
		bool changedTracks = false;
		for (std::size_t trackIndex : visibleTrackIndices)
		{
			auto& track = m_data.Clip.Tracks[trackIndex];
			float firstTime = 0.0f;
			if (!AnimationEditorWindowDetail::TryGetFirstTrackKeyTime(track, &firstTime))
				continue;

			const float desiredTime = (std::max)(0.0f, m_previewTime);
			const float deltaTime = desiredTime - firstTime;
			changedTracks |= AnimationEditorWindowDetail::ShiftTrackKeyTimes(track, deltaTime);
		}
		if (changedTracks)
		{
			Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
			m_previewTime = std::clamp(m_previewTime, 0.0f, (std::max)(m_data.Clip.Duration, 0.0f));
			ResetTimelineInteractionState();
			m_isDirty = true;
			m_statusMessage = L"已将可见轨道起点对齐到当前时间";
			m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
			previewChanged = true;
		}
		else
		{
			m_statusMessage = L"没有可对齐的轨道";
			m_statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
		}
	}
	ImGui::EndDisabled();

	const float childHeight = (std::min)(
		420.0f,
		(std::max)(140.0f, 48.0f + static_cast<float>(visibleTrackIndices.size()) * 46.0f));
	if (ImGui::BeginChild("AnimationTimelineOverview", ImVec2(0.0f, childHeight), ImGuiChildFlags_None))
	{
		const float labelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.24f, 140.0f, 240.0f);
		const float rulerHeight = 28.0f;
		const float rowHeight = 44.0f;
		const ImVec4 rulerBgColor(0.14f, 0.14f, 0.16f, 1.0f);
		const ImVec4 rowBgEven(0.12f, 0.12f, 0.13f, 1.0f);
		const ImVec4 rowBgOdd(0.10f, 0.10f, 0.11f, 1.0f);
		const ImVec4 laneColor(0.28f, 0.28f, 0.30f, 1.0f);
		const ImU32 textColor = ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
		const ImU32 minorTextColor = ImGui::GetColorU32(ImVec4(0.68f, 0.68f, 0.72f, 1.0f));
		const ImU32 rulerColor = ImGui::GetColorU32(ImVec4(0.38f, 0.42f, 0.48f, 1.0f));
		const ImU32 playheadColor = IM_COL32(255, 208, 64, 220);
		const ImU32 translationColor = IM_COL32(84, 206, 118, 255);
		const ImU32 rotationColor = IM_COL32(90, 156, 255, 255);
		const ImU32 scaleColor = IM_COL32(255, 176, 84, 255);
		const ImU32 matrixColor = IM_COL32(210, 96, 255, 255);
		const ImVec2 mousePos = ImGui::GetIO().MousePos;

		auto drawTimeScrubArea = [&](const ImVec2& itemMin, const ImVec2& itemMax, bool allowScrub)
		{
			const float laneMinX = itemMin.x + labelWidth + 8.0f;
			const float laneMaxX = itemMax.x - 10.0f;
			if (laneMaxX <= laneMinX)
				return;

			if (allowScrub && (ImGui::IsItemHovered() || ImGui::IsItemActive()) && ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				const float clampedX = std::clamp(mousePos.x, laneMinX, laneMaxX);
				const float alpha = (clampedX - laneMinX) / (laneMaxX - laneMinX);
				const float newTime = visibleStart + alpha * visibleSpan;
				if (std::isfinite(newTime))
				{
					m_previewTime = newTime;
					previewChanged = true;
				}
			}
		};

		auto drawTimelineKeyLine = [&](const ImVec2& itemMin, const ImVec2& itemMax, float currentTime, bool oddRow)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImU32 bgColor = ImGui::GetColorU32(oddRow ? rowBgOdd : rowBgEven);
			drawList->AddRectFilled(itemMin, itemMax, bgColor);

			const float laneMinX = itemMin.x + labelWidth + 8.0f;
			const float laneMaxX = itemMax.x - 10.0f;
			if (laneMaxX <= laneMinX)
				return;

			const float laneCenterY = itemMin.y + rowHeight * 0.5f;
			const float playheadAlpha = (currentTime - visibleStart) / visibleSpan;
			const float playheadX = laneMinX + (laneMaxX - laneMinX) * AnimationEditorWindowDetail::Clamp01(playheadAlpha);

			drawList->AddLine(
				ImVec2(laneMinX, laneCenterY),
				ImVec2(laneMaxX, laneCenterY),
				ImGui::GetColorU32(laneColor),
				1.0f);
			drawList->AddLine(
				ImVec2(playheadX, itemMin.y + 4.0f),
				ImVec2(playheadX, itemMax.y - 4.0f),
				playheadColor,
				1.4f);
			drawList->AddTriangleFilled(
				ImVec2(playheadX - 5.0f, itemMin.y + 2.0f),
				ImVec2(playheadX + 5.0f, itemMin.y + 2.0f),
				ImVec2(playheadX, itemMin.y + 10.0f),
				playheadColor);
		};

		ImGui::PushID("TimelineRuler");
		const ImVec2 rulerSize(ImGui::GetContentRegionAvail().x, rulerHeight);
		ImGui::InvisibleButton("##TimelineRuler", rulerSize);
		const ImVec2 rulerMin = ImGui::GetItemRectMin();
		const ImVec2 rulerMax = ImGui::GetItemRectMax();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(rulerMin, rulerMax, ImGui::GetColorU32(rulerBgColor));

		const float rulerLaneMinX = rulerMin.x + labelWidth + 8.0f;
		const float rulerLaneMaxX = rulerMax.x - 10.0f;
		drawList->AddText(ImVec2(rulerMin.x + 8.0f, rulerMin.y + 6.0f), textColor, "时间");
		if (rulerLaneMaxX > rulerLaneMinX)
		{
			drawList->AddLine(
				ImVec2(rulerLaneMinX, rulerMin.y + rulerHeight - 6.0f),
				ImVec2(rulerLaneMaxX, rulerMin.y + rulerHeight - 6.0f),
				rulerColor,
				1.0f);

			for (int tickIndex = 0; tickIndex <= 4; ++tickIndex)
			{
				const float tickAlpha = static_cast<float>(tickIndex) / 4.0f;
				const float tickX = rulerLaneMinX + (rulerLaneMaxX - rulerLaneMinX) * tickAlpha;
				drawList->AddLine(
					ImVec2(tickX, rulerMin.y + 4.0f),
					ImVec2(tickX, rulerMin.y + rulerHeight - 6.0f),
					ImGui::GetColorU32(ImVec4(0.52f, 0.52f, 0.56f, 1.0f)),
					1.0f);
				char tickLabel[32] = {};
				const float tickTime = visibleStart + visibleSpan * tickAlpha;
				snprintf(tickLabel, sizeof(tickLabel), "%.1f", tickTime);
				drawList->AddText(ImVec2(tickX + 2.0f, rulerMin.y + 4.0f), minorTextColor, tickLabel);
			}
		}

		drawTimeScrubArea(rulerMin, rulerMax, true);
		ImGui::PopID();

		if (visibleTrackIndices.empty())
		{
			ImGui::TextDisabled("没有符合筛选条件的轨道。");
		}

		for (std::size_t visibleIndex = 0; visibleIndex < visibleTrackIndices.size(); ++visibleIndex)
		{
			const std::size_t trackIndex = visibleTrackIndices[visibleIndex];
			auto& track = m_data.Clip.Tracks[trackIndex];
			ImGui::PushID(static_cast<int>(trackIndex));
			const ImVec2 rowSize(ImGui::GetContentRegionAvail().x, rowHeight);
			ImGui::InvisibleButton("##TimelineRow", rowSize);
			const ImVec2 rowMin = ImGui::GetItemRectMin();
			const ImVec2 rowMax = ImGui::GetItemRectMax();
			drawTimelineKeyLine(rowMin, rowMax, m_previewTime, (trackIndex & 1) != 0);

			const std::wstring trackName = track.BoneName.empty()
				? (L"Track " + std::to_wstring(trackIndex))
				: track.BoneName;
			const std::string trackNameUtf8 = SString::WstringToUTF8(trackName);
			drawList->PushClipRect(
				ImVec2(rowMin.x + 8.0f, rowMin.y + 4.0f),
				ImVec2(rowMin.x + labelWidth - 6.0f, rowMax.y - 4.0f),
				true);
			drawList->AddText(ImVec2(rowMin.x + 8.0f, rowMin.y + 12.0f), textColor, trackNameUtf8.c_str());
			drawList->PopClipRect();

			const float laneMinX = rowMin.x + labelWidth + 8.0f;
			const float laneMaxX = rowMax.x - 10.0f;
			const float channelOffsets[4] = { -12.0f, -4.0f, 4.0f, 12.0f };
			const char* channelNames[4] = { "T", "R", "S", "M" };
			const ImU32 channelColors[4] = { translationColor, rotationColor, scaleColor, matrixColor };
			const float laneCenterY = rowMin.y + rowHeight * 0.5f;
			if (laneMaxX <= laneMinX)
			{
				ImGui::PopID();
				continue;
			}

			for (int channelIndex = 0; channelIndex < 4; ++channelIndex)
			{
				const float channelY = laneCenterY + channelOffsets[channelIndex];
				drawList->AddLine(
					ImVec2(laneMinX, channelY),
					ImVec2(laneMaxX, channelY),
					IM_COL32(72, 72, 78, 110),
					1.0f);
				drawList->AddText(
					ImVec2(laneMinX - 16.0f, channelY - 7.0f),
					channelColors[channelIndex],
					channelNames[channelIndex]);
			}

			AnimationEditorWindowDetail::TimelineMarkerHit markerHit{};
			AnimationEditorWindowDetail::DrawTimelineChannel(
				drawList,
				track.TranslationKeys,
				visibleStart,
				visibleEnd,
				laneMinX,
				laneMaxX,
				laneCenterY + channelOffsets[0],
				translationColor,
				"T",
				AnimationKeyChannel::Translation,
				trackIndex,
				m_timelineKeySelection.Valid,
				m_timelineKeySelection.TrackIndex,
				m_timelineKeySelection.Channel,
				m_timelineKeySelection.KeyIndex,
				mousePos,
				&markerHit);
			AnimationEditorWindowDetail::DrawTimelineChannel(
				drawList,
				track.RotationKeys,
				visibleStart,
				visibleEnd,
				laneMinX,
				laneMaxX,
				laneCenterY + channelOffsets[1],
				rotationColor,
				"R",
				AnimationKeyChannel::Rotation,
				trackIndex,
				m_timelineKeySelection.Valid,
				m_timelineKeySelection.TrackIndex,
				m_timelineKeySelection.Channel,
				m_timelineKeySelection.KeyIndex,
				mousePos,
				&markerHit);
			AnimationEditorWindowDetail::DrawTimelineChannel(
				drawList,
				track.ScaleKeys,
				visibleStart,
				visibleEnd,
				laneMinX,
				laneMaxX,
				laneCenterY + channelOffsets[2],
				scaleColor,
				"S",
				AnimationKeyChannel::Scale,
				trackIndex,
				m_timelineKeySelection.Valid,
				m_timelineKeySelection.TrackIndex,
				m_timelineKeySelection.Channel,
				m_timelineKeySelection.KeyIndex,
				mousePos,
				&markerHit);
			AnimationEditorWindowDetail::DrawTimelineChannel(
				drawList,
				track.MatrixKeys,
				visibleStart,
				visibleEnd,
				laneMinX,
				laneMaxX,
				laneCenterY + channelOffsets[3],
				matrixColor,
				"M",
				AnimationKeyChannel::Matrix,
				trackIndex,
				m_timelineKeySelection.Valid,
				m_timelineKeySelection.TrackIndex,
				m_timelineKeySelection.Channel,
				m_timelineKeySelection.KeyIndex,
				mousePos,
				&markerHit);

			auto timelineTimeFromMouseX = [&]() -> float
			{
				const float clampedX = std::clamp(mousePos.x, laneMinX, laneMaxX);
				const float alpha = (clampedX - laneMinX) / (laneMaxX - laneMinX);
				return std::clamp(visibleStart + alpha * visibleSpan, visibleStart, visibleEnd);
			};

			const bool rowHovered = ImGui::IsItemHovered();
			const bool rowActive = ImGui::IsItemActive();
			bool selectedKeyIsOnThisTrack =
				m_timelineKeySelection.Valid &&
				m_timelineKeySelection.TrackIndex == trackIndex &&
				AnimationEditorWindowDetail::HasTrackKey(
					track,
					m_timelineKeySelection.Channel,
					m_timelineKeySelection.KeyIndex);

			if (markerHit.HasHit && rowHovered)
			{
				ImGui::SetTooltip(
					"%s key: %.3f\n插值: %s",
					markerHit.ChannelName != nullptr ? markerHit.ChannelName : "Key",
					markerHit.Time,
					markerHit.InterpolationName != nullptr ? markerHit.InterpolationName : "Linear");

				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					m_timelineKeySelection.Valid = true;
					m_timelineKeySelection.TrackIndex = trackIndex;
					m_timelineKeySelection.Channel = markerHit.Channel;
					m_timelineKeySelection.KeyIndex = markerHit.KeyIndex;
					m_timelineKeyDragging = true;
					selectedKeyIsOnThisTrack = true;
					m_previewTime = std::clamp(markerHit.Time, 0.0f, duration);
					previewChanged = true;
				}

				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					m_timelineKeySelection.Valid = true;
					m_timelineKeySelection.TrackIndex = trackIndex;
					m_timelineKeySelection.Channel = markerHit.Channel;
					m_timelineKeySelection.KeyIndex = markerHit.KeyIndex;
					m_timelineKeyDragging = false;
					selectedKeyIsOnThisTrack = true;
					m_timelineContextTime = std::clamp(markerHit.Time, 0.0f, duration);
					ImGui::OpenPopup("TimelineKeyContext");
				}
			}
			else if (rowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				m_timelineKeySelection.Valid = false;
				m_timelineKeyDragging = false;
				m_timelineContextTime = timelineTimeFromMouseX();
				m_previewTime = m_timelineContextTime;
				previewChanged = true;
				ImGui::OpenPopup("TimelineRowContext");
			}

			if (m_timelineKeyDragging && selectedKeyIsOnThisTrack)
			{
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					const float newTime = timelineTimeFromMouseX();
					if (AnimationEditorWindowDetail::SetTrackKeyTimeNoSort(
						track,
						m_timelineKeySelection.Channel,
						m_timelineKeySelection.KeyIndex,
						newTime))
					{
						m_previewTime = newTime;
						m_isDirty = true;
						previewChanged = true;
					}
				}
				else
				{
					Witchcraft::Animation::AnimationClipEditing::SortTrackKeys(track);
					Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
					const float updatedDuration = (std::max)(m_data.Clip.Duration, 0.0f);
					m_previewTime = updatedDuration > 0.0f
						? std::clamp(m_previewTime, 0.0f, updatedDuration)
						: 0.0f;
					m_timelineKeyDragging = false;
					m_timelineKeySelection.Valid = false;
					m_statusMessage = L"已移动时间轴关键帧";
					m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
					previewChanged = true;
				}
			}
			else if ((rowHovered || rowActive) && ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				const float newTime = timelineTimeFromMouseX();
				if (std::isfinite(newTime))
				{
					m_previewTime = newTime;
					previewChanged = true;
				}
			}

			if (ImGui::BeginPopup("TimelineKeyContext"))
			{
				const bool hasSelectedKey =
					m_timelineKeySelection.Valid &&
					m_timelineKeySelection.TrackIndex == trackIndex &&
					AnimationEditorWindowDetail::HasTrackKey(
						track,
						m_timelineKeySelection.Channel,
						m_timelineKeySelection.KeyIndex);
				ImGui::TextDisabled("关键帧：%s", hasSelectedKey
					? AnimationEditorWindowDetail::GetTimelineChannelName(m_timelineKeySelection.Channel)
					: "?");
				if (ImGui::MenuItem("跳到关键帧", nullptr, false, hasSelectedKey))
				{
					m_previewTime = std::clamp(m_timelineContextTime, 0.0f, duration);
					previewChanged = true;
				}
				Witchcraft::Animation::AnimationInterpolationType selectedInterpolation =
					Witchcraft::Animation::AnimationInterpolationType::Linear;
				const bool hasSelectedInterpolation =
					hasSelectedKey &&
					AnimationEditorWindowDetail::GetTrackKeyInterpolation(
						track,
						m_timelineKeySelection.Channel,
						m_timelineKeySelection.KeyIndex,
						&selectedInterpolation);
				if (ImGui::BeginMenu("插值", hasSelectedInterpolation))
				{
					auto drawInterpolationMenuItem =
						[&](Witchcraft::Animation::AnimationInterpolationType interpolation)
					{
						const bool isSelected = selectedInterpolation == interpolation;
						if (ImGui::MenuItem(
							AnimationEditorWindowDetail::GetInterpolationTypeName(interpolation),
							nullptr,
							isSelected))
						{
							if (AnimationEditorWindowDetail::SetTrackKeyInterpolation(
								track,
								m_timelineKeySelection.Channel,
								m_timelineKeySelection.KeyIndex,
								interpolation))
							{
								m_isDirty = true;
								m_statusMessage = L"已修改关键帧插值类型";
								m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
								previewChanged = true;
							}
						}
					};
					drawInterpolationMenuItem(Witchcraft::Animation::AnimationInterpolationType::Step);
					drawInterpolationMenuItem(Witchcraft::Animation::AnimationInterpolationType::Linear);
					drawInterpolationMenuItem(Witchcraft::Animation::AnimationInterpolationType::CubicSpline);
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("删除关键帧", nullptr, false, hasSelectedKey))
				{
					if (Witchcraft::Animation::AnimationClipEditing::DeleteKey(
						track,
						m_timelineKeySelection.Channel,
						m_timelineKeySelection.KeyIndex))
					{
						Witchcraft::Animation::AnimationClipEditing::RecalculateDuration(m_data.Clip);
						const float updatedDuration = (std::max)(m_data.Clip.Duration, 0.0f);
						m_previewTime = updatedDuration > 0.0f
							? std::clamp(m_previewTime, 0.0f, updatedDuration)
							: 0.0f;
						m_timelineKeySelection.Valid = false;
						m_timelineKeyDragging = false;
						m_isDirty = true;
						m_statusMessage = L"已从时间轴删除关键帧";
						m_statusColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
						previewChanged = true;
					}
				}
				ImGui::EndPopup();
			}

			if (ImGui::BeginPopup("TimelineRowContext"))
			{
				ImGui::TextDisabled("在 %.3f 插入", m_timelineContextTime);
				if (ImGui::MenuItem("插入平移 key"))
				{
					previewChanged |= InsertKeyOnTrack(track, AnimationKeyChannel::Translation, m_timelineContextTime);
				}
				if (ImGui::MenuItem("插入旋转 key"))
				{
					previewChanged |= InsertKeyOnTrack(track, AnimationKeyChannel::Rotation, m_timelineContextTime);
				}
				if (ImGui::MenuItem("插入缩放 key"))
				{
					previewChanged |= InsertKeyOnTrack(track, AnimationKeyChannel::Scale, m_timelineContextTime);
				}
				if (ImGui::MenuItem("插入全部 TRS"))
				{
					bool inserted = false;
					inserted |= InsertKeyOnTrack(track, AnimationKeyChannel::Translation, m_timelineContextTime);
					inserted |= InsertKeyOnTrack(track, AnimationKeyChannel::Rotation, m_timelineContextTime);
					inserted |= InsertKeyOnTrack(track, AnimationKeyChannel::Scale, m_timelineContextTime);
					previewChanged |= inserted;
				}
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	return previewChanged;
}
