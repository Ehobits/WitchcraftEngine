#include "WSkinnedMeshFile.h"

#include "WitchcraftXmlValueHelpers.h"

namespace WSkinnedMeshFileDetail
{
	void AppendVertexStreams(pugi::xml_node vertexNode, const Witchcraft::Animation::SkinnedVertex& vertex)
	{
		pugi::xml_node positionNode = vertexNode.append_child(PUGIXML_TEXT("Position"));
		WitchcraftXmlValueHelpers::AppendFloat3Attributes(positionNode, vertex.StaticVertex.Pos);

		pugi::xml_node colorNode = vertexNode.append_child(PUGIXML_TEXT("Color"));
		WitchcraftXmlValueHelpers::AppendFloat4Attributes(colorNode, vertex.StaticVertex.Color);

		pugi::xml_node normalNode = vertexNode.append_child(PUGIXML_TEXT("Normal"));
		WitchcraftXmlValueHelpers::AppendFloat3Attributes(normalNode, vertex.StaticVertex.Normal);

		pugi::xml_node texNode = vertexNode.append_child(PUGIXML_TEXT("TexCoord0"));
		texNode.append_attribute(PUGIXML_TEXT("x")).set_value(vertex.StaticVertex.TexC.x);
		texNode.append_attribute(PUGIXML_TEXT("y")).set_value(vertex.StaticVertex.TexC.y);

		pugi::xml_node tangentNode = vertexNode.append_child(PUGIXML_TEXT("Tangent"));
		WitchcraftXmlValueHelpers::AppendFloat3Attributes(tangentNode, vertex.StaticVertex.Tangent);

		pugi::xml_node bitangentNode = vertexNode.append_child(PUGIXML_TEXT("Bitangent"));
		WitchcraftXmlValueHelpers::AppendFloat3Attributes(bitangentNode, vertex.StaticVertex.Bitangent);

		pugi::xml_node skinNode = vertexNode.append_child(PUGIXML_TEXT("Skinning"));
		for (std::uint32_t influenceIndex = 0; influenceIndex < Witchcraft::Animation::MaxBoneInfluenceCountPerVertex; ++influenceIndex)
		{
			pugi::xml_node influenceNode = skinNode.append_child(PUGIXML_TEXT("Influence"));
			influenceNode.append_attribute(PUGIXML_TEXT("boneIndex")).set_value(vertex.Skinning.BoneIndices[influenceIndex]);
			influenceNode.append_attribute(PUGIXML_TEXT("weight")).set_value(vertex.Skinning.BoneWeights[influenceIndex]);
		}
	}

	void ReadVertexStreams(const pugi::xml_node& vertexNode, Witchcraft::Animation::SkinnedVertex* outVertex)
	{
		if (outVertex == nullptr)
			return;

		const pugi::xml_node positionNode = vertexNode.child(PUGIXML_TEXT("Position"));
		outVertex->StaticVertex.Pos = WitchcraftXmlValueHelpers::ReadFloat3Attributes(positionNode, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));

		const pugi::xml_node colorNode = vertexNode.child(PUGIXML_TEXT("Color"));
		outVertex->StaticVertex.Color = WitchcraftXmlValueHelpers::ReadFloat4Attributes(colorNode, DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));

		const pugi::xml_node normalNode = vertexNode.child(PUGIXML_TEXT("Normal"));
		outVertex->StaticVertex.Normal = WitchcraftXmlValueHelpers::ReadFloat3Attributes(normalNode, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));

		const pugi::xml_node texNode = vertexNode.child(PUGIXML_TEXT("TexCoord0"));
		outVertex->StaticVertex.TexC.x = texNode.attribute(PUGIXML_TEXT("x")).as_float(0.0f);
		outVertex->StaticVertex.TexC.y = texNode.attribute(PUGIXML_TEXT("y")).as_float(0.0f);

		const pugi::xml_node tangentNode = vertexNode.child(PUGIXML_TEXT("Tangent"));
		outVertex->StaticVertex.Tangent = WitchcraftXmlValueHelpers::ReadFloat3Attributes(tangentNode, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));

		const pugi::xml_node bitangentNode = vertexNode.child(PUGIXML_TEXT("Bitangent"));
		outVertex->StaticVertex.Bitangent = WitchcraftXmlValueHelpers::ReadFloat3Attributes(bitangentNode, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));

		const pugi::xml_node skinNode = vertexNode.child(PUGIXML_TEXT("Skinning"));
		std::uint32_t influenceIndex = 0;
		for (pugi::xml_node influenceNode = skinNode.child(PUGIXML_TEXT("Influence"));
			influenceNode && influenceIndex < Witchcraft::Animation::MaxBoneInfluenceCountPerVertex;
			influenceNode = influenceNode.next_sibling(PUGIXML_TEXT("Influence")), ++influenceIndex)
		{
			outVertex->Skinning.BoneIndices[influenceIndex] = influenceNode.attribute(PUGIXML_TEXT("boneIndex")).as_uint(0u);
			outVertex->Skinning.BoneWeights[influenceIndex] = influenceNode.attribute(PUGIXML_TEXT("weight")).as_float(0.0f);
		}

		outVertex->Skinning.Normalize();
	}
}

using namespace WSkinnedMeshFileDetail;

const wchar_t* WSkinnedMeshFile::GetRootNodeName() const
{
	return RootNodeName;
}

