#include "WModelFile.h"

#include "WitchcraftXmlValueHelpers.h"

#include <cwchar>
#include <iomanip>
#include <sstream>

namespace WModelFileDetail
{
	// 节点类型在文件里直接保存为短文本，便于调试和人工检查。
	const pugi::char_t* ToNodeTypeText(WModelNodeType type)
	{
		return type == WModelNodeType::Mesh ? PUGIXML_TEXT("Mesh") : PUGIXML_TEXT("Empty");
	}

	WModelNodeType ParseNodeType(const std::wstring& value)
	{
		return _wcsicmp(value.c_str(), L"Mesh") == 0 ? WModelNodeType::Mesh : WModelNodeType::Empty;
	}

	void AppendTransformNode(pugi::xml_node parent, const Transform& transform)
	{
		pugi::xml_node transformNode = parent.append_child(PUGIXML_TEXT("Transform"));
		WitchcraftXmlValueHelpers::AppendFloat3ChildNode(transformNode, PUGIXML_TEXT("Position"), transform.position);
		WitchcraftXmlValueHelpers::AppendFloat3ChildNode(transformNode, PUGIXML_TEXT("Rotation"), transform.rotation);
		WitchcraftXmlValueHelpers::AppendFloat3ChildNode(transformNode, PUGIXML_TEXT("Scale"), transform.scale);
	}

	void ReadTransformNode(const pugi::xml_node& parent, Transform* outTransform)
	{
		if (outTransform == nullptr)
			return;

		const pugi::xml_node transformNode = parent.child(PUGIXML_TEXT("Transform"));
		if (!transformNode)
			return;

		WitchcraftXmlValueHelpers::TryReadFloat3ChildNode(transformNode, PUGIXML_TEXT("Position"), &outTransform->position);
		WitchcraftXmlValueHelpers::TryReadFloat3ChildNode(transformNode, PUGIXML_TEXT("Rotation"), &outTransform->rotation);
		WitchcraftXmlValueHelpers::TryReadFloat3ChildNode(transformNode, PUGIXML_TEXT("Scale"), &outTransform->scale);
	}

