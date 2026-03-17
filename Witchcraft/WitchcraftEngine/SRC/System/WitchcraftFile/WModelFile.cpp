#include "WModelFile.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "pugixml.hpp"
#include "String/SStringUtils.h"

namespace
{
	using XmlString = pugi::string_t;

	std::wstring Trim(const std::wstring& value)
	{
		const size_t begin = value.find_first_not_of(L" \t\r\n");
		if (begin == std::wstring::npos)
			return L"";

		const size_t end = value.find_last_not_of(L" \t\r\n");
		return value.substr(begin, end - begin + 1);
	}

	XmlString ToXmlString(const std::wstring& value)
	{
#ifdef PUGIXML_WCHAR_MODE
		return value;
#else
		return SString::WstringToUTF8(value);
#endif
	}

	std::wstring FromXmlString(const pugi::char_t* value)
	{
		if (value == nullptr)
			return L"";

#ifdef PUGIXML_WCHAR_MODE
		return value;
#else
		return SString::UTF8ToWstring(value);
#endif
	}

	const pugi::char_t* ToNodeTypeText(WModelNodeType type)
	{
		return type == WModelNodeType::Mesh ? PUGIXML_TEXT("Mesh") : PUGIXML_TEXT("Empty");
	}

	WModelNodeType ParseNodeType(const std::wstring& value)
	{
		return _wcsicmp(value.c_str(), L"Mesh") == 0 ? WModelNodeType::Mesh : WModelNodeType::Empty;
	}

	void AppendTextNode(pugi::xml_node parent, const pugi::char_t* nodeName, const std::wstring& value)
	{
		const XmlString xmlValue = ToXmlString(value);
		parent.append_child(nodeName).text().set(xmlValue.c_str());
	}

	void AppendVector3Node(pugi::xml_node parent, const pugi::char_t* nodeName, const DirectX::XMFLOAT3& value)
	{
		pugi::xml_node node = parent.append_child(nodeName);
		node.append_attribute(PUGIXML_TEXT("x")).set_value(value.x);
		node.append_attribute(PUGIXML_TEXT("y")).set_value(value.y);
		node.append_attribute(PUGIXML_TEXT("z")).set_value(value.z);
	}

	void AppendVector4Node(pugi::xml_node parent, const pugi::char_t* nodeName, const DirectX::XMFLOAT4& value)
	{
		pugi::xml_node node = parent.append_child(nodeName);
		node.append_attribute(PUGIXML_TEXT("x")).set_value(value.x);
		node.append_attribute(PUGIXML_TEXT("y")).set_value(value.y);
		node.append_attribute(PUGIXML_TEXT("z")).set_value(value.z);
		node.append_attribute(PUGIXML_TEXT("w")).set_value(value.w);
	}

	bool TryReadVector3Node(const pugi::xml_node& parent, const pugi::char_t* nodeName, DirectX::XMFLOAT3* outValue)
	{
		if (outValue == nullptr)
			return false;

		const pugi::xml_node node = parent.child(nodeName);
		if (!node)
			return false;

		outValue->x = node.attribute(PUGIXML_TEXT("x")).as_float(outValue->x);
		outValue->y = node.attribute(PUGIXML_TEXT("y")).as_float(outValue->y);
		outValue->z = node.attribute(PUGIXML_TEXT("z")).as_float(outValue->z);
		return true;
	}

	void AppendTransformNode(pugi::xml_node parent, const Transform& transform)
	{
		pugi::xml_node transformNode = parent.append_child(PUGIXML_TEXT("Transform"));
		AppendVector3Node(transformNode, PUGIXML_TEXT("Position"), transform.position);
		AppendVector3Node(transformNode, PUGIXML_TEXT("Rotation"), transform.rotation);
		AppendVector3Node(transformNode, PUGIXML_TEXT("Scale"), transform.scale);
	}

