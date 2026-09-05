#include "WAnimationFile.h"

#include <algorithm>
#include <cwctype>
#include <string>

#include "WitchcraftXmlValueHelpers.h"

namespace WAnimationFileDetail
{
	const pugi::char_t* ToXmlInterpolationValue(Witchcraft::Animation::AnimationInterpolationType interpolation)
	{
		switch (interpolation)
		{
		case Witchcraft::Animation::AnimationInterpolationType::Step:
			return PUGIXML_TEXT("Step");
		case Witchcraft::Animation::AnimationInterpolationType::Linear:
			return PUGIXML_TEXT("Linear");
		case Witchcraft::Animation::AnimationInterpolationType::CubicSpline:
			return PUGIXML_TEXT("CubicSpline");
		default:
			return PUGIXML_TEXT("Linear");
		}
	}

	Witchcraft::Animation::AnimationInterpolationType FromXmlInterpolationValue(const pugi::xml_attribute& attribute)
	{
		std::wstring value = WitchcraftXmlFileBase::FromXmlString(attribute.as_string(PUGIXML_TEXT("Linear")));
		std::transform(
			value.begin(),
			value.end(),
			value.begin(),
			[](wchar_t character)
			{
				return static_cast<wchar_t>(std::towlower(character));
			});
		if (value == L"step")
			return Witchcraft::Animation::AnimationInterpolationType::Step;
		if (value == L"cubicspline")
			return Witchcraft::Animation::AnimationInterpolationType::CubicSpline;
		return Witchcraft::Animation::AnimationInterpolationType::Linear;
	}
}

const wchar_t* WAnimationFile::GetRootNodeName() const
{
	return RootNodeName;
}

void WAnimationFile::BuildBody(pugi::xml_node root) const
{
	root.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.Clip.Name).c_str());
	root.append_attribute(PUGIXML_TEXT("duration")).set_value(m_data.Clip.Duration);
	root.append_attribute(PUGIXML_TEXT("ticksPerSecond")).set_value(m_data.Clip.TicksPerSecond);
	root.append_attribute(PUGIXML_TEXT("loop")).set_value(m_data.Clip.Loop);

	pugi::xml_node eventsNode = root.append_child(PUGIXML_TEXT("Events"));
	for (const Witchcraft::Animation::AnimationClipEvent& eventPoint : m_data.Clip.Events)
	{
		pugi::xml_node eventNode = eventsNode.append_child(PUGIXML_TEXT("Event"));
		eventNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(eventPoint.Name).c_str());
		eventNode.append_attribute(PUGIXML_TEXT("time")).set_value(eventPoint.Time);
		eventNode.append_attribute(PUGIXML_TEXT("parameter")).set_value(WitchcraftXmlFileBase::ToXmlString(eventPoint.Parameter).c_str());
		eventNode.append_attribute(PUGIXML_TEXT("scriptCallback")).set_value(WitchcraftXmlFileBase::ToXmlString(eventPoint.ScriptCallbackName).c_str());
	}

	pugi::xml_node tracksNode = root.append_child(PUGIXML_TEXT("Tracks"));
	for (const Witchcraft::Animation::BoneAnimationTrack& track : m_data.Clip.Tracks)
	{
		pugi::xml_node trackNode = tracksNode.append_child(PUGIXML_TEXT("Track"));
		trackNode.append_attribute(PUGIXML_TEXT("boneName")).set_value(WitchcraftXmlFileBase::ToXmlString(track.BoneName).c_str());
		trackNode.append_attribute(PUGIXML_TEXT("boneIndex")).set_value(track.BoneIndex);

		pugi::xml_node translationNode = trackNode.append_child(PUGIXML_TEXT("Translations"));
		for (const Witchcraft::Animation::BoneTranslationKey& key : track.TranslationKeys)
			AppendFloat3KeyAttributes(
				translationNode.append_child(PUGIXML_TEXT("Key")),
				key.Time,
				key.Value,
				key.Interpolation);

		pugi::xml_node rotationNode = trackNode.append_child(PUGIXML_TEXT("Rotations"));
		for (const Witchcraft::Animation::BoneRotationKey& key : track.RotationKeys)
			AppendFloat4KeyAttributes(
				rotationNode.append_child(PUGIXML_TEXT("Key")),
				key.Time,
				key.Value,
				key.Interpolation);

		pugi::xml_node scaleNode = trackNode.append_child(PUGIXML_TEXT("Scales"));
		for (const Witchcraft::Animation::BoneScaleKey& key : track.ScaleKeys)
			AppendFloat3KeyAttributes(
				scaleNode.append_child(PUGIXML_TEXT("Key")),
				key.Time,
				key.Value,
				key.Interpolation);

		pugi::xml_node matrixNode = trackNode.append_child(PUGIXML_TEXT("Matrices"));
		for (const Witchcraft::Animation::BoneMatrixKey& key : track.MatrixKeys)
			AppendMatrixKeyAttributes(
				matrixNode.append_child(PUGIXML_TEXT("Key")),
				key.Time,
				key.Value,
				key.Interpolation);
	}
}