	template<typename TValue>
	pugi::string_t SerializeScalarArray(const std::vector<TValue>& values)
	{
		std::wostringstream stream;
		stream << std::setprecision(9);
		for (size_t index = 0; index < values.size(); ++index)
		{
			if (index > 0)
				stream << L' ';
			stream << values[index];
		}

		return WitchcraftXmlFileBase::ToXmlString(stream.str());
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

	void AppendScalarStreamNode(pugi::xml_node parent, const pugi::char_t* nodeName, const pugi::string_t& value)
	{
		parent.append_child(nodeName).text().set(value.c_str());
	}

	void AppendMeshSkinningNode(pugi::xml_node parent, const std::vector<Witchcraft::Animation::VertexBoneInfluence4>& skinning)
	{
		if (skinning.empty())
			return;

		pugi::xml_node skinningNode = parent.append_child(PUGIXML_TEXT("Skinning"));
		skinningNode.append_attribute(PUGIXML_TEXT("vertexCount")).set_value(static_cast<unsigned int>(skinning.size()));
		for (const Witchcraft::Animation::VertexBoneInfluence4& influence : skinning)
		{
			pugi::xml_node vertexNode = skinningNode.append_child(PUGIXML_TEXT("Vertex"));
			for (std::uint32_t slotIndex = 0; slotIndex < Witchcraft::Animation::MaxBoneInfluenceCountPerVertex; ++slotIndex)
			{
				pugi::xml_node influenceNode = vertexNode.append_child(PUGIXML_TEXT("Influence"));
				influenceNode.append_attribute(PUGIXML_TEXT("boneIndex")).set_value(influence.BoneIndices[slotIndex]);
				influenceNode.append_attribute(PUGIXML_TEXT("weight")).set_value(influence.BoneWeights[slotIndex]);
			}
		}
	}

	bool ReadMeshSkinningNode(const pugi::xml_node& parent, size_t vertexCount, std::vector<Witchcraft::Animation::VertexBoneInfluence4>* outSkinning)
	{
		if (outSkinning == nullptr)
			return false;

		outSkinning->clear();
		const pugi::xml_node skinningNode = parent.child(PUGIXML_TEXT("Skinning"));
		if (!skinningNode)
			return true;

		const size_t serializedVertexCount = skinningNode.attribute(PUGIXML_TEXT("vertexCount")).as_uint(static_cast<unsigned int>(vertexCount));
		if (serializedVertexCount != vertexCount)
			return false;

		outSkinning->reserve(vertexCount);
		for (pugi::xml_node vertexNode = skinningNode.child(PUGIXML_TEXT("Vertex")); vertexNode; vertexNode = vertexNode.next_sibling(PUGIXML_TEXT("Vertex")))
		{
			Witchcraft::Animation::VertexBoneInfluence4 influence;
			std::uint32_t slotIndex = 0;
			for (pugi::xml_node influenceNode = vertexNode.child(PUGIXML_TEXT("Influence"));
				influenceNode && slotIndex < Witchcraft::Animation::MaxBoneInfluenceCountPerVertex;
				influenceNode = influenceNode.next_sibling(PUGIXML_TEXT("Influence")), ++slotIndex)
			{
				influence.BoneIndices[slotIndex] = influenceNode.attribute(PUGIXML_TEXT("boneIndex")).as_uint(0u);
				influence.BoneWeights[slotIndex] = influenceNode.attribute(PUGIXML_TEXT("weight")).as_float(0.0f);
			}

			influence.Normalize();
			outSkinning->push_back(std::move(influence));
		}

		return outSkinning->size() == vertexCount;
	}

	void AppendSkeletonNode(
		pugi::xml_node parent,
		const std::wstring& skeletonAsset,
		const std::wstring& skeletonName,
		const Witchcraft::Animation::SkeletonTopology& skeleton)
	{
		if (skeletonAsset.empty() && skeletonName.empty() && skeleton.Bones.empty())
			return;

		pugi::xml_node skeletonNode = parent.append_child(PUGIXML_TEXT("Skeleton"));
		skeletonNode.append_attribute(PUGIXML_TEXT("asset")).set_value(WitchcraftXmlFileBase::ToXmlString(skeletonAsset).c_str());
		skeletonNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(skeletonName).c_str());
		skeletonNode.append_attribute(PUGIXML_TEXT("rootBoneIndex")).set_value(skeleton.RootBoneIndex);

		if (!skeleton.Bones.empty())
		{
			pugi::xml_node bonesNode = skeletonNode.append_child(PUGIXML_TEXT("Bones"));
			for (const Witchcraft::Animation::SkeletonBone& bone : skeleton.Bones)
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
	}

	bool ReadSkeletonNode(
		const pugi::xml_node& parent,
		std::wstring* outSkeletonAsset,
		std::wstring* outSkeletonName,
		Witchcraft::Animation::SkeletonTopology* outSkeleton)
	{
		if (outSkeletonAsset == nullptr || outSkeletonName == nullptr || outSkeleton == nullptr)
			return false;

		*outSkeletonAsset = L"";
		*outSkeletonName = L"";
		*outSkeleton = {};

		const pugi::xml_node skeletonNode = parent.child(PUGIXML_TEXT("Skeleton"));
		if (!skeletonNode)
			return true;

		outSkeletonAsset->assign(WitchcraftXmlFileBase::FromXmlString(skeletonNode.attribute(PUGIXML_TEXT("asset")).as_string()));
		outSkeletonName->assign(WitchcraftXmlFileBase::FromXmlString(skeletonNode.attribute(PUGIXML_TEXT("name")).as_string()));
		outSkeleton->RootBoneIndex = skeletonNode.attribute(PUGIXML_TEXT("rootBoneIndex")).as_int(-1);

		const pugi::xml_node bonesNode = skeletonNode.child(PUGIXML_TEXT("Bones"));
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
			outSkeleton->Bones.push_back(std::move(bone));
		}

		outSkeleton->RebuildNameToIndexMap();
		return true;
	}

