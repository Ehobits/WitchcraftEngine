#include "WAnimationFile.h"

#include "WitchcraftXmlValueHelpers.h"

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

	pugi::xml_node tracksNode = root.append_child(PUGIXML_TEXT("Tracks"));
	for (const Witchcraft::Animation::BoneAnimationTrack& track : m_data.Clip.Tracks)
	{
		pugi::xml_node trackNode = tracksNode.append_child(PUGIXML_TEXT("Track"));
		trackNode.append_attribute(PUGIXML_TEXT("boneName")).set_value(WitchcraftXmlFileBase::ToXmlString(track.BoneName).c_str());
		trackNode.append_attribute(PUGIXML_TEXT("boneIndex")).set_value(track.BoneIndex);

		pugi::xml_node translationNode = trackNode.append_child(PUGIXML_TEXT("Translations"));
		for (const Witchcraft::Animation::BoneTranslationKey& key : track.TranslationKeys)
			AppendFloat3KeyAttributes(translationNode.append_child(PUGIXML_TEXT("Key")), key.Time, key.Value);

		pugi::xml_node rotationNode = trackNode.append_child(PUGIXML_TEXT("Rotations"));
		for (const Witchcraft::Animation::BoneRotationKey& key : track.RotationKeys)
			AppendFloat4KeyAttributes(rotationNode.append_child(PUGIXML_TEXT("Key")), key.Time, key.Value);

		pugi::xml_node scaleNode = trackNode.append_child(PUGIXML_TEXT("Scales"));
		for (const Witchcraft::Animation::BoneScaleKey& key : track.ScaleKeys)
			AppendFloat3KeyAttributes(scaleNode.append_child(PUGIXML_TEXT("Key")), key.Time, key.Value);

		pugi::xml_node matrixNode = trackNode.append_child(PUGIXML_TEXT("Matrices"));
		for (const Witchcraft::Animation::BoneMatrixKey& key : track.MatrixKeys)
			AppendMatrixKeyAttributes(matrixNode.append_child(PUGIXML_TEXT("Key")), key.Time, key.Value);
	}
}

bool WAnimationFile::ReadBody(const pugi::xml_node& root)
{
	WAnimationFileData data;
	data.Clip.Name = WitchcraftXmlFileBase::FromXmlString(root.attribute(PUGIXML_TEXT("name")).as_string());
	data.Clip.Duration = root.attribute(PUGIXML_TEXT("duration")).as_float(0.0f);
	data.Clip.TicksPerSecond = root.attribute(PUGIXML_TEXT("ticksPerSecond")).as_float(0.0f);
	data.Clip.Loop = root.attribute(PUGIXML_TEXT("loop")).as_bool(true);

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
			track.RotationKeys.push_back(key);
		}

		for (pugi::xml_node keyNode = trackNode.child(PUGIXML_TEXT("Scales")).child(PUGIXML_TEXT("Key")); keyNode; keyNode = keyNode.next_sibling(PUGIXML_TEXT("Key")))
		{
			Witchcraft::Animation::BoneScaleKey key;
			key.Time = keyNode.attribute(PUGIXML_TEXT("time")).as_float(0.0f);
			key.Value.x = keyNode.attribute(PUGIXML_TEXT("x")).as_float(1.0f);
			key.Value.y = keyNode.attribute(PUGIXML_TEXT("y")).as_float(1.0f);
			key.Value.z = keyNode.attribute(PUGIXML_TEXT("z")).as_float(1.0f);
			track.ScaleKeys.push_back(key);
		}

		for (pugi::xml_node keyNode = trackNode.child(PUGIXML_TEXT("Matrices")).child(PUGIXML_TEXT("Key")); keyNode; keyNode = keyNode.next_sibling(PUGIXML_TEXT("Key")))
		{
			Witchcraft::Animation::BoneMatrixKey key;
			key.Time = keyNode.attribute(PUGIXML_TEXT("time")).as_float(0.0f);
			key.Value = WitchcraftXmlValueHelpers::ReadMatrixAttributes(keyNode, key.Value);
			track.MatrixKeys.push_back(key);
		}

		data.Clip.Tracks.push_back(std::move(track));
	}

	m_data = std::move(data);
	return true;
}

void WAnimationFile::AppendMatrixKeyAttributes(pugi::xml_node node, float time, const DirectX::XMFLOAT4X4& value) const
{
	node.append_attribute(PUGIXML_TEXT("time")).set_value(time);
	WitchcraftXmlValueHelpers::AppendMatrixAttributes(node, value);
}

void WAnimationFile::AppendFloat3KeyAttributes(pugi::xml_node node, float time, const DirectX::XMFLOAT3& value) const
{
	node.append_attribute(PUGIXML_TEXT("time")).set_value(time);
	WitchcraftXmlValueHelpers::AppendFloat3Attributes(node, value);
}

void WAnimationFile::AppendFloat4KeyAttributes(pugi::xml_node node, float time, const DirectX::XMFLOAT4& value) const
{
	node.append_attribute(PUGIXML_TEXT("time")).set_value(time);
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