void WSkinnedMeshFile::BuildBody(pugi::xml_node root) const
{
	root.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.Name).c_str());
	root.append_attribute(PUGIXML_TEXT("skeleton")).set_value(WitchcraftXmlFileBase::ToXmlString(m_data.SkeletonAssetPath).c_str());

	pugi::xml_node verticesNode = root.append_child(PUGIXML_TEXT("Vertices"));
	for (const Witchcraft::Animation::SkinnedVertex& vertex : m_data.Vertices)
		AppendVertexStreams(verticesNode.append_child(PUGIXML_TEXT("Vertex")), vertex);

	pugi::xml_node indicesNode = root.append_child(PUGIXML_TEXT("Indices"));
	for (std::uint32_t index : m_data.Indices)
		indicesNode.append_child(PUGIXML_TEXT("Index")).append_attribute(PUGIXML_TEXT("value")).set_value(index);

	pugi::xml_node submeshesNode = root.append_child(PUGIXML_TEXT("Submeshes"));
	for (const Witchcraft::Animation::SkinnedSubmeshDesc& submesh : m_data.Submeshes)
	{
		pugi::xml_node submeshNode = submeshesNode.append_child(PUGIXML_TEXT("Submesh"));
		submeshNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(submesh.Name).c_str());
		submeshNode.append_attribute(PUGIXML_TEXT("materialSlot")).set_value(WitchcraftXmlFileBase::ToXmlString(submesh.MaterialSlotName).c_str());
		submeshNode.append_attribute(PUGIXML_TEXT("indexStart")).set_value(submesh.IndexStart);
		submeshNode.append_attribute(PUGIXML_TEXT("indexCount")).set_value(submesh.IndexCount);
		submeshNode.append_attribute(PUGIXML_TEXT("baseVertex")).set_value(submesh.BaseVertex);
	}
}

bool WSkinnedMeshFile::ReadBody(const pugi::xml_node& root)
{
	WSkinnedMeshFileData data;
	data.Name = WitchcraftXmlFileBase::FromXmlString(root.attribute(PUGIXML_TEXT("name")).as_string());
	data.SkeletonAssetPath = WitchcraftXmlFileBase::FromXmlString(root.attribute(PUGIXML_TEXT("skeleton")).as_string());

	const pugi::xml_node verticesNode = root.child(PUGIXML_TEXT("Vertices"));
	for (pugi::xml_node vertexNode = verticesNode.child(PUGIXML_TEXT("Vertex")); vertexNode; vertexNode = vertexNode.next_sibling(PUGIXML_TEXT("Vertex")))
	{
		Witchcraft::Animation::SkinnedVertex vertex;
		ReadVertexStreams(vertexNode, &vertex);
		data.Vertices.push_back(std::move(vertex));
	}

	const pugi::xml_node indicesNode = root.child(PUGIXML_TEXT("Indices"));
	for (pugi::xml_node indexNode = indicesNode.child(PUGIXML_TEXT("Index")); indexNode; indexNode = indexNode.next_sibling(PUGIXML_TEXT("Index")))
		data.Indices.push_back(indexNode.attribute(PUGIXML_TEXT("value")).as_uint(0u));

	const pugi::xml_node submeshesNode = root.child(PUGIXML_TEXT("Submeshes"));
	for (pugi::xml_node submeshNode = submeshesNode.child(PUGIXML_TEXT("Submesh")); submeshNode; submeshNode = submeshNode.next_sibling(PUGIXML_TEXT("Submesh")))
	{
		Witchcraft::Animation::SkinnedSubmeshDesc submesh;
		submesh.Name = WitchcraftXmlFileBase::FromXmlString(submeshNode.attribute(PUGIXML_TEXT("name")).as_string());
		submesh.MaterialSlotName = WitchcraftXmlFileBase::FromXmlString(submeshNode.attribute(PUGIXML_TEXT("materialSlot")).as_string());
		submesh.IndexStart = submeshNode.attribute(PUGIXML_TEXT("indexStart")).as_uint(0u);
		submesh.IndexCount = submeshNode.attribute(PUGIXML_TEXT("indexCount")).as_uint(0u);
		submesh.BaseVertex = submeshNode.attribute(PUGIXML_TEXT("baseVertex")).as_uint(0u);
		data.Submeshes.push_back(std::move(submesh));
	}

	m_data = std::move(data);
	return true;
}

std::wstring WSkinnedMeshFile::SerializeToText(const WSkinnedMeshFileData& data)
{
	WSkinnedMeshFile file;
	file.m_data = data;
	return file.SerializeDocumentToText();
}

bool WSkinnedMeshFile::DeserializeFromText(const std::wstring& text, WSkinnedMeshFileData* outData)
{
	if (outData == nullptr)
		return false;

	WSkinnedMeshFile file;
	if (!file.DeserializeDocumentFromText(text))
		return false;

	*outData = std::move(file.m_data);
	return true;
}

bool WSkinnedMeshFile::SaveToFile(const std::filesystem::path& path, const WSkinnedMeshFileData& data)
{
	WSkinnedMeshFile file;
	file.m_data = data;
	return file.SaveDocumentToFile(path);
}

bool WSkinnedMeshFile::LoadFromFile(const std::filesystem::path& path, WSkinnedMeshFileData* outData)
{
	if (outData == nullptr)
		return false;

	WSkinnedMeshFile file;
	if (!file.LoadDocumentFromFile(path))
		return false;

	*outData = std::move(file.m_data);
	return true;
}