	void AppendMeshNode(pugi::xml_node parent, const WModelMeshData& meshData)
	{
		// 几何数据采用“分离流”写法：
		// 每种顶点属性单独存一条标量流，避免重复标签膨胀文件体积。
		pugi::xml_node meshNode = parent.append_child(PUGIXML_TEXT("Mesh"));
		meshNode.append_attribute(PUGIXML_TEXT("id")).set_value(WitchcraftXmlFileBase::ToXmlString(meshData.Id).c_str());
		meshNode.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(meshData.Name).c_str());

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
		AppendMeshSkinningNode(meshNode, meshData.Skinning);

		pugi::xml_node indicesNode = meshNode.append_child(PUGIXML_TEXT("Indices"));
		indicesNode.append_attribute(PUGIXML_TEXT("indexCount")).set_value(static_cast<unsigned int>(meshData.Indices.size()));
		indicesNode.text().set(SerializeScalarArray(meshData.Indices).c_str());
	}

	bool ReadMeshNode(const pugi::xml_node& meshNode, WModelMeshData* outMeshData)
	{
		if (outMeshData == nullptr || !meshNode)
			return false;

		WModelMeshData meshData;
		meshData.Id = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("id")).as_string());
		meshData.Name = WitchcraftXmlFileBase::FromXmlString(meshNode.attribute(PUGIXML_TEXT("name")).as_string());

		const pugi::xml_node streamsNode = meshNode.child(PUGIXML_TEXT("VertexStreams"));
		if (!streamsNode)
			return false;

		const std::vector<float> positions = ParseFloatArray(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(streamsNode.child(PUGIXML_TEXT("Positions")).text().as_string())));
		if (positions.empty() || positions.size() % 3 != 0)
			return false;

		size_t vertexCount = streamsNode.attribute(PUGIXML_TEXT("vertexCount")).as_uint(0);
		if (vertexCount == 0)
			vertexCount = positions.size() / 3;
		if (positions.size() != vertexCount * 3ull)
			return false;

		meshData.Vertices.resize(vertexCount);
		// 先补默认值，再按存在的流覆盖，保证缺失流时结构完整。
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
		if (!ApplyFloat4Stream(ParseFloatArray(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(streamsNode.child(PUGIXML_TEXT("Colors")).text().as_string()))), &meshData.Vertices, &Vertex::Color))
			return false;
		if (!ApplyFloat3Stream(ParseFloatArray(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(streamsNode.child(PUGIXML_TEXT("Normals")).text().as_string()))), &meshData.Vertices, &Vertex::Normal))
			return false;
		if (!ApplyFloat2Stream(ParseFloatArray(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(streamsNode.child(PUGIXML_TEXT("TexCoords0")).text().as_string()))), &meshData.Vertices, &Vertex::TexC))
			return false;
		if (!ApplyFloat3Stream(ParseFloatArray(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(streamsNode.child(PUGIXML_TEXT("Tangents")).text().as_string()))), &meshData.Vertices, &Vertex::Tangent))
			return false;
		if (!ApplyFloat3Stream(ParseFloatArray(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(streamsNode.child(PUGIXML_TEXT("Bitangents")).text().as_string()))), &meshData.Vertices, &Vertex::Bitangent))
			return false;
		if (!ReadMeshSkinningNode(meshNode, meshData.Vertices.size(), &meshData.Skinning))
			return false;

		const pugi::xml_node indicesNode = meshNode.child(PUGIXML_TEXT("Indices"));
		if (indicesNode)
		{
			meshData.Indices = ParseIndexArray(WitchcraftXmlFileBase::Trim(WitchcraftXmlFileBase::FromXmlString(indicesNode.text().as_string())));
			const size_t indexCount = indicesNode.attribute(PUGIXML_TEXT("indexCount")).as_uint(0);
			if (indexCount != 0 && indexCount != meshData.Indices.size())
				return false;
		}

		*outMeshData = std::move(meshData);
		return true;
	}

	void AppendNodeData(pugi::xml_node parent, const WModelNodeData& nodeData)
	{
		// 层级节点只保存本地变换；运行时 world transform 由实体树重新组合。
		pugi::xml_node node = parent.append_child(PUGIXML_TEXT("Node"));
		node.append_attribute(PUGIXML_TEXT("id")).set_value(WitchcraftXmlFileBase::ToXmlString(nodeData.Id).c_str());
		node.append_attribute(PUGIXML_TEXT("name")).set_value(WitchcraftXmlFileBase::ToXmlString(nodeData.Name).c_str());
		node.append_attribute(PUGIXML_TEXT("type")).set_value(ToNodeTypeText(nodeData.Type));

		AppendTransformNode(node, nodeData.LocalTransform);

		if (!nodeData.MeshRef.empty() || !nodeData.MaterialSlots.empty())
		{
			pugi::xml_node rendererNode = node.append_child(PUGIXML_TEXT("MeshRenderer"));
			rendererNode.append_attribute(PUGIXML_TEXT("meshRef")).set_value(WitchcraftXmlFileBase::ToXmlString(nodeData.MeshRef).c_str());

			if (!nodeData.MaterialSlots.empty())
			{
				pugi::xml_node slotsNode = rendererNode.append_child(PUGIXML_TEXT("MaterialSlots"));
				for (const WModelMaterialSlot& slot : nodeData.MaterialSlots)
				{
					pugi::xml_node slotNode = slotsNode.append_child(PUGIXML_TEXT("Slot"));
					slotNode.append_attribute(PUGIXML_TEXT("index")).set_value(slot.Index);
					slotNode.append_attribute(PUGIXML_TEXT("materialRef")).set_value(WitchcraftXmlFileBase::ToXmlString(slot.MaterialRef).c_str());
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
		nodeData.Id = WitchcraftXmlFileBase::FromXmlString(node.attribute(PUGIXML_TEXT("id")).as_string());
		nodeData.Name = WitchcraftXmlFileBase::FromXmlString(node.attribute(PUGIXML_TEXT("name")).as_string());
		nodeData.Type = ParseNodeType(WitchcraftXmlFileBase::FromXmlString(node.attribute(PUGIXML_TEXT("type")).as_string()));

		ReadTransformNode(node, &nodeData.LocalTransform);

		const pugi::xml_node rendererNode = node.child(PUGIXML_TEXT("MeshRenderer"));
		if (rendererNode)
		{
			nodeData.MeshRef = WitchcraftXmlFileBase::FromXmlString(rendererNode.attribute(PUGIXML_TEXT("meshRef")).as_string());

			const pugi::xml_node slotsNode = rendererNode.child(PUGIXML_TEXT("MaterialSlots"));
			for (pugi::xml_node slotNode = slotsNode.child(PUGIXML_TEXT("Slot")); slotNode; slotNode = slotNode.next_sibling(PUGIXML_TEXT("Slot")))
			{
				WModelMaterialSlot slot;
				slot.Index = slotNode.attribute(PUGIXML_TEXT("index")).as_uint();
				slot.MaterialRef = WitchcraftXmlFileBase::FromXmlString(slotNode.attribute(PUGIXML_TEXT("materialRef")).as_string());
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
}

using namespace WModelFileDetail;

const wchar_t* WModelFile::GetRootNodeName() const
{
	return RootNodeName;
}

void WModelFile::BuildBody(pugi::xml_node root) const
{
	// wmodel 同时保存：
	// 1. 模型元数据
	// 2. 材质文件引用
	// 3. 网格原始顶点/索引
	// 4. 模型层级结构
	pugi::xml_node metaNode = root.append_child(PUGIXML_TEXT("Meta"));
	WitchcraftXmlFileBase::AppendTextNode(metaNode, PUGIXML_TEXT("Name"), m_data.Name);
	WitchcraftXmlFileBase::AppendTextNode(metaNode, PUGIXML_TEXT("SourceFile"), m_data.SourceFile);

	pugi::xml_node materialsNode = root.append_child(PUGIXML_TEXT("Materials"));
	for (const WModelMaterialRef& material : m_data.Materials)
	{
		pugi::xml_node materialNode = materialsNode.append_child(PUGIXML_TEXT("Material"));
		materialNode.append_attribute(PUGIXML_TEXT("id")).set_value(WitchcraftXmlFileBase::ToXmlString(material.Id).c_str());
		materialNode.append_attribute(PUGIXML_TEXT("file")).set_value(WitchcraftXmlFileBase::ToXmlString(material.File).c_str());
	}

	pugi::xml_node meshesNode = root.append_child(PUGIXML_TEXT("Meshes"));
	for (const WModelMeshData& mesh : m_data.Meshes)
		AppendMeshNode(meshesNode, mesh);

	AppendSkeletonNode(root, m_data.SkeletonAsset, m_data.SkeletonName, m_data.Skeleton);

	pugi::xml_node hierarchyNode = root.append_child(PUGIXML_TEXT("Hierarchy"));
	AppendNodeData(hierarchyNode, m_data.RootNode);
}

bool WModelFile::ReadBody(const pugi::xml_node& root)
{
	// 这里只做文件数据反序列化，不直接创建运行时 GPU 资源。
	WModelFileData data;

	const pugi::xml_node metaNode = root.child(PUGIXML_TEXT("Meta"));
	if (metaNode)
	{
		data.Name = WitchcraftXmlFileBase::FromXmlString(metaNode.child(PUGIXML_TEXT("Name")).text().as_string());
		data.SourceFile = WitchcraftXmlFileBase::FromXmlString(metaNode.child(PUGIXML_TEXT("SourceFile")).text().as_string());
	}

	const pugi::xml_node materialsNode = root.child(PUGIXML_TEXT("Materials"));
	for (pugi::xml_node materialNode = materialsNode.child(PUGIXML_TEXT("Material")); materialNode; materialNode = materialNode.next_sibling(PUGIXML_TEXT("Material")))
	{
		WModelMaterialRef materialRef;
		materialRef.Id = WitchcraftXmlFileBase::FromXmlString(materialNode.attribute(PUGIXML_TEXT("id")).as_string());
		materialRef.File = WitchcraftXmlFileBase::FromXmlString(materialNode.attribute(PUGIXML_TEXT("file")).as_string());
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

	if (!ReadSkeletonNode(root, &data.SkeletonAsset, &data.SkeletonName, &data.Skeleton))
		return false;

	const pugi::xml_node hierarchyNode = root.child(PUGIXML_TEXT("Hierarchy"));
	const pugi::xml_node rootNode = hierarchyNode.child(PUGIXML_TEXT("Node"));
	if (!ReadNodeData(rootNode, &data.RootNode))
		return false;

	m_data = std::move(data);
	return true;
}

std::wstring WModelFile::SerializeToText(const WModelFileData& data)
{
	WModelFile file;
	file.m_data = data;
	return file.SerializeDocumentToText();
}

bool WModelFile::DeserializeFromText(const std::wstring& text, WModelFileData* outData)
{
	if (outData == nullptr)
		return false;

	WModelFile file;
	if (!file.DeserializeDocumentFromText(text))
		return false;

	*outData = std::move(file.m_data);
	return true;
}

bool WModelFile::SaveToFile(const std::filesystem::path& path, const WModelFileData& data)
{
	WModelFile file;
	file.m_data = data;
	return file.SaveDocumentToFile(path);
}

bool WModelFile::LoadFromFile(const std::filesystem::path& path, WModelFileData* outData)
{
	if (outData == nullptr)
		return false;

	WModelFile file;
	if (!file.LoadDocumentFromFile(path))
		return false;

	*outData = std::move(file.m_data);
	return true;
}