	void ReadTransformNode(const pugi::xml_node& parent, Transform* outTransform)
	{
		if (outTransform == nullptr)
			return;

		const pugi::xml_node transformNode = parent.child(PUGIXML_TEXT("Transform"));
		if (!transformNode)
			return;

		TryReadVector3Node(transformNode, PUGIXML_TEXT("Position"), &outTransform->position);
		TryReadVector3Node(transformNode, PUGIXML_TEXT("Rotation"), &outTransform->rotation);
		TryReadVector3Node(transformNode, PUGIXML_TEXT("Scale"), &outTransform->scale);
	}

	template<typename TValue>
	XmlString SerializeScalarArray(const std::vector<TValue>& values)
	{
		std::wostringstream stream;
		stream << std::setprecision(9);
		for (size_t index = 0; index < values.size(); ++index)
		{
			if (index > 0)
				stream << L' ';
			stream << values[index];
		}

		return ToXmlString(stream.str());
	}

	std::vector<float> ParseFloatArray(const std::wstring& text)
	{
		std::vector<float> values;
		std::wstringstream stream(text);
		float value = 0.0f;
		while (stream >> value)
			values.push_back(value);
		return values;
	}

	std::vector<std::uint32_t> ParseIndexArray(const std::wstring& text)
	{
		std::vector<std::uint32_t> values;
		std::wstringstream stream(text);
		std::uint32_t value = 0;
		while (stream >> value)
			values.push_back(value);
		return values;
	}

	bool ApplyFloat3Stream(const std::vector<float>& values, std::vector<Vertex>* vertices, DirectX::XMFLOAT3 Vertex::* member)
	{
		if (vertices == nullptr)
			return false;

		if (values.empty())
			return true;

		if (values.size() != vertices->size() * 3ull)
			return false;

		for (size_t index = 0; index < vertices->size(); ++index)
		{
			((*vertices)[index].*member).x = values[index * 3 + 0];
			((*vertices)[index].*member).y = values[index * 3 + 1];
			((*vertices)[index].*member).z = values[index * 3 + 2];
		}

		return true;
	}

	bool ApplyFloat2Stream(const std::vector<float>& values, std::vector<Vertex>* vertices, DirectX::XMFLOAT2 Vertex::* member)
	{
		if (vertices == nullptr)
			return false;

		if (values.empty())
			return true;

		if (values.size() != vertices->size() * 2ull)
			return false;

		for (size_t index = 0; index < vertices->size(); ++index)
		{
			((*vertices)[index].*member).x = values[index * 2 + 0];
			((*vertices)[index].*member).y = values[index * 2 + 1];
		}

		return true;
	}

	bool ApplyFloat4Stream(const std::vector<float>& values, std::vector<Vertex>* vertices, DirectX::XMFLOAT4 Vertex::* member)
	{
		if (vertices == nullptr)
			return false;

		if (values.empty())
			return true;

		if (values.size() != vertices->size() * 4ull)
			return false;

		for (size_t index = 0; index < vertices->size(); ++index)
		{
			((*vertices)[index].*member).x = values[index * 4 + 0];
			((*vertices)[index].*member).y = values[index * 4 + 1];
			((*vertices)[index].*member).z = values[index * 4 + 2];
			((*vertices)[index].*member).w = values[index * 4 + 3];
		}

		return true;
	}

	void AppendScalarStreamNode(pugi::xml_node parent, const pugi::char_t* nodeName, const XmlString& value)
	{
		parent.append_child(nodeName).text().set(value.c_str());
	}

