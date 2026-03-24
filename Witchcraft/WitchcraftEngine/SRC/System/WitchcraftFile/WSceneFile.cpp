#include "WSceneFile.h"

namespace
{
	const pugi::char_t* BoolToXmlText(bool value)
	{
		return value ? PUGIXML_TEXT("true") : PUGIXML_TEXT("false");
	}

	void AppendTransformNode(pugi::xml_node parent, const Transform& transform)
	{
		pugi::xml_node transformNode = parent.append_child(PUGIXML_TEXT("Transform"));
		transformNode.append_attribute(PUGIXML_TEXT("px")).set_value(transform.position.x);
		transformNode.append_attribute(PUGIXML_TEXT("py")).set_value(transform.position.y);
		transformNode.append_attribute(PUGIXML_TEXT("pz")).set_value(transform.position.z);
		transformNode.append_attribute(PUGIXML_TEXT("rx")).set_value(transform.rotation.x);
		transformNode.append_attribute(PUGIXML_TEXT("ry")).set_value(transform.rotation.y);
		transformNode.append_attribute(PUGIXML_TEXT("rz")).set_value(transform.rotation.z);
		transformNode.append_attribute(PUGIXML_TEXT("sx")).set_value(transform.scale.x);
		transformNode.append_attribute(PUGIXML_TEXT("sy")).set_value(transform.scale.y);
		transformNode.append_attribute(PUGIXML_TEXT("sz")).set_value(transform.scale.z);
	}

	void ReadTransformNode(const pugi::xml_node& parent, Transform* outTransform)
	{
		if (outTransform == nullptr)
			return;

		const pugi::xml_node transformNode = parent.child(PUGIXML_TEXT("Transform"));
		if (!transformNode)
			return;

		outTransform->position.x = transformNode.attribute(PUGIXML_TEXT("px")).as_float(outTransform->position.x);
		outTransform->position.y = transformNode.attribute(PUGIXML_TEXT("py")).as_float(outTransform->position.y);
		outTransform->position.z = transformNode.attribute(PUGIXML_TEXT("pz")).as_float(outTransform->position.z);
		outTransform->rotation.x = transformNode.attribute(PUGIXML_TEXT("rx")).as_float(outTransform->rotation.x);
		outTransform->rotation.y = transformNode.attribute(PUGIXML_TEXT("ry")).as_float(outTransform->rotation.y);
		outTransform->rotation.z = transformNode.attribute(PUGIXML_TEXT("rz")).as_float(outTransform->rotation.z);
		outTransform->scale.x = transformNode.attribute(PUGIXML_TEXT("sx")).as_float(outTransform->scale.x);
		outTransform->scale.y = transformNode.attribute(PUGIXML_TEXT("sy")).as_float(outTransform->scale.y);
		outTransform->scale.z = transformNode.attribute(PUGIXML_TEXT("sz")).as_float(outTransform->scale.z);
	}

	void AppendLightNode(pugi::xml_node parent, const WSceneLightData& lightData)
	{
		pugi::xml_node lightNode = parent.append_child(PUGIXML_TEXT("Light"));
		lightNode.append_attribute(PUGIXML_TEXT("kind")).set_value(WitchcraftXmlFileBase::ToXmlString(lightData.Kind).c_str());
		lightNode.append_attribute(PUGIXML_TEXT("type")).set_value(lightData.Type);
		lightNode.append_attribute(PUGIXML_TEXT("r")).set_value(lightData.Color.x);
		lightNode.append_attribute(PUGIXML_TEXT("g")).set_value(lightData.Color.y);
		lightNode.append_attribute(PUGIXML_TEXT("b")).set_value(lightData.Color.z);
		lightNode.append_attribute(PUGIXML_TEXT("power")).set_value(lightData.Power);
		lightNode.append_attribute(PUGIXML_TEXT("castShadow")).set_value(lightData.CastShadow);
	}

	void ReadLightNode(const pugi::xml_node& parent, WSceneLightData* outLightData)
	{
		if (outLightData == nullptr)
			return;

		const pugi::xml_node lightNode = parent.child(PUGIXML_TEXT("Light"));
		if (!lightNode)
			return;

		outLightData->Kind = WitchcraftXmlFileBase::FromXmlString(lightNode.attribute(PUGIXML_TEXT("kind")).as_string());
		outLightData->Type = lightNode.attribute(PUGIXML_TEXT("type")).as_float(outLightData->Type);
		outLightData->Color.x = lightNode.attribute(PUGIXML_TEXT("r")).as_float(outLightData->Color.x);
		outLightData->Color.y = lightNode.attribute(PUGIXML_TEXT("g")).as_float(outLightData->Color.y);
		outLightData->Color.z = lightNode.attribute(PUGIXML_TEXT("b")).as_float(outLightData->Color.z);
		outLightData->Power = lightNode.attribute(PUGIXML_TEXT("power")).as_float(outLightData->Power);
		outLightData->CastShadow = lightNode.attribute(PUGIXML_TEXT("castShadow")).as_bool(outLightData->CastShadow);
	}

