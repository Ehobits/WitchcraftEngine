#include "D3DWindow.h"
#include "D3DWindowAssetHelpers.h"

// RenderAssetImport
// 说明：
// - 该实现文件承接 D3DWindow 中“材质/贴图导入与创建”的核心逻辑；
// - 当前仍通过 D3DWindow 的成员函数形式暴露，先做物理拆分，不改现有接口与调用关系；
// - 第一轮仅迁移最核心的导入/建材函数，避免把材质应用与渲染层重排一起卷入。

void D3DWindow::CreateImportedMaterialTextureSlot(
	std::vector<Texture>& textureGroup,
	UINT slotIndex,
	const ImportedTextureSource& source,
	Texture* fallback,
	const std::wstring& uniqueMaterialName,
	const std::wstring& slotName,
	ResourceUploadBatch& resourceUpload)
{
	UINT descriptorIndex = 0;
	if (!TryReserveSrvDescriptorSlots(1, L"CreateImportedMaterialTextureSlot", &descriptorIndex))
	{
		if (fallback != nullptr)
			textureGroup[slotIndex] = *fallback;
		return;
	}

	Texture texture;
	if (!source.Path.empty() && std::filesystem::exists(source.Path))
	{
		texture.Create(
			d3dDevice.Get(),
			SrvDescriptorHeap.Get(),
			&resourceUpload,
			uniqueMaterialName + L"_" + slotName,
			source.Path,
			D3DWindowAssetHelpers::ResolveTextureTypeFromPath(source.Path),
			descriptorIndex);
	}
	else
	{
		texture.CreateAlias(
			d3dDevice.Get(),
			SrvDescriptorHeap.Get(),
			uniqueMaterialName + L"_" + slotName,
			fallback != nullptr ? fallback->GetResource() : nullptr,
			descriptorIndex);
	}

	textureGroup[slotIndex] = texture;
}

void D3DWindow::CreateMaterialTextureSlot(
	std::vector<Texture>& textureGroup,
	UINT slotIndex,
	const ImportedTextureSource& source,
	Texture* fallbackTexture,
	const std::wstring& MaterialName,
	const wchar_t* slotName,
	ResourceUploadBatch& resourceUpload,
	std::vector<Texture>* existingTextureGroup)
{
	if (slotIndex >= textureGroup.size())
		return;

	UINT descriptorIndex = 0;
	bool hasDescriptorSlot = false;

	if (existingTextureGroup != nullptr && slotIndex < existingTextureGroup->size())
	{
		descriptorIndex = (*existingTextureGroup)[slotIndex].GetIndex();
		hasDescriptorSlot = true;
	}
	else
	{
		hasDescriptorSlot = TryReserveSrvDescriptorSlots(
			1,
			L"ApplyMaterialPbrTexturesFromWMaterialData",
			&descriptorIndex);
	}

	if (!hasDescriptorSlot)
	{
		if (fallbackTexture != nullptr)
			textureGroup[slotIndex] = *fallbackTexture;
		return;
	}

	Texture texture;
	if (!source.Path.empty() && std::filesystem::exists(source.Path))
	{
		texture.Create(
			d3dDevice.Get(),
			SrvDescriptorHeap.Get(),
			&resourceUpload,
			MaterialName + L"_" + slotName,
			source.Path,
			D3DWindowAssetHelpers::ResolveTextureTypeFromPath(source.Path),
			descriptorIndex);
	}
	else
	{
		texture.CreateAlias(
			d3dDevice.Get(),
			SrvDescriptorHeap.Get(),
			MaterialName + L"_" + slotName,
			fallbackTexture != nullptr ? fallbackTexture->GetResource() : nullptr,
			descriptorIndex);
	}

	textureGroup[slotIndex] = texture;
}