bool WAnimationFile::ReadBody(const pugi::xml_node& root)
{
	WAnimationFileData data;
	data.Clip.Name = WitchcraftXmlFileBase::FromXmlString(root.attribute(PUGIXML_TEXT("name")).as_string());
	data.Clip.Duration = root.attribute(PUGIXML_TEXT("duration")).as_float(0.0f);
	data.Clip.TicksPerSecond = root.attribute(PUGIXML_TEXT("ticksPerSecond")).as_float(0.0f);
	data.Clip.Loop = root.attribute(PUGIXML_TEXT("loop")).as_bool(true);

	const pugi::xml_node eventsNode = root.child(PUGIXML_TEXT("Events"));
	for (pugi::xml_node eventNode = eventsNode.child(PUGIXML_TEXT("Event")); eventNode; eventNode = eventNode.next_sibling(PUGIXML_TEXT("Event")))
	{
		Witchcraft::Animation::AnimationClipEvent eventPoint;
		eventPoint.Name = WitchcraftXmlFileBase::FromXmlString(eventNode.attribute(PUGIXML_TEXT("name")).as_string());
		eventPoint.Time = eventNode.attribute(PUGIXML_TEXT("time")).as_float(0.0f);
		eventPoint.Parameter = WitchcraftXmlFileBase::FromXmlString(eventNode.attribute(PUGIXML_TEXT("parameter")).as_string());
		eventPoint.ScriptCallbackName = WitchcraftXmlFileBase::FromXmlString(eventNode.attribute(PUGIXML_TEXT("scriptCallback")).as_string());
		data.Clip.Events.push_back(std::move(eventPoint));
	}

	const pugi::xml_node tracksNode = root.child(PUGIXML_TEXT("Tracks"));
	for (pugi::xml_node trackNode = tracksNode.child(PUGIXML_TEXT("Track")); trackNode; trackNode = trackNode.next_sibling(PUGIXML_TEXT("Track")))
	{
		Witchcraft::Animation::BoneAnimationTrack track;
		track.BoneName = WitchcraftXmlFileBase::FromXmlString(trackNode.attribute(PUGIXML_TEXT("boneName")).as_string());
		track.BoneIndex = trackNode.attribute(PUGIXML_TEXT("boneIndex")).as_int(-1);

		for (pugi::xml_node keyNode = trackNode.child(PUGIXML_TEXT("Translations")).child(PUGIXML_TEXT("Key")); keyNode; keyNode = keyNode.next_sibling(PUGIXML_TEXT("Key")))
		{
			Witchcraft::Animation::BoneTranslationKey key;
			key.Time = keyNode.attribute(PUGIXML_TEXT("time")).as_float(0.0f);
			key.Value.x = keyNode.attribute(PUGIXML_TEXT("x")).as_float(0.0f);
			key.Value.y = keyNode.attribute(PUGIXML_TEXT("y")).as_float(0.0f);
			key.Value.z = keyNode.attribute(PUGIXML_TEXT("z")).as_float(0.0f);
			key.Interpolation = WAnimationFileDetail::FromXmlInterpolationValue(keyNode.attribute(PUGIXML_TEXT("interpolation")));
			track.TranslationKeys.push_back(key);
		}

		for (pugi::xml_node keyNode = trackNode.child(PUGIXML_TEXT("Rotations")).child(PUGIXML_TEXT("Key")); keyNode; keyNode = keyNode.next_sibling(PUGIXML_TEXT("Key")))
		{
			Witchcraft::Animation::BoneRotationKey key;
			key.Time = keyNode.attribute(PUGIXML_TEXT("time")).as_float(0.0f);
			key.Value.x = keyNode.attribute(PUGIXML_TEXT("x")).as_float(0.0f);
			key.Value.y = keyNode.attribute(PUGIXML_TEXT("y")).as_float(0.0f);
			key.Value.z = keyNode.attribute(PUGIXML_TEXT("z")).as_float(0.0f);
			key.Value.w = keyNode.attribute(PUGIXML_TEXT("w")).as_float(1.0f);
			key.Interpolation = WAnimationFileDetail::FromXmlInterpolationValue(keyNode.attribute(PUGIXML_TEXT("interpolation")));
			track.RotationKeys.push_back(key);
		}

		for (pugi::xml_node keyNode = trackNode.child(PUGIXML_TEXT("Scales")).child(PUGIXML_TEXT("Key")); keyNode; keyNode = keyNode.next_sibling(PUGIXML_TEXT("Key")))
		{
			Witchcraft::Animation::BoneScaleKey key;
			key.Time = keyNode.attribute(PUGIXML_TEXT("time")).as_float(0.0f);
			key.Value.x = keyNode.attribute(PUGIXML_TEXT("x")).as_float(1.0f);
			key.Value.y = keyNode.attribute(PUGIXML_TEXT("y")).as_float(1.0f);
			key.Value.z = keyNode.attribute(PUGIXML_TEXT("z")).as_float(1.0f);
			key.Interpolation = WAnimationFileDetail::FromXmlInterpolationValue(keyNode.attribute(PUGIXML_TEXT("interpolation")));
			track.ScaleKeys.push_back(key);
		}

		for (pugi::xml_node keyNode = trackNode.child(PUGIXML_TEXT("Matrices")).child(PUGIXML_TEXT("Key")); keyNode; keyNode = keyNode.next_sibling(PUGIXML_TEXT("Key")))
		{
			Witchcraft::Animation::BoneMatrixKey key;
			key.Time = keyNode.attribute(PUGIXML_TEXT("time")).as_float(0.0f);
			key.Value = WitchcraftXmlValueHelpers::ReadMatrixAttributes(keyNode, key.Value);
			key.Interpolation = WAnimationFileDetail::FromXmlInterpolationValue(keyNode.attribute(PUGIXML_TEXT("interpolation")));
			track.MatrixKeys.push_back(key);
		}

		data.Clip.Tracks.push_back(std::move(track));
	}

	m_data = std::move(data);
	return true;
}

