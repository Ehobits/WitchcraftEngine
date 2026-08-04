#include "WModelRuntimeHelpers.h"

#include "WSkeletonFile.h"

#include <cstddef>
#include <cmath>
#include <utility>

namespace Witchcraft::WModelRuntime
{
	bool CanCollapseLeadingWrapperNode(const WModelNodeData& nodeData)
	{
		return nodeData.Type == WModelNodeType::Empty &&
			nodeData.MeshRef.empty() &&
			nodeData.MaterialSlots.empty() &&
			nodeData.Children.size() == 1;
	}

	Transform CombineWModelNodeTransformsForWrapperCollapse(const Transform& parentTransform, const Transform& localTransform)
	{
		Transform combined = localTransform;
		combined.position.x += parentTransform.position.x;
		combined.position.y += parentTransform.position.y;
		combined.position.z += parentTransform.position.z;
		combined.rotation.x += parentTransform.rotation.x;
		combined.rotation.y += parentTransform.rotation.y;
		combined.rotation.z += parentTransform.rotation.z;
		combined.scale.x *= parentTransform.scale.x;
		combined.scale.y *= parentTransform.scale.y;
		combined.scale.z *= parentTransform.scale.z;
		return combined;
	}

	bool IsNearlyEqual(float a, float b, float epsilon = 0.0001f)
	{
		return std::abs(a - b) <= epsilon;
	}

	bool IsIdentityWModelTransform(const Transform& transform)
	{
		return
			IsNearlyEqual(transform.position.x, 0.0f) &&
			IsNearlyEqual(transform.position.y, 0.0f) &&
			IsNearlyEqual(transform.position.z, 0.0f) &&
			IsNearlyEqual(transform.rotation.x, 0.0f) &&
			IsNearlyEqual(transform.rotation.y, 0.0f) &&
			IsNearlyEqual(transform.rotation.z, 0.0f) &&
			IsNearlyEqual(transform.scale.x, 1.0f) &&
			IsNearlyEqual(transform.scale.y, 1.0f) &&
			IsNearlyEqual(transform.scale.z, 1.0f);
	}

	std::filesystem::path ResolveWModelReferencedPath(
		const std::filesystem::path& basePath,
		const std::wstring& referencedPath)
	{
		if (referencedPath.empty())
			return {};

		std::filesystem::path resolvedPath(referencedPath);
		if (resolvedPath.is_relative())
			resolvedPath = basePath / resolvedPath;

		return resolvedPath.lexically_normal();
	}

	std::filesystem::path BuildWModelSkeletonAssetPath(
		const std::filesystem::path& modelFilePath,
		const WModelFileData& modelFileData)
	{
		if (!modelFileData.SkeletonAsset.empty())
			return ResolveWModelReferencedPath(modelFilePath.parent_path(), modelFileData.SkeletonAsset);

		return (modelFilePath.parent_path() / L"Animation" / (modelFilePath.stem().wstring() + WSkeletonFile::Extension)).lexically_normal();
	}

	const WModelNodeData* FindWModelNodeById(
		const WModelNodeData& rootNodeData,
		const std::wstring& nodeId)
	{
		if (!nodeId.empty() && rootNodeData.Id == nodeId)
			return &rootNodeData;

		for (const WModelNodeData& childNode : rootNodeData.Children)
		{
			if (const WModelNodeData* foundNode = FindWModelNodeById(childNode, nodeId))
				return foundNode;
		}

		return nullptr;
	}

	const WModelNodeData* FindFirstWModelMeshDescendant(
		const WModelNodeData& rootNodeData)
	{
		if (rootNodeData.Type == WModelNodeType::Mesh && !rootNodeData.MeshRef.empty())
			return &rootNodeData;

		for (const WModelNodeData& childNode : rootNodeData.Children)
		{
			if (const WModelNodeData* foundNode = FindFirstWModelMeshDescendant(childNode))
				return foundNode;
		}

		return nullptr;
	}

	WModelNodeData CollapseLeadingWModelWrapperNodes(
		WModelNodeData rootNodeData)
	{
		while (CanCollapseLeadingWrapperNode(rootNodeData))
		{
			WModelNodeData childNode = std::move(rootNodeData.Children.front());
			childNode.LocalTransform = CombineWModelNodeTransformsForWrapperCollapse(rootNodeData.LocalTransform, childNode.LocalTransform);

			if (childNode.Name.empty())
				childNode.Name = rootNodeData.Name;
			if (childNode.Id.empty())
				childNode.Id = rootNodeData.Id;

			rootNodeData = std::move(childNode);
		}

		return rootNodeData;
	}