std::wstring D3DWindow::CreateMaterialFromImport(const std::wstring& name, const ImportedMaterialInfo& materialInfo)
{
	const std::wstring uniqueMaterialName = D3DWindowAssetHelpers::MakeUniqueName(Materials, name.empty() ? L"ImportedMaterial" : name);
	const std::wstring textureGroupName = uniqueMaterialName + L"_TextureGroup";

	Texture* fallbackDiffuse = &TextureGroups[L"Diffuse"][0];
	Texture* fallbackNormal = &TextureGroups[L"Diffuse"][1];
	Texture* fallbackSpecular = &TextureGroups[L"Diffuse"][0];
	Texture* fallbackMetallic = &TextureGroups[L"Diffuse"][3];
	Texture* fallbackRoughness = &TextureGroups[L"Diffuse"][4];

	std::vector<Texture> textureGroup(5);
	ResourceUploadBatch resourceUpload(d3dDevice.Get());
	resourceUpload.Begin();

	CreateImportedMaterialTextureSlot(textureGroup, 0, materialInfo.DiffuseTexture, fallbackDiffuse, uniqueMaterialName, L"Diffuse", resourceUpload);
	CreateImportedMaterialTextureSlot(textureGroup, 1, materialInfo.NormalTexture, fallbackNormal, uniqueMaterialName, L"Normal", resourceUpload);
	CreateImportedMaterialTextureSlot(textureGroup, 2, materialInfo.SpecularTexture, fallbackSpecular, uniqueMaterialName, L"Specular", resourceUpload);
	CreateImportedMaterialTextureSlot(textureGroup, 3, materialInfo.MetallicTexture, fallbackMetallic, uniqueMaterialName, L"Metallic", resourceUpload);
	CreateImportedMaterialTextureSlot(textureGroup, 4, materialInfo.RoughnessTexture, fallbackRoughness, uniqueMaterialName, L"Roughness", resourceUpload);

	auto uploadResourcesFinished = resourceUpload.End(CommandQueue.Get());
	uploadResourcesFinished.wait();

	TextureGroups[textureGroupName] = textureGroup;

	Material material;
	material.SetName(uniqueMaterialName);
	material.MatCBIndex = (int)Materials.size();
	material.DiffuseTexture = &TextureGroups[textureGroupName][0];
	material.NormalTexture = &TextureGroups[textureGroupName][1];
	material.SpecularTexture = &TextureGroups[textureGroupName][2];
	material.MetallicTexture = &TextureGroups[textureGroupName][3];
	material.RoughnessTexture = &TextureGroups[textureGroupName][4];
	const bool useDiffuseAlphaAsOpacity =
		materialInfo.UseOpacityTexture || D3DWindowAssetHelpers::IsLikelyAlphaCarrierTexturePath(materialInfo.DiffuseTexture.Path);
	material.OpacityTexture = useDiffuseAlphaAsOpacity ? material.DiffuseTexture : nullptr;
	material.Properties.DiffuseAlbedo = materialInfo.DiffuseColor;
	material.Properties.FresnelR0 = materialInfo.FresnelR0;
	material.Properties.Emissive = materialInfo.Emissive;
	material.Properties.Metallic = materialInfo.Metallic;
	material.Properties.Roughness = materialInfo.Roughness;
	material.Properties.Opacity = std::clamp(materialInfo.Opacity, 0.0f, 1.0f);
	material.Properties.UseDiffuseTexture = !materialInfo.DiffuseTexture.Path.empty() ? 1u : 0u;
	material.Properties.UseNormalTexture = !materialInfo.NormalTexture.Path.empty() ? 1u : 0u;
	material.Properties.UseMetallicTexture = !materialInfo.MetallicTexture.Path.empty() ? 1u : 0u;
	material.Properties.UseRoughnessTexture = !materialInfo.RoughnessTexture.Path.empty() ? 1u : 0u;
	material.Properties.UseSpecularTexture = !materialInfo.SpecularTexture.Path.empty() ? 1u : 0u;
	material.NumFramesDirty = SwapChainBufferCount;

	Materials[uniqueMaterialName] = material;
	FreshenMaterialCBs();

	return uniqueMaterialName;
}

std::wstring D3DWindow::CreateColorMaterial(
	const std::wstring& name,
	const DirectX::XMFLOAT4& diffuseColor,
	float roughness,
	float metallic,
	float opacity)
{
	const std::wstring uniqueMaterialName = D3DWindowAssetHelpers::MakeUniqueName(Materials, name.empty() ? L"ColorMaterial" : name);

	Material material;
	material.SetName(uniqueMaterialName);
	material.MatCBIndex = static_cast<int>(Materials.size());
	material.DiffuseTexture = nullptr;
	material.NormalTexture = nullptr;
	material.SpecularTexture = nullptr;
	material.MetallicTexture = nullptr;
	material.RoughnessTexture = nullptr;
	material.OpacityTexture = nullptr;
	material.Properties.DiffuseAlbedo = diffuseColor;
	material.Properties.Metallic = std::clamp(metallic, 0.0f, 1.0f);
	material.Properties.Roughness = std::clamp(roughness, 0.0f, 1.0f);
	material.Properties.Opacity = std::clamp(opacity, 0.0f, 1.0f);
	material.Properties.UseDiffuseTexture = 0u;
	material.Properties.UseNormalTexture = 0u;
	material.Properties.UseMetallicTexture = 0u;
	material.Properties.UseRoughnessTexture = 0u;
	material.Properties.UseSpecularTexture = 0u;
	material.NumFramesDirty = SwapChainBufferCount;

	Materials[uniqueMaterialName] = material;
	FreshenMaterialCBs();
	return uniqueMaterialName;
}
