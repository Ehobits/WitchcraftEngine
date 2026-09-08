#include "WProjectFile.h"

#include "WitchcraftXmlValueHelpers.h"

#include <utility>

namespace WProjectFileDetail
{
	void AppendMetaNode(pugi::xml_node parent, const WProjectMetaData& meta)
	{
		pugi::xml_node metaNode = parent.append_child(PUGIXML_TEXT("Meta"));
		metaNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(meta.Name).c_str());
		metaNode.append_attribute(PUGIXML_TEXT("createdAt")).set_value(WitchcraftXmlFileBase::ToXmlString(meta.CreatedAt).c_str());
		metaNode.append_attribute(PUGIXML_TEXT("updatedAt")).set_value(WitchcraftXmlFileBase::ToXmlString(meta.UpdatedAt).c_str());
	}

	void ReadMetaNode(const pugi::xml_node& parent, WProjectMetaData* outMeta)
	{
		if (outMeta == nullptr)
			return;

		const pugi::xml_node metaNode = parent.child(PUGIXML_TEXT("Meta"));
		if (!metaNode)
			return;

		outMeta->Name = WitchcraftXmlFileBase::FromXmlString(metaNode.attribute(PUGIXML_TEXT("name")).as_string());
		outMeta->CreatedAt = WitchcraftXmlFileBase::FromXmlString(metaNode.attribute(PUGIXML_TEXT("createdAt")).as_string());
		outMeta->UpdatedAt = WitchcraftXmlFileBase::FromXmlString(metaNode.attribute(PUGIXML_TEXT("updatedAt")).as_string());
	}

	void AppendSettingsNode(pugi::xml_node parent, const WProjectSettingsData& settings)
	{
		pugi::xml_node settingsNode = parent.append_child(PUGIXML_TEXT("Settings"));
		settingsNode.append_attribute(PUGIXML_TEXT("defaultScenePath")).set_value(WitchcraftXmlFileBase::ToXmlString(settings.DefaultScenePath).c_str());
		settingsNode.append_attribute(PUGIXML_TEXT("currentScenePath")).set_value(WitchcraftXmlFileBase::ToXmlString(settings.CurrentScenePath).c_str());
	}

	void ReadSettingsNode(const pugi::xml_node& parent, WProjectSettingsData* outSettings)
	{
		if (outSettings == nullptr)
			return;

		const pugi::xml_node settingsNode = parent.child(PUGIXML_TEXT("Settings"));
		if (!settingsNode)
			return;

		outSettings->DefaultScenePath = WitchcraftXmlFileBase::FromXmlString(settingsNode.attribute(PUGIXML_TEXT("defaultScenePath")).as_string());
		outSettings->CurrentScenePath = WitchcraftXmlFileBase::FromXmlString(settingsNode.attribute(PUGIXML_TEXT("currentScenePath")).as_string());
		if (outSettings->CurrentScenePath.empty())
			outSettings->CurrentScenePath = WitchcraftXmlFileBase::FromXmlString(settingsNode.attribute(PUGIXML_TEXT("lastActiveScenePath")).as_string());
	}

	void AppendScenesNode(pugi::xml_node parent, const std::vector<WProjectSceneData>& scenes)
	{
		pugi::xml_node scenesNode = parent.append_child(PUGIXML_TEXT("Scenes"));
		for (const WProjectSceneData& scene : scenes)
		{
			pugi::xml_node sceneNode = scenesNode.append_child(PUGIXML_TEXT("Scene"));
			sceneNode.append_attribute(PUGIXML_TEXT("id")).set_value(WitchcraftXmlFileBase::ToXmlString(scene.Id).c_str());
			sceneNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(scene.Name).c_str());
			sceneNode.append_attribute(PUGIXML_TEXT("path")).set_value(WitchcraftXmlFileBase::ToXmlString(scene.Path).c_str());
			sceneNode.append_attribute(PUGIXML_TEXT("entry")).set_value(WitchcraftXmlValueHelpers::BoolToXmlText(scene.Entry));
		}
	}

	void ReadScenesNode(const pugi::xml_node& parent, std::vector<WProjectSceneData>* outScenes)
	{
		if (outScenes == nullptr)
			return;

		const pugi::xml_node scenesNode = parent.child(PUGIXML_TEXT("Scenes"));
		if (!scenesNode)
			return;

		for (pugi::xml_node sceneNode = scenesNode.child(PUGIXML_TEXT("Scene"));
			sceneNode;
			sceneNode = sceneNode.next_sibling(PUGIXML_TEXT("Scene")))
		{
			WProjectSceneData scene;
			scene.Id = WitchcraftXmlFileBase::FromXmlString(sceneNode.attribute(PUGIXML_TEXT("id")).as_string());
			scene.Name = WitchcraftXmlFileBase::FromXmlString(sceneNode.attribute(PUGIXML_TEXT("name")).as_string());
			scene.Path = WitchcraftXmlFileBase::FromXmlString(sceneNode.attribute(PUGIXML_TEXT("path")).as_string());
			scene.Entry = sceneNode.attribute(PUGIXML_TEXT("entry")).as_bool(scene.Entry);
			outScenes->push_back(std::move(scene));
		}
	}

	void AppendEditorStateNode(pugi::xml_node parent, const WProjectEditorStateData& editorState)
	{
		pugi::xml_node editorNode = parent.append_child(PUGIXML_TEXT("EditorState"));
		editorNode.append_attribute(PUGIXML_TEXT("currentSceneId")).set_value(WitchcraftXmlFileBase::ToXmlString(editorState.CurrentSceneId).c_str());
	}

	void ReadEditorStateNode(const pugi::xml_node& parent, WProjectEditorStateData* outEditorState)
	{
		if (outEditorState == nullptr)
			return;

		const pugi::xml_node editorNode = parent.child(PUGIXML_TEXT("EditorState"));
		if (!editorNode)
			return;

		outEditorState->CurrentSceneId = WitchcraftXmlFileBase::FromXmlString(editorNode.attribute(PUGIXML_TEXT("currentSceneId")).as_string());
		if (outEditorState->CurrentSceneId.empty())
			outEditorState->CurrentSceneId = WitchcraftXmlFileBase::FromXmlString(editorNode.attribute(PUGIXML_TEXT("activeSceneId")).as_string());
	}
}