	void AppendMeshNode(pugi::xml_node parent, const WModelMeshData& meshData)
	{
		pugi::xml_node meshNode = parent.append_child(PUGIXML_TEXT("Mesh"));
		meshNode.append_attribute(PUGIXML_TEXT("id")).set_value(ToXmlString(meshData.Id).c_str());
		meshNode.append_attribute(PUGIXML_TEXT("name")).set_value(ToXmlString(meshData.Name).c_str());

		std::vector<float> positions;
		std::vector<float> colors;
		std::vector<float> normals;
		std::vector<float> texCoords;
		std::vector<float> tangents;
		std::vector<float> bitangents;

		positions.reserve(meshData.Vertices.size() * 3ull);
		colors.reserve(meshData.Vertices.size() * 4ull);
		normals.reserve(meshData.Vertices.size() * 3ull);
		texCoords.reserve(meshData.Vertices.size() * 2ull);
		tangents.reserve(meshData.Vertices.size() * 3ull);
		bitangents.reserve(meshData.Vertices.size() * 3ull);

		for (const Vertex& vertex : meshData.Vertices)
		{
			positions.push_back(vertex.Pos.x);
			positions.push_back(vertex.Pos.y);
			positions.push_back(vertex.Pos.z);

			colors.push_back(vertex.Color.x);
			colors.push_back(vertex.Color.y);
			colors.push_back(vertex.Color.z);
			colors.push_back(vertex.Color.w);

			normals.push_back(vertex.Normal.x);
			normals.push_back(vertex.Normal.y);
			normals.push_back(vertex.Normal.z);

			texCoords.push_back(vertex.TexC.x);
			texCoords.push_back(vertex.TexC.y);

			tangents.push_back(vertex.Tangent.x);
			tangents.push_back(vertex.Tangent.y);
			tangents.push_back(vertex.Tangent.z);

			bitangents.push_back(vertex.Bitangent.x);
			bitangents.push_back(vertex.Bitangent.y);
			bitangents.push_back(vertex.Bitangent.z);
		}

		pugi::xml_node streamsNode = meshNode.append_child(PUGIXML_TEXT("VertexStreams"));
		streamsNode.append_attribute(PUGIXML_TEXT("vertexCount")).set_value(static_cast<unsigned int>(meshData.Vertices.size()));
		AppendScalarStreamNode(streamsNode, PUGIXML_TEXT("Positions"), SerializeScalarArray(positions));
		AppendScalarStreamNode(streamsNode, PUGIXML_TEXT("Colors"), SerializeScalarArray(colors));
		AppendScalarStreamNode(streamsNode, PUGIXML_TEXT("Normals"), SerializeScalarArray(normals));
		AppendScalarStreamNode(streamsNode, PUGIXML_TEXT("TexCoords0"), SerializeScalarArray(texCoords));
		AppendScalarStreamNode(streamsNode, PUGIXML_TEXT("Tangents"), SerializeScalarArray(tangents));
		AppendScalarStreamNode(streamsNode, PUGIXML_TEXT("Bitangents"), SerializeScalarArray(bitangents));

		pugi::xml_node indicesNode = meshNode.append_child(PUGIXML_TEXT("Indices"));
		indicesNode.append_attribute(PUGIXML_TEXT("indexCount")).set_value(static_cast<unsigned int>(meshData.Indices.size()));
		indicesNode.text().set(SerializeScalarArray(meshData.Indices).c_str());
	}