	bool LoadWModelRuntimeAsset(
		const std::filesystem::path& modelFilePath,
		WModelRuntimeAsset* outAsset)
	{
		if (outAsset == nullptr)
			return false;

		WModelFileData modelFileData;
		const std::filesystem::path normalizedModelPath = modelFilePath.lexically_normal();
		if (!WModelFile::LoadFromFile(normalizedModelPath, &modelFileData))
			return false;

		*outAsset = {};
		outAsset->ModelPath = normalizedModelPath;
		outAsset->ModelDirectory = normalizedModelPath.parent_path();
		outAsset->Data = std::move(modelFileData);

		for (const WModelMeshData& meshData : outAsset->Data.Meshes)
		{
			if (!meshData.Id.empty())
				outAsset->MeshesById[meshData.Id] = &meshData;
		}

		outAsset->SkeletonAssetPath = BuildWModelSkeletonAssetPath(outAsset->ModelPath, outAsset->Data);

		return true;
	}

	bool BuildWModelMeshPayload(
		const WModelRuntimeAsset& asset,
		const WModelNodeData& nodeData,
		bool allowAbsorbIdentityMeshChildren,
		const DirectX::XMFLOAT4* overrideVertexColor,
		WModelMeshPayload* outPayload)
	{
		if (outPayload == nullptr)
			return false;

		*outPayload = {};

		auto appendMeshData = [&](const WModelMeshData* sourceMeshData)
		{
			if (sourceMeshData == nullptr)
				return;

			const std::uint32_t baseVertex = static_cast<std::uint32_t>(outPayload->Vertices.size());
			outPayload->Vertices.insert(
				outPayload->Vertices.end(),
				sourceMeshData->Vertices.begin(),
				sourceMeshData->Vertices.end());
			if (overrideVertexColor != nullptr)
			{
				for (size_t vertexIndex = baseVertex; vertexIndex < outPayload->Vertices.size(); ++vertexIndex)
					outPayload->Vertices[vertexIndex].Color = *overrideVertexColor;
			}

			for (std::uint32_t index : sourceMeshData->Indices)
				outPayload->Indices.push_back(baseVertex + index);

			const bool sourceHasSkinning =
				!sourceMeshData->Skinning.empty() &&
				sourceMeshData->Skinning.size() == sourceMeshData->Vertices.size();
			if (sourceHasSkinning)
			{
				outPayload->ContainsSkinning = true;
				outPayload->Skinning.insert(
					outPayload->Skinning.end(),
					sourceMeshData->Skinning.begin(),
					sourceMeshData->Skinning.end());
			}
			else
			{
				outPayload->ContainsNonSkinned = true;
			}
		};

		if (nodeData.Type == WModelNodeType::Mesh && !nodeData.MeshRef.empty())
		{
			const auto meshIt = asset.MeshesById.find(nodeData.MeshRef);
			if (meshIt != asset.MeshesById.end() && meshIt->second != nullptr)
			{
				appendMeshData(meshIt->second);
				if (!nodeData.MaterialSlots.empty())
					outPayload->PrimaryMaterialRef = nodeData.MaterialSlots[0].MaterialRef;
			}
		}
		else if (allowAbsorbIdentityMeshChildren && nodeData.Type == WModelNodeType::Empty)
		{
			std::wstring absorbedMaterialRef;
			bool absorbedMaterialRefInitialized = false;
			bool hasMaterialMismatch = false;
			std::vector<const WModelNodeData*> absorbedMeshChildren;

			for (const WModelNodeData& childNode : nodeData.Children)
			{
				if (childNode.Type != WModelNodeType::Mesh || childNode.MeshRef.empty())
					continue;
				if (!IsIdentityWModelTransform(childNode.LocalTransform))
					continue;

				const auto childMeshIt = asset.MeshesById.find(childNode.MeshRef);
				if (childMeshIt == asset.MeshesById.end() || childMeshIt->second == nullptr)
					continue;

				// 仅当可吸收子网格材质一致时才允许“挂到父级”。
				// 若材质/贴图不同，需要保留为独立子实体，避免父级单材质渲染错误。
				const std::wstring childMaterialRef =
					!childNode.MaterialSlots.empty() ? childNode.MaterialSlots[0].MaterialRef : L"";
				if (!absorbedMaterialRefInitialized)
				{
					absorbedMaterialRef = childMaterialRef;
					absorbedMaterialRefInitialized = true;
				}
				else if (absorbedMaterialRef != childMaterialRef)
				{
					hasMaterialMismatch = true;
					break;
				}

				absorbedMeshChildren.push_back(&childNode);
			}

			if (!hasMaterialMismatch)
			{
				for (const WModelNodeData* absorbedChildNode : absorbedMeshChildren)
				{
					const auto childMeshIt = asset.MeshesById.find(absorbedChildNode->MeshRef);
					if (childMeshIt == asset.MeshesById.end() || childMeshIt->second == nullptr)
						continue;

					appendMeshData(childMeshIt->second);
				}

				outPayload->AbsorbedMeshChildren = std::move(absorbedMeshChildren);
				outPayload->PrimaryMaterialRef = absorbedMaterialRef;
			}
		}

		outPayload->HasCompleteSkinning =
			outPayload->ContainsSkinning &&
			!outPayload->ContainsNonSkinned &&
			outPayload->Skinning.size() == outPayload->Vertices.size();

		return !outPayload->Vertices.empty() && !outPayload->Indices.empty();
	}
}