const wchar_t* WProjectFile::GetRootNodeName() const
{
	return RootNodeName;
}

void WProjectFile::BuildBody(pugi::xml_node root) const
{
	WProjectFileDetail::AppendMetaNode(root, m_data.Meta);
	WProjectFileDetail::AppendSettingsNode(root, m_data.Settings);
	WProjectFileDetail::AppendScenesNode(root, m_data.Scenes);
	WProjectFileDetail::AppendEditorStateNode(root, m_data.EditorState);
}

bool WProjectFile::ReadBody(const pugi::xml_node& root)
{
	WProjectFileData projectData;
	WProjectFileDetail::ReadMetaNode(root, &projectData.Meta);
	WProjectFileDetail::ReadSettingsNode(root, &projectData.Settings);
	WProjectFileDetail::ReadScenesNode(root, &projectData.Scenes);
	WProjectFileDetail::ReadEditorStateNode(root, &projectData.EditorState);
	m_data = std::move(projectData);
	return true;
}

std::wstring WProjectFile::SerializeToText(const WProjectFileData& data)
{
	WProjectFile file;
	file.m_data = data;
	return file.SerializeDocumentToText();
}

bool WProjectFile::DeserializeFromText(const std::wstring& text, WProjectFileData* outData)
{
	if (outData == nullptr)
		return false;

	WProjectFile file;
	if (!file.DeserializeDocumentFromText(text))
		return false;

	*outData = std::move(file.m_data);
	return true;
}

bool WProjectFile::SaveToFile(const std::filesystem::path& path, const WProjectFileData& data)
{
	WProjectFile file;
	file.m_data = data;
	return file.SaveDocumentToFile(path);
}

bool WProjectFile::LoadFromFile(const std::filesystem::path& path, WProjectFileData* outData)
{
	if (outData == nullptr)
		return false;

	WProjectFile file;
	if (!file.LoadDocumentFromFile(path))
		return false;

	*outData = std::move(file.m_data);
	return true;
}