	bool ReadMeshNode(const pugi::xml_node& meshNode, WModelMeshData* outMeshData)
	{
		if (outMeshData == nullptr || !meshNode)
			return false;

		WModelMeshData meshData;
		meshData.Id = FromXmlString(meshNode.attribute(PUGIXML_TEXT("id")).as_string());
		meshData.Name = FromXmlString(meshNode.attribute(PUGIXML_TEXT("name")).as_string());

		const pugi::xml_node streamsNode = meshNode.child(PUGIXML_TEXT("VertexStreams"));
		if (!streamsNode)
			return false;

		const std::vector<float> positions = ParseFloatArray(Trim(FromXmlString(streamsNode.child(PUGIXML_TEXT("Positions")).text().as_string())));
		if (positions.empty() || positions.size() % 3 != 0)
			return false;

		size_t vertexCount = streamsNode.attribute(PUGIXML_TEXT("vertexCount")).as_uint(0);
		if (vertexCount == 0)
			vertexCount = positions.size() / 3;
		if (positions.size() != vertexCount * 3ull)
			return false;

		meshData.Vertices.resize(vertexCount);
		for (Vertex& vertex : meshData.Vertices)
		{
			vertex.Color = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
			vertex.TexC = DirectX::XMFLOAT2(0.0f, 0.0f);
			vertex.Normal = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
			vertex.Tangent = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
			vertex.Bitangent = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		}

		if (!ApplyFloat3Stream(positions, &meshData.Vertices, &Vertex::Pos))
			return false;
		if (!ApplyFloat4Stream(ParseFloatArray(Trim(FromXmlString(streamsNode.child(PUGIXML_TEXT("Colors")).text().as_string()))), &meshData.Vertices, &Vertex::Color))
			return false;
		if (!ApplyFloat3Stream(ParseFloatArray(Trim(FromXmlString(streamsNode.child(PUGIXML_TEXT("Normals")).text().as_string()))), &meshData.Vertices, &Vertex::Normal))
			return false;
		if (!ApplyFloat2Stream(ParseFloatArray(Trim(FromXmlString(streamsNode.child(PUGIXML_TEXT("TexCoords0")).text().as_string()))), &meshData.Vertices, &Vertex::TexC))
			return false;
		if (!ApplyFloat3Stream(ParseFloatArray(Trim(FromXmlString(streamsNode.child(PUGIXML_TEXT("Tangents")).text().as_string()))), &meshData.Vertices, &Vertex::Tangent))
			return false;
		if (!ApplyFloat3Stream(ParseFloatArray(Trim(FromXmlString(streamsNode.child(PUGIXML_TEXT("Bitangents")).text().as_string()))), &meshData.Vertices, &Vertex::Bitangent))
			return false;

		const pugi::xml_node indicesNode = meshNode.child(PUGIXML_TEXT("Indices"));
		if (indicesNode)
		{
			meshData.Indices = ParseIndexArray(Trim(FromXmlString(indicesNode.text().as_string())));
			const size_t indexCount = indicesNode.attribute(PUGIXML_TEXT("indexCount")).as_uint(0);
			if (indexCount != 0 && indexCount != meshData.Indices.size())
				return false;
		}

		*outMeshData = std::move(meshData);
		return true;
	}

	void AppendNodeData(pugi::xml_node parent, const WModelNodeData& nodeData)
	{
		pugi::xml_node node = parent.append_child(PUGIXML_TEXT("Node"));
		node.append_attribute(PUGIXML_TEXT("id")).set_value(ToXmlString(nodeData.Id).c_str());
		node.append_attribute(PUGIXML_TEXT("name")).set_value(ToXmlString(nodeData.Name).c_str());
		node.append_attribute(PUGIXML_TEXT("type")).set_value(ToNodeTypeText(nodeData.Type));

		AppendTransformNode(node, nodeData.LocalTransform);

		if (!nodeData.MeshRef.empty() || !nodeData.MaterialSlots.empty())
		{
			pugi::xml_node rendererNode = node.append_child(PUGIXML_TEXT("MeshRenderer"));
			rendererNode.append_attribute(PUGIXML_TEXT("meshRef")).set_value(ToXmlString(nodeData.MeshRef).c_str());

			if (!nodeData.MaterialSlots.empty())
			{
				pugi::xml_node slotsNode = rendererNode.append_child(PUGIXML_TEXT("MaterialSlots"));
				for (const WModelMaterialSlot& slot : nodeData.MaterialSlots)
				{
					pugi::xml_node slotNode = slotsNode.append_child(PUGIXML_TEXT("Slot"));
					slotNode.append_attribute(PUGIXML_TEXT("index")).set_value(slot.Index);
					slotNode.append_attribute(PUGIXML_TEXT("materialRef")).set_value(ToXmlString(slot.MaterialRef).c_str());
				}
			}
		}

		if (!nodeData.Children.empty())
		{
			pugi::xml_node childrenNode = node.append_child(PUGIXML_TEXT("Children"));
			for (const WModelNodeData& child : nodeData.Children)
				AppendNodeData(childrenNode, child);
		}
	}

