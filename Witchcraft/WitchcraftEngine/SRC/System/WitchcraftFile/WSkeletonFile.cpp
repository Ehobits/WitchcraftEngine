#include "WSkeletonFile.h"

#include "WitchcraftXmlValueHelpers.h"

const wchar_t* WSkeletonFile::GetRootNodeName() const
{
	return RootNodeName;
}

void WSkeletonFile::BuildBody(pugi::xml_node root) const
{
	root.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.Name).c_str());
	root.append_attribute(PUGIXML_TEXT("rootBoneIndex")).set_value(m_data.Topology.RootBoneIndex);

	pugi::xml_node bonesNode = root.append_child(PUGIXML_TEXT("Bones"));
	for (const Witchcraft::Animation::SkeletonBone& bone : m_data.Topology.Bones)
	{
		pugi::xml_node boneNode = bonesNode.append_child(PUGIXML_TEXT("Bone"));
		boneNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(bone.Name).c_str());
		boneNode.append_attribute(PUGIXML_TEXT("parentIndex")).set_value(bone.ParentIndex);

		pugi::xml_node bindLocalNode = boneNode.append_child(PUGIXML_TEXT("BindLocalPose"));
		WitchcraftXmlValueHelpers::AppendFloat3Attributes(bindLocalNode.append_child(PUGIXML_TEXT("Translation")), bone.BindLocalPose.Translation);
		WitchcraftXmlValueHelpers::AppendFloat4Attributes(bindLocalNode.append_child(PUGIXML_TEXT("Rotation")), bone.BindLocalPose.Rotation);
		WitchcraftXmlValueHelpers::AppendFloat3Attributes(bindLocalNode.append_child(PUGIXML_TEXT("Scale")), bone.BindLocalPose.Scale);
		bindLocalNode.append_attribute(PUGIXML_TEXT("hasMatrix")).set_value(bone.BindLocalPose.HasMatrix);
		WitchcraftXmlValueHelpers::AppendMatrixNode(bindLocalNode, PUGIXML_TEXT("Matrix"), bone.BindLocalPose.Matrix);

		WitchcraftXmlValueHelpers::AppendMatrixNode(boneNode, PUGIXML_TEXT("BindGlobalMatrix"), bone.BindGlobalMatrix);
		WitchcraftXmlValueHelpers::AppendMatrixNode(boneNode, PUGIXML_TEXT("InverseBindPose"), bone.InverseBindPose);
	}
}

bool WSkeletonFile::ReadBody(const pugi::xml_node& root)
{
	WSkeletonFileData data;
	data.Name = WitchcraftXmlFileBase::FromXmlString(root.attribute(PUGIXML_TEXT("name")).as_string());
	data.Topology.RootBoneIndex = root.attribute(PUGIXML_TEXT("rootBoneIndex")).as_int(-1);

	const pugi::xml_node bonesNode = root.child(PUGIXML_TEXT("Bones"));
	for (pugi::xml_node boneNode = bonesNode.child(PUGIXML_TEXT("Bone")); boneNode; boneNode = boneNode.next_sibling(PUGIXML_TEXT("Bone")))
	{
		Witchcraft::Animation::SkeletonBone bone;
		bone.Name = WitchcraftXmlFileBase::FromXmlString(boneNode.attribute(PUGIXML_TEXT("name")).as_string());
		bone.ParentIndex = boneNode.attribute(PUGIXML_TEXT("parentIndex")).as_int(-1);

		const pugi::xml_node bindLocalNode = boneNode.child(PUGIXML_TEXT("BindLocalPose"));
		if (bindLocalNode)
		{
			bone.BindLocalPose.Translation = WitchcraftXmlValueHelpers::ReadFloat3Attributes(bindLocalNode.child(PUGIXML_TEXT("Translation")), bone.BindLocalPose.Translation);
			bone.BindLocalPose.Rotation = WitchcraftXmlValueHelpers::ReadFloat4Attributes(bindLocalNode.child(PUGIXML_TEXT("Rotation")), bone.BindLocalPose.Rotation);
			bone.BindLocalPose.Scale = WitchcraftXmlValueHelpers::ReadFloat3Attributes(bindLocalNode.child(PUGIXML_TEXT("Scale")), bone.BindLocalPose.Scale);
			bone.BindLocalPose.HasMatrix = bindLocalNode.attribute(PUGIXML_TEXT("hasMatrix")).as_bool(false);
			bone.BindLocalPose.Matrix = WitchcraftXmlValueHelpers::ReadMatrixNode(bindLocalNode, PUGIXML_TEXT("Matrix"), Witchcraft::Animation::MakeIdentityFloat4x4());
		}

		bone.BindGlobalMatrix = WitchcraftXmlValueHelpers::ReadMatrixNode(boneNode, PUGIXML_TEXT("BindGlobalMatrix"), Witchcraft::Animation::MakeIdentityFloat4x4());
		bone.InverseBindPose = WitchcraftXmlValueHelpers::ReadMatrixNode(boneNode, PUGIXML_TEXT("InverseBindPose"), Witchcraft::Animation::MakeIdentityFloat4x4());
		data.Topology.Bones.push_back(std::move(bone));
	}

	data.Topology.RebuildNameToIndexMap();
	m_data = std::move(data);
	return true;
}

std::wstring WSkeletonFile::SerializeToText(const WSkeletonFileData& data)
{
	WSkeletonFile file;
	file.m_data = data;
	return file.SerializeDocumentToText();
}

bool WSkeletonFile::DeserializeFromText(const std::wstring& text, WSkeletonFileData* outData)
{
	if (outData == nullptr)
		return false;

	WSkeletonFile file;
	if (!file.DeserializeDocumentFromText(text))
		return false;

	*outData = std::move(file.m_data);
	return true;
}

bool WSkeletonFile::SaveToFile(const std::filesystem::path& path, const WSkeletonFileData& data)
{
	WSkeletonFile file;
	file.m_data = data;
	return file.SaveDocumentToFile(path);
}

bool WSkeletonFile::LoadFromFile(const std::filesystem::path& path, WSkeletonFileData* outData)
{
	if (outData == nullptr)
		return false;

	WSkeletonFile file;
	if (!file.LoadDocumentFromFile(path))
		return false;

	*outData = std::move(file.m_data);
	return true;
}