	void AppendRenderSettingsNode(pugi::xml_node parent, const WSceneRenderSettingsData& renderSettings)
	{
		pugi::xml_node renderSettingsNode = parent.append_child(PUGIXML_TEXT("RenderSettings"));
		renderSettingsNode.append_attribute(PUGIXML_TEXT("shadowOpacity")).set_value(renderSettings.ShadowOpacity);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("shadowSoftness")).set_value(renderSettings.ShadowSoftness);
	}

	void ReadRenderSettingsNode(const pugi::xml_node& parent, WSceneRenderSettingsData* outRenderSettings)
	{
		if (outRenderSettings == nullptr)
			return;

		const pugi::xml_node renderSettingsNode = parent.child(PUGIXML_TEXT("RenderSettings"));
		if (!renderSettingsNode)
			return;

		outRenderSettings->ShadowOpacity = renderSettingsNode.attribute(PUGIXML_TEXT("shadowOpacity")).as_float(outRenderSettings->ShadowOpacity);
		outRenderSettings->ShadowSoftness = renderSettingsNode.attribute(PUGIXML_TEXT("shadowSoftness")).as_float(outRenderSettings->ShadowSoftness);
	}
}

const wchar_t* WSceneFile::GetRootNodeName() const
{
	return RootNodeName;
}

void WSceneFile::BuildBody(pugi::xml_node root) const
{
	pugi::xml_node metaNode = root.append_child(PUGIXML_TEXT("Meta"));
	metaNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.Meta.Name).c_str());
	metaNode.append_attribute(PUGIXML_TEXT("createdAt")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.Meta.CreatedAt).c_str());
	metaNode.append_attribute(PUGIXML_TEXT("updatedAt")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.Meta.UpdatedAt).c_str());

	AppendRenderSettingsNode(root, m_data.RenderSettings);

	pugi::xml_node assetsNode = root.append_child(PUGIXML_TEXT("Assets"));
	for (const WSceneModelAssetData& modelData : m_data.Models)
	{
		pugi::xml_node modelNode = assetsNode.append_child(PUGIXML_TEXT("Model"));
		modelNode.append_attribute(PUGIXML_TEXT("id")).set_value(WitchcraftXmlFileBase::ToXmlString(modelData.Id).c_str());
		modelNode.append_attribute(PUGIXML_TEXT("path")).set_value(WitchcraftXmlFileBase::ToXmlString(modelData.Path).c_str());
	}

	pugi::xml_node entitiesNode = root.append_child(PUGIXML_TEXT("Entities"));
	for (const WSceneEntityData& entityData : m_data.Entities)
	{
		pugi::xml_node entityNode = entitiesNode.append_child(PUGIXML_TEXT("Entity"));
		entityNode.append_attribute(PUGIXML_TEXT("id")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Id).c_str());
		entityNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Name).c_str());
		entityNode.append_attribute(PUGIXML_TEXT("active")).set_value(BoolToXmlText(entityData.Active));
		entityNode.append_attribute(PUGIXML_TEXT("parentId")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.ParentId).c_str());

		AppendTransformNode(entityNode, entityData.LocalTransform);

		if (entityData.HasMesh)
		{
			pugi::xml_node meshNode = entityNode.append_child(PUGIXML_TEXT("Mesh"));
			meshNode.append_attribute(PUGIXML_TEXT("renderSourceType")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Mesh.RenderSourceType).c_str());
			meshNode.append_attribute(PUGIXML_TEXT("renderLayer")).set_value(entityData.Mesh.RenderLayer);
			meshNode.append_attribute(PUGIXML_TEXT("primitiveKind")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Mesh.PrimitiveKind).c_str());
			meshNode.append_attribute(PUGIXML_TEXT("geometryRef")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Mesh.GeometryRef).c_str());
			meshNode.append_attribute(PUGIXML_TEXT("skyTexturePath")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Mesh.SkyTexturePath).c_str());
			meshNode.append_attribute(PUGIXML_TEXT("rawSubMeshIndex")).set_value(entityData.Mesh.RawSubMeshIndex);
			meshNode.append_attribute(PUGIXML_TEXT("modelRef")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Mesh.ModelRef).c_str());
			meshNode.append_attribute(PUGIXML_TEXT("meshName")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Mesh.MeshName).c_str());
			meshNode.append_attribute(PUGIXML_TEXT("nodeId")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Mesh.NodeId).c_str());
			meshNode.append_attribute(PUGIXML_TEXT("materialName")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Mesh.MaterialName).c_str());
			meshNode.append_attribute(PUGIXML_TEXT("materialFile")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.Mesh.MaterialFile).c_str());
		}

		if (entityData.HasLight)
			AppendLightNode(entityNode, entityData.Light);
	}
}