	bool ReadNodeData(const pugi::xml_node& node, WModelNodeData* outNodeData)
	{
		if (outNodeData == nullptr || !node)
			return false;

		WModelNodeData nodeData;
		nodeData.Id = FromXmlString(node.attribute(PUGIXML_TEXT("id")).as_string());
		nodeData.Name = FromXmlString(node.attribute(PUGIXML_TEXT("name")).as_string());
		nodeData.Type = ParseNodeType(FromXmlString(node.attribute(PUGIXML_TEXT("type")).as_string()));

		ReadTransformNode(node, &nodeData.LocalTransform);

		const pugi::xml_node rendererNode = node.child(PUGIXML_TEXT("MeshRenderer"));
		if (rendererNode)
		{
			nodeData.MeshRef = FromXmlString(rendererNode.attribute(PUGIXML_TEXT("meshRef")).as_string());

			const pugi::xml_node slotsNode = rendererNode.child(PUGIXML_TEXT("MaterialSlots"));
			for (pugi::xml_node slotNode = slotsNode.child(PUGIXML_TEXT("Slot")); slotNode; slotNode = slotNode.next_sibling(PUGIXML_TEXT("Slot")))
			{
				WModelMaterialSlot slot;
				slot.Index = slotNode.attribute(PUGIXML_TEXT("index")).as_uint();
				slot.MaterialRef = FromXmlString(slotNode.attribute(PUGIXML_TEXT("materialRef")).as_string());
				nodeData.MaterialSlots.push_back(std::move(slot));
			}
		}

		const pugi::xml_node childrenNode = node.child(PUGIXML_TEXT("Children"));
		for (pugi::xml_node childNode = childrenNode.child(PUGIXML_TEXT("Node")); childNode; childNode = childNode.next_sibling(PUGIXML_TEXT("Node")))
		{
			WModelNodeData childData;
			if (!ReadNodeData(childNode, &childData))
				return false;
			nodeData.Children.push_back(std::move(childData));
		}

		*outNodeData = std::move(nodeData);
		return true;
	}

	void BuildXmlDocument(const WModelFileData& data, pugi::xml_document* outDocument)
	{
		if (outDocument == nullptr)
			return;

		outDocument->reset();

		pugi::xml_node declaration = outDocument->append_child(pugi::node_declaration);
		declaration.append_attribute(PUGIXML_TEXT("version")) = PUGIXML_TEXT("1.0");
		declaration.append_attribute(PUGIXML_TEXT("encoding")) = PUGIXML_TEXT("utf-8");

		pugi::xml_node root = outDocument->append_child(WModelFile::RootNodeName);
		root.append_attribute(PUGIXML_TEXT("version")) = PUGIXML_TEXT("1");

		pugi::xml_node metaNode = root.append_child(PUGIXML_TEXT("Meta"));
		AppendTextNode(metaNode, PUGIXML_TEXT("Name"), data.Name);
		AppendTextNode(metaNode, PUGIXML_TEXT("SourceFile"), data.SourceFile);

		pugi::xml_node materialsNode = root.append_child(PUGIXML_TEXT("Materials"));
		for (const WModelMaterialRef& material : data.Materials)
		{
			pugi::xml_node materialNode = materialsNode.append_child(PUGIXML_TEXT("Material"));
			materialNode.append_attribute(PUGIXML_TEXT("id")).set_value(ToXmlString(material.Id).c_str());
			materialNode.append_attribute(PUGIXML_TEXT("file")).set_value(ToXmlString(material.File).c_str());
		}

		pugi::xml_node meshesNode = root.append_child(PUGIXML_TEXT("Meshes"));
		for (const WModelMeshData& mesh : data.Meshes)
			AppendMeshNode(meshesNode, mesh);

		pugi::xml_node hierarchyNode = root.append_child(PUGIXML_TEXT("Hierarchy"));
		AppendNodeData(hierarchyNode, data.RootNode);
	}