void WAnimationFile::AppendMatrixKeyAttributes(
	pugi::xml_node node,
	float time,
	const DirectX::XMFLOAT4X4& value,
	Witchcraft::Animation::AnimationInterpolationType interpolation) const
{
	node.append_attribute(PUGIXML_TEXT("time")).set_value(time);
	node.append_attribute(PUGIXML_TEXT("interpolation")).set_value(WAnimationFileDetail::ToXmlInterpolationValue(interpolation));
	WitchcraftXmlValueHelpers::AppendMatrixAttributes(node, value);
}

void WAnimationFile::AppendFloat3KeyAttributes(
	pugi::xml_node node,
	float time,
	const DirectX::XMFLOAT3& value,
	Witchcraft::Animation::AnimationInterpolationType interpolation) const
{
	node.append_attribute(PUGIXML_TEXT("time")).set_value(time);
	node.append_attribute(PUGIXML_TEXT("interpolation")).set_value(WAnimationFileDetail::ToXmlInterpolationValue(interpolation));
	WitchcraftXmlValueHelpers::AppendFloat3Attributes(node, value);
}

void WAnimationFile::AppendFloat4KeyAttributes(
	pugi::xml_node node,
	float time,
	const DirectX::XMFLOAT4& value,
	Witchcraft::Animation::AnimationInterpolationType interpolation) const
{
	node.append_attribute(PUGIXML_TEXT("time")).set_value(time);
	node.append_attribute(PUGIXML_TEXT("interpolation")).set_value(WAnimationFileDetail::ToXmlInterpolationValue(interpolation));
	WitchcraftXmlValueHelpers::AppendFloat4Attributes(node, value);
}

std::wstring WAnimationFile::SerializeToText(const WAnimationFileData& data)
{
	WAnimationFile file;
	file.m_data = data;
	return file.SerializeDocumentToText();
}

bool WAnimationFile::DeserializeFromText(const std::wstring& text, WAnimationFileData* outData)
{
	if (outData == nullptr)
		return false;

	WAnimationFile file;
	if (!file.DeserializeDocumentFromText(text))
		return false;

	*outData = std::move(file.m_data);
	return true;
}

bool WAnimationFile::SaveToFile(const std::filesystem::path& path, const WAnimationFileData& data)
{
	WAnimationFile file;
	file.m_data = data;
	return file.SaveDocumentToFile(path);
}

bool WAnimationFile::LoadFromFile(const std::filesystem::path& path, WAnimationFileData* outData)
{
	if (outData == nullptr)
		return false;

	WAnimationFile file;
	if (!file.LoadDocumentFromFile(path))
		return false;

	*outData = std::move(file.m_data);
	return true;
}
