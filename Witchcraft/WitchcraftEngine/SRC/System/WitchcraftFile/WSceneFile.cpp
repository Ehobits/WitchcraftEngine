#include "WSceneFile.h"

#include "WitchcraftXmlValueHelpers.h"

namespace WSceneFileDetail
{
	// 实体本地 Transform 直接压成一组紧凑属性，方便人工查看。
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

	// 普通灯光实体仍按 Light 子节点保存。
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
		lightNode.append_attribute(PUGIXML_TEXT("enableVolumetric")).set_value(lightData.EnableVolumetric);
		lightNode.append_attribute(PUGIXML_TEXT("volumetricIntensity")).set_value(lightData.VolumetricIntensity);
		lightNode.append_attribute(PUGIXML_TEXT("volumetricAttenuationDistance")).set_value(lightData.VolumetricAttenuationDistance);
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
		outLightData->EnableVolumetric = lightNode.attribute(PUGIXML_TEXT("enableVolumetric")).as_bool(outLightData->EnableVolumetric);
		outLightData->VolumetricIntensity = lightNode.attribute(PUGIXML_TEXT("volumetricIntensity")).as_float(outLightData->VolumetricIntensity);
		outLightData->VolumetricAttenuationDistance = lightNode.attribute(PUGIXML_TEXT("volumetricAttenuationDistance")).as_float(outLightData->VolumetricAttenuationDistance);
	}

	void AppendCameraNode(pugi::xml_node parent, const WSceneCameraData& cameraData)
	{
		pugi::xml_node cameraNode = parent.append_child(PUGIXML_TEXT("Camera"));
		cameraNode.append_attribute(PUGIXML_TEXT("primary")).set_value(WitchcraftXmlValueHelpers::BoolToXmlText(cameraData.Primary));
		cameraNode.append_attribute(PUGIXML_TEXT("renderEnabled")).set_value(WitchcraftXmlValueHelpers::BoolToXmlText(cameraData.RenderEnabled));
		cameraNode.append_attribute(PUGIXML_TEXT("nearZ")).set_value(cameraData.NearZ);
		cameraNode.append_attribute(PUGIXML_TEXT("farZ")).set_value(cameraData.FarZ);
		cameraNode.append_attribute(PUGIXML_TEXT("fovY")).set_value(cameraData.FovY);
		cameraNode.append_attribute(PUGIXML_TEXT("viewportScale")).set_value(cameraData.ViewportScale);
		cameraNode.append_attribute(PUGIXML_TEXT("outputTargetId")).set_value(cameraData.OutputTargetId);
	}

	void ReadCameraNode(const pugi::xml_node& parent, WSceneCameraData* outCameraData)
	{
		if (outCameraData == nullptr)
			return;

		const pugi::xml_node cameraNode = parent.child(PUGIXML_TEXT("Camera"));
		if (!cameraNode)
			return;

		outCameraData->Primary = cameraNode.attribute(PUGIXML_TEXT("primary")).as_bool(outCameraData->Primary);
		outCameraData->RenderEnabled = cameraNode.attribute(PUGIXML_TEXT("renderEnabled")).as_bool(outCameraData->RenderEnabled);
		outCameraData->NearZ = cameraNode.attribute(PUGIXML_TEXT("nearZ")).as_float(outCameraData->NearZ);
		outCameraData->FarZ = cameraNode.attribute(PUGIXML_TEXT("farZ")).as_float(outCameraData->FarZ);
		outCameraData->FovY = cameraNode.attribute(PUGIXML_TEXT("fovY")).as_float(outCameraData->FovY);
		outCameraData->ViewportScale = cameraNode.attribute(PUGIXML_TEXT("viewportScale")).as_float(outCameraData->ViewportScale);
		outCameraData->OutputTargetId = cameraNode.attribute(PUGIXML_TEXT("outputTargetId")).as_uint(outCameraData->OutputTargetId);
	}

	// 环境配置是场景级数据，不再模拟为一个可序列化实体。
	void AppendEnvironmentNode(pugi::xml_node parent, const WSceneEnvironmentData& environmentData)
	{
		if (!environmentData.HasAmbientLight)
			return;

		pugi::xml_node environmentNode = parent.append_child(PUGIXML_TEXT("Environment"));
		environmentNode.append_attribute(PUGIXML_TEXT("active")).set_value(WitchcraftXmlValueHelpers::BoolToXmlText(environmentData.AmbientLightActive));
		environmentNode.append_attribute(PUGIXML_TEXT("r")).set_value(environmentData.AmbientLight.Color.x);
		environmentNode.append_attribute(PUGIXML_TEXT("g")).set_value(environmentData.AmbientLight.Color.y);
		environmentNode.append_attribute(PUGIXML_TEXT("b")).set_value(environmentData.AmbientLight.Color.z);
		environmentNode.append_attribute(PUGIXML_TEXT("power")).set_value(environmentData.AmbientLight.Power);
	}

	void ReadEnvironmentNode(const pugi::xml_node& parent, WSceneEnvironmentData* outEnvironmentData)
	{
		if (outEnvironmentData == nullptr)
			return;

		const pugi::xml_node environmentNode = parent.child(PUGIXML_TEXT("Environment"));
		if (!environmentNode)
			return;

		outEnvironmentData->HasAmbientLight = true;
		outEnvironmentData->AmbientLight.Kind = L"Ambient";
		outEnvironmentData->AmbientLight.Type = 0.0f;
		outEnvironmentData->AmbientLight.CastShadow = false;
		outEnvironmentData->AmbientLight.EnableVolumetric = false;
		outEnvironmentData->AmbientLight.VolumetricIntensity = 0.0f;
		outEnvironmentData->AmbientLight.VolumetricAttenuationDistance = 0.0f;
		outEnvironmentData->AmbientLightActive =
			environmentNode.attribute(PUGIXML_TEXT("active")).as_bool(outEnvironmentData->AmbientLightActive);
		outEnvironmentData->AmbientLight.Color.x =
			environmentNode.attribute(PUGIXML_TEXT("r")).as_float(outEnvironmentData->AmbientLight.Color.x);
		outEnvironmentData->AmbientLight.Color.y =
			environmentNode.attribute(PUGIXML_TEXT("g")).as_float(outEnvironmentData->AmbientLight.Color.y);
		outEnvironmentData->AmbientLight.Color.z =
			environmentNode.attribute(PUGIXML_TEXT("b")).as_float(outEnvironmentData->AmbientLight.Color.z);
		outEnvironmentData->AmbientLight.Power =
			environmentNode.attribute(PUGIXML_TEXT("power")).as_float(outEnvironmentData->AmbientLight.Power);
	}

	void AppendEntityTypeColorsNode(pugi::xml_node parent, const std::vector<WSceneEntityTypeColorData>& typeColors)
	{
		if (typeColors.empty())
			return;

		pugi::xml_node typeColorsNode = parent.append_child(PUGIXML_TEXT("EntityTypeColors"));
		for (const WSceneEntityTypeColorData& typeColor : typeColors)
		{
			pugi::xml_node typeNode = typeColorsNode.append_child(PUGIXML_TEXT("Type"));
			typeNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(typeColor.Type).c_str());
			typeNode.append_attribute(PUGIXML_TEXT("r")).set_value(typeColor.Color.x);
			typeNode.append_attribute(PUGIXML_TEXT("g")).set_value(typeColor.Color.y);
			typeNode.append_attribute(PUGIXML_TEXT("b")).set_value(typeColor.Color.z);
			typeNode.append_attribute(PUGIXML_TEXT("a")).set_value(typeColor.Color.w);
		}
	}

	void ReadEntityTypeColorsNode(const pugi::xml_node& parent, std::vector<WSceneEntityTypeColorData>* outTypeColors)
	{
		if (outTypeColors == nullptr)
			return;

		const pugi::xml_node typeColorsNode = parent.child(PUGIXML_TEXT("EntityTypeColors"));
		if (!typeColorsNode)
			return;

		for (pugi::xml_node typeNode = typeColorsNode.child(PUGIXML_TEXT("Type"));
			typeNode;
			typeNode = typeNode.next_sibling(PUGIXML_TEXT("Type")))
		{
			WSceneEntityTypeColorData typeColor;
			typeColor.Type = WitchcraftXmlFileBase::FromXmlString(typeNode.attribute(PUGIXML_TEXT("name")).as_string());
			typeColor.Color.x = typeNode.attribute(PUGIXML_TEXT("r")).as_float(typeColor.Color.x);
			typeColor.Color.y = typeNode.attribute(PUGIXML_TEXT("g")).as_float(typeColor.Color.y);
			typeColor.Color.z = typeNode.attribute(PUGIXML_TEXT("b")).as_float(typeColor.Color.z);
			typeColor.Color.w = typeNode.attribute(PUGIXML_TEXT("a")).as_float(typeColor.Color.w);
			outTypeColors->push_back(std::move(typeColor));
		}
	}

	// 后处理 / 阴影等全局画面参数单独挂在 RenderSettings 节点。
	void AppendRenderSettingsNode(pugi::xml_node parent, const WSceneRenderSettingsData& renderSettings)
	{
		pugi::xml_node renderSettingsNode = parent.append_child(PUGIXML_TEXT("RenderSettings"));
		renderSettingsNode.append_attribute(PUGIXML_TEXT("shadowOpacity")).set_value(renderSettings.ShadowOpacity);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("shadowSoftness")).set_value(renderSettings.ShadowSoftness);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("aoEnabled")).set_value(renderSettings.AOEnabled);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("aoStrength")).set_value(renderSettings.AOStrength);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("aoRadius")).set_value(renderSettings.AORadius);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("aoFadeStart")).set_value(renderSettings.AOFadeStart);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("aoFadeEnd")).set_value(renderSettings.AOFadeEnd);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("aoSurfaceEpsilon")).set_value(renderSettings.AOSurfaceEpsilon);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("aoBlurSigma")).set_value(renderSettings.AOBlurSigma);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("fxaaEnabled")).set_value(renderSettings.FXAAEnabled);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("fxaaContrastThreshold")).set_value(renderSettings.FXAAContrastThreshold);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("fxaaRelativeThreshold")).set_value(renderSettings.FXAARelativeThreshold);
		renderSettingsNode.append_attribute(PUGIXML_TEXT("fxaaSpanMax")).set_value(renderSettings.FXAASpanMax);
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
		outRenderSettings->AOEnabled = renderSettingsNode.attribute(PUGIXML_TEXT("aoEnabled")).as_bool(outRenderSettings->AOEnabled);
		outRenderSettings->AOStrength = renderSettingsNode.attribute(PUGIXML_TEXT("aoStrength")).as_float(outRenderSettings->AOStrength);
		outRenderSettings->AORadius = renderSettingsNode.attribute(PUGIXML_TEXT("aoRadius")).as_float(outRenderSettings->AORadius);
		outRenderSettings->AOFadeStart = renderSettingsNode.attribute(PUGIXML_TEXT("aoFadeStart")).as_float(outRenderSettings->AOFadeStart);
		outRenderSettings->AOFadeEnd = renderSettingsNode.attribute(PUGIXML_TEXT("aoFadeEnd")).as_float(outRenderSettings->AOFadeEnd);
		outRenderSettings->AOSurfaceEpsilon = renderSettingsNode.attribute(PUGIXML_TEXT("aoSurfaceEpsilon")).as_float(outRenderSettings->AOSurfaceEpsilon);
		outRenderSettings->AOBlurSigma = renderSettingsNode.attribute(PUGIXML_TEXT("aoBlurSigma")).as_float(outRenderSettings->AOBlurSigma);
		outRenderSettings->FXAAEnabled = renderSettingsNode.attribute(PUGIXML_TEXT("fxaaEnabled")).as_bool(outRenderSettings->FXAAEnabled);
		outRenderSettings->FXAAContrastThreshold = renderSettingsNode.attribute(PUGIXML_TEXT("fxaaContrastThreshold")).as_float(outRenderSettings->FXAAContrastThreshold);
		outRenderSettings->FXAARelativeThreshold = renderSettingsNode.attribute(PUGIXML_TEXT("fxaaRelativeThreshold")).as_float(outRenderSettings->FXAARelativeThreshold);
		outRenderSettings->FXAASpanMax = renderSettingsNode.attribute(PUGIXML_TEXT("fxaaSpanMax")).as_float(outRenderSettings->FXAASpanMax);
	}
}