	bool DeserializeXmlDocument(const pugi::xml_document& document, WModelFileData* outData)
	{
		if (outData == nullptr)
			return false;

		const pugi::xml_node root = document.child(WModelFile::RootNodeName);
		if (!root)
			return false;

		WModelFileData data;

		const pugi::xml_node metaNode = root.child(PUGIXML_TEXT("Meta"));
		if (metaNode)
		{
			data.Name = FromXmlString(metaNode.child(PUGIXML_TEXT("Name")).text().as_string());
			data.SourceFile = FromXmlString(metaNode.child(PUGIXML_TEXT("SourceFile")).text().as_string());
		}

		const pugi::xml_node materialsNode = root.child(PUGIXML_TEXT("Materials"));
		for (pugi::xml_node materialNode = materialsNode.child(PUGIXML_TEXT("Material")); materialNode; materialNode = materialNode.next_sibling(PUGIXML_TEXT("Material")))
		{
			WModelMaterialRef materialRef;
			materialRef.Id = FromXmlString(materialNode.attribute(PUGIXML_TEXT("id")).as_string());
			materialRef.File = FromXmlString(materialNode.attribute(PUGIXML_TEXT("file")).as_string());
			data.Materials.push_back(std::move(materialRef));
		}

		const pugi::xml_node meshesNode = root.child(PUGIXML_TEXT("Meshes"));
		for (pugi::xml_node meshNode = meshesNode.child(PUGIXML_TEXT("Mesh")); meshNode; meshNode = meshNode.next_sibling(PUGIXML_TEXT("Mesh")))
		{
			WModelMeshData meshData;
			if (!ReadMeshNode(meshNode, &meshData))
				return false;
			data.Meshes.push_back(std::move(meshData));
		}

		const pugi::xml_node hierarchyNode = root.child(PUGIXML_TEXT("Hierarchy"));
		const pugi::xml_node rootNode = hierarchyNode.child(PUGIXML_TEXT("Node"));
		if (!ReadNodeData(rootNode, &data.RootNode))
			return false;

		*outData = std::move(data);
		return true;
	}
}

std::wstring WModelFile::SerializeToText(const WModelFileData& data)
{
	pugi::xml_document document;
	BuildXmlDocument(data, &document);

	std::ostringstream output;
	document.save(output, PUGIXML_TEXT("  "), pugi::format_default, pugi::encoding_utf8);
	return SString::UTF8ToWstring(output.str());
}

bool WModelFile::DeserializeFromText(const std::wstring& text, WModelFileData* outData)
{
	if (outData == nullptr)
		return false;

	const std::wstring trimmedText = Trim(text);
	if (trimmedText.empty() || trimmedText[0] != L'<')
		return false;

	pugi::xml_document document;
	const XmlString xmlText = ToXmlString(trimmedText);
	const pugi::xml_parse_result result = document.load_string(xmlText.c_str(), pugi::parse_default);
	if (!result)
		return false;

	return DeserializeXmlDocument(document, outData);
}

bool WModelFile::SaveToFile(const std::filesystem::path& path, const WModelFileData& data)
{
	if (!path.parent_path().empty())
		std::filesystem::create_directories(path.parent_path());

	pugi::xml_document document;
	BuildXmlDocument(data, &document);
	return document.save_file(path.c_str(), PUGIXML_TEXT("  "), pugi::format_default, pugi::encoding_utf8);
}

bool WModelFile::LoadFromFile(const std::filesystem::path& path, WModelFileData* outData)
{
	if (outData == nullptr || !std::filesystem::exists(path))
		return false;

	pugi::xml_document document;
	const pugi::xml_parse_result result = document.load_file(path.c_str(), pugi::parse_default, pugi::encoding_utf8);
	if (!result)
		return false;

	return DeserializeXmlDocument(document, outData);
}