bool WSceneFile::ReadBody(const pugi::xml_node& root)
{
	WSceneFileData sceneData;

	const pugi::xml_node metaNode = root.child(PUGIXML_TEXT("Meta"));
	if (metaNode)
	{
		sceneData.Meta.Name = WitchcraftXmlFileBase::FromXmlString(metaNode.attribute(PUGIXML_TEXT("name")).as_string());
		sceneData.Meta.CreatedAt = WitchcraftXmlFileBase::FromXmlString(metaNode.attribute(PUGIXML_TEXT("createdAt")).as_string());
		sceneData.Meta.UpdatedAt = WitchcraftXmlFileBase::FromXmlString(metaNode.attribute(PUGIXML_TEXT("updatedAt")).as_string());
	}

	ReadRenderSettingsNode(root, &sceneData.RenderSettings);

	const pugi::xml_node assetsNode = root.child(PUGIXML_TEXT("Assets"));
	for (pugi::xml_node modelNode = assetsNode.child(PUGIXML_TEXT("Model"));
		modelNode;
		modelNode = modelNode.next_sibling(PUGIXML_TEXT("Model")))
	{
		WSceneModelAssetData modelData;
		modelData.Id = WitchcraftXmlFileBase::FromXmlString(modelNode.attribute(PUGIXML_TEXT("id")).as_string());
		modelData.Path = WitchcraftXmlFileBase::FromXmlString(modelNode.attribute(PUGIXML_TEXT("path")).as_string());
		sceneData.Models.push_back(std::move(modelData));
	}

	const pugi::xml_node entitiesNode = root.child(PUGIXML_TEXT("Entities"));
	for (pugi::xml_node entityNode = entitiesNode.child(PUGIXML_TEXT("Entity"));
		entityNode;
		entityNode = entityNode.next_sibling(PUGIXML_TEXT("Entity")))
	{
		WSceneEntityData entityData;
		entityData.Id = WitchcraftXmlFileBase::FromXmlString(entityNode.attribute(PUGIXML_TEXT("id")).as_string());
		entityData.Name = WitchcraftXmlFileBase::FromXmlString(entityNode.attribute(PUGIXML_TEXT("name")).as_string());
		entityData.Active = entityNode.attribute(PUGIXML_TEXT("active")).as_bool(true);
		entityData.ParentId = WitchcraftXmlFileBase::FromXmlString(entityNode.attribute(PUGIXML_TEXT("parentId")).as_string());
		ReadTransformNode(entityNode, &entityData.LocalTransform);

		const pugi::xml_node meshNode = entityNode.child(PUGIXML_TEXT("Mesh"));
		if (meshNode)
		{
			entityData.HasMesh = true;
			entityData.Mesh.RenderSourceType = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("renderSourceType")).as_string());
			entityData.Mesh.RenderLayer = meshNode.attribute(PUGIXML_TEXT("renderLayer")).as_uint(entityData.Mesh.RenderLayer);
			entityData.Mesh.PrimitiveKind = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("primitiveKind")).as_string());
			entityData.Mesh.GeometryRef = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("geometryRef")).as_string());
			entityData.Mesh.SkyTexturePath = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("skyTexturePath")).as_string());
			entityData.Mesh.RawSubMeshIndex = meshNode.attribute(PUGIXML_TEXT("rawSubMeshIndex")).as_int(entityData.Mesh.RawSubMeshIndex);
			entityData.Mesh.ModelRef = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("modelRef")).as_string());
			entityData.Mesh.MeshName = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("meshName")).as_string());
			entityData.Mesh.NodeId = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("nodeId")).as_string());
			entityData.Mesh.MaterialName = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("materialName")).as_string());
			entityData.Mesh.MaterialFile = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("materialFile")).as_string());
		}

		const pugi::xml_node lightNode = entityNode.child(PUGIXML_TEXT("Light"));
		if (lightNode)
		{
			entityData.HasLight = true;
			ReadLightNode(entityNode, &entityData.Light);
		}

		sceneData.Entities.push_back(std::move(entityData));
	}

	m_data = std::move(sceneData);
	return true;
}

std::wstring WSceneFile::SerializeToText(const WSceneFileData& data)
{
	WSceneFile file;
	file.m_data = data;
	return file.SerializeDocumentToText();
}

bool WSceneFile::DeserializeFromText(const std::wstring& text, WSceneFileData* outData)
{
	if (outData == nullptr)
		return false;

	WSceneFile file;
	if (!file.DeserializeDocumentFromText(text))
		return false;

	*outData = std::move(file.m_data);
	return true;
}

bool WSceneFile::SaveToFile(const std::filesystem::path& path, const WSceneFileData& data)
{
	WSceneFile file;
	file.m_data = data;
	return file.SaveDocumentToFile(path);
}

bool WSceneFile::LoadFromFile(const std::filesystem::path& path, WSceneFileData* outData)
{
	if (outData == nullptr)
		return false;

	WSceneFile file;
	if (!file.LoadDocumentFromFile(path))
		return false;

	*outData = std::move(file.m_data);
	return true;
}