using namespace WSceneFileDetail;

const wchar_t* WSceneFile::GetRootNodeName() const
{
	return RootNodeName;
}

// 写出一份完整场景：元数据、环境、全局渲染设置、外部模型资产引用和实体列表。
void WSceneFile::BuildBody(pugi::xml_node root) const
{
	pugi::xml_node metaNode = root.append_child(PUGIXML_TEXT("Meta"));
	metaNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.Meta.Name).c_str());
	metaNode.append_attribute(PUGIXML_TEXT("createdAt")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.Meta.CreatedAt).c_str());
	metaNode.append_attribute(PUGIXML_TEXT("updatedAt")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.Meta.UpdatedAt).c_str());

	AppendRenderSettingsNode(root, m_data.RenderSettings);
	AppendEnvironmentNode(root, m_data.Environment);
	AppendEntityTypeColorsNode(root, m_data.EntityTypeColors);

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
		entityNode.append_attribute(PUGIXML_TEXT("type")).set_value(WitchcraftXmlFileBase::ToXmlString(entityData.EntityType).c_str());
		entityNode.append_attribute(PUGIXML_TEXT("active")).set_value(WitchcraftXmlValueHelpers::BoolToXmlText(entityData.Active));
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

		if (entityData.HasCamera)
			AppendCameraNode(entityNode, entityData.Camera);

		entityNode.append_attribute(PUGIXML_TEXT("hasSkeleton")).set_value(WitchcraftXmlValueHelpers::BoolToXmlText(entityData.HasSkeleton));
		entityNode.append_attribute(PUGIXML_TEXT("hasAnimator")).set_value(WitchcraftXmlValueHelpers::BoolToXmlText(entityData.HasAnimator));
		entityNode.append_attribute(PUGIXML_TEXT("hasSkinningRuntime")).set_value(WitchcraftXmlValueHelpers::BoolToXmlText(entityData.HasSkinningRuntime));
	}
}

// 读取场景文件并还原到内存结构；这里只负责 XML -> 数据，不负责创建运行时对象。
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
	ReadEnvironmentNode(root, &sceneData.Environment);
	ReadEntityTypeColorsNode(root, &sceneData.EntityTypeColors);

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
		entityData.EntityType = WitchcraftXmlFileBase::FromXmlString(entityNode.attribute(PUGIXML_TEXT("type")).as_string());
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

		const pugi::xml_node cameraNode = entityNode.child(PUGIXML_TEXT("Camera"));
		if (cameraNode)
		{
			entityData.HasCamera = true;
			ReadCameraNode(entityNode, &entityData.Camera);
		}

		entityData.HasSkeleton = entityNode.attribute(PUGIXML_TEXT("hasSkeleton")).as_bool(false);
		entityData.HasAnimator = entityNode.attribute(PUGIXML_TEXT("hasAnimator")).as_bool(false);
		entityData.HasSkinningRuntime = entityNode.attribute(PUGIXML_TEXT("hasSkinningRuntime")).as_bool(false);

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
