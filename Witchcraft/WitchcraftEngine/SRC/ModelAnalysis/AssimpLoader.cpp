#include "AssimpLoader.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <unordered_map>
#include "Helpers/MathHelpers.h"
#include "ECS/WitchcraECS.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Component/SkinnedMeshComponent.h"
#include "Engine/Engine.h"
#include "D3DWindow/D3DWindow.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"
#include "Editor/Window/ConsoleWindow.h"
#include "System/WitchcraftFile/WMaterialFile.h"
#include "System/WitchcraftFile/WAnimationFile.h"
#include "System/WitchcraftFile/WModelFile.h"
#include "System/WitchcraftFile/WModelRuntimeHelpers.h"
#include "System/WitchcraftFile/WSkeletonFile.h"

namespace AssimpImpl
{
	using namespace MathHelps;

	// ═══════════════════════════════════════════════
	//  动画导入辅助：时间归一化、姿态校验、关键帧压缩
	// ═══════════════════════════════════════════════

	double NormalizeImportedAnimationTime(double time)
	{
		if (!std::isfinite(time))
			return 0.0;

		constexpr double kNearZeroTimeEpsilon = 1.0e-3;
		if (std::abs(time) <= kNearZeroTimeEpsilon)
			return 0.0;

		return (std::max)(0.0, time);
	}

	bool IsDefaultTranslationValue(const DirectX::XMFLOAT3& value)
	{
		return NearlyEqualFloat(value.x, 0.0f) &&
			NearlyEqualFloat(value.y, 0.0f) &&
			NearlyEqualFloat(value.z, 0.0f);
	}

	bool IsDefaultScaleValue(const DirectX::XMFLOAT3& value)
	{
		return NearlyEqualFloat(value.x, 1.0f) &&
			NearlyEqualFloat(value.y, 1.0f) &&
			NearlyEqualFloat(value.z, 1.0f);
	}

	bool TryNormalizeQuaternion(const DirectX::XMFLOAT4& input, DirectX::XMFLOAT4& output)
	{
		if (!IsFiniteFloat4(input))
			return false;

		const float lengthSquared =
			input.x * input.x +
			input.y * input.y +
			input.z * input.z +
			input.w * input.w;
		if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-12f)
			return false;

		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		output = DirectX::XMFLOAT4(
			input.x * inverseLength,
			input.y * inverseLength,
			input.z * inverseLength,
			input.w * inverseLength);
		return IsFiniteFloat4(output);
	}

	void CompressTranslationKeys(std::vector<Witchcraft::Animation::BoneTranslationKey>& keys)
	{
		if (keys.size() <= 1)
			return;

		std::vector<Witchcraft::Animation::BoneTranslationKey> compressed;
		compressed.reserve(keys.size());
		compressed.push_back(keys.front());
		for (size_t keyIndex = 1; keyIndex < keys.size(); ++keyIndex)
		{
			const auto& candidate = keys[keyIndex];
			auto& last = compressed.back();
			if (NearlyEqualFloat(candidate.Time, last.Time, 1e-6f))
			{
				last = candidate;
				continue;
			}
			if (NearlyEqualFloat3(candidate.Value, last.Value))
				continue;
			compressed.push_back(candidate);
		}
		if (!NearlyEqualFloat(compressed.back().Time, keys.back().Time, 1e-6f))
			compressed.push_back(keys.back());
		keys.swap(compressed);
	}

	void CompressRotationKeys(std::vector<Witchcraft::Animation::BoneRotationKey>& keys)
	{
		if (keys.size() <= 1)
			return;

		std::vector<Witchcraft::Animation::BoneRotationKey> compressed;
		compressed.reserve(keys.size());
		compressed.push_back(keys.front());
		for (size_t keyIndex = 1; keyIndex < keys.size(); ++keyIndex)
		{
			auto candidate = keys[keyIndex];
			DirectX::XMFLOAT4 normalizedCandidate = candidate.Value;
			if (!TryNormalizeQuaternion(candidate.Value, normalizedCandidate))
				continue;
			candidate.Value = normalizedCandidate;

			auto& last = compressed.back();
			DirectX::XMFLOAT4 normalizedLast = last.Value;
			if (TryNormalizeQuaternion(last.Value, normalizedLast))
				last.Value = normalizedLast;

			if (NearlyEqualFloat(candidate.Time, last.Time, 1e-6f))
			{
				last = candidate;
				continue;
			}
			if (NearlyEqualFloat4(candidate.Value, last.Value) ||
				NearlyEqualFloat4(candidate.Value, DirectX::XMFLOAT4(-last.Value.x, -last.Value.y, -last.Value.z, -last.Value.w)))
			{
				continue;
			}
			compressed.push_back(candidate);
		}
		if (!NearlyEqualFloat(compressed.back().Time, keys.back().Time, 1e-6f))
			compressed.push_back(keys.back());
		keys.swap(compressed);
	}

	void CompressScaleKeys(std::vector<Witchcraft::Animation::BoneScaleKey>& keys)
	{
		if (keys.size() <= 1)
			return;

		std::vector<Witchcraft::Animation::BoneScaleKey> compressed;
		compressed.reserve(keys.size());
		compressed.push_back(keys.front());
		for (size_t keyIndex = 1; keyIndex < keys.size(); ++keyIndex)
		{
			const auto& candidate = keys[keyIndex];
			auto& last = compressed.back();
			if (NearlyEqualFloat(candidate.Time, last.Time, 1e-6f))
			{
				last = candidate;
				continue;
			}
			if (NearlyEqualFloat3(candidate.Value, last.Value))
				continue;
			compressed.push_back(candidate);
		}
		if (!NearlyEqualFloat(compressed.back().Time, keys.back().Time, 1e-6f))
			compressed.push_back(keys.back());
		keys.swap(compressed);
	}

}

namespace AssimpImpl
{
	using namespace MathHelps;

	// ═══════════════════════════════════════════════
	//  路径与命名辅助
	// ═══════════════════════════════════════════════

	struct AssimpLoaderPathHelpers
	{
		static std::filesystem::path MakeUniqueFilePath(const std::filesystem::path& directory, const std::wstring& preferredFileName)
		{
			const std::filesystem::path preferredPath = directory / preferredFileName;
			if (!std::filesystem::exists(preferredPath))
				return preferredPath;
			const std::filesystem::path preferredStem = std::filesystem::path(preferredFileName).stem();
			const std::filesystem::path preferredExtension = std::filesystem::path(preferredFileName).extension();
			for (UINT suffix = 1; ; ++suffix)
			{
				const std::filesystem::path candidate =
					directory / (preferredStem.wstring() + L"_" + std::to_wstring(suffix) + preferredExtension.wstring());
				if (!std::filesystem::exists(candidate))
					return candidate;
			}
		}

		static std::wstring SanitizeName(std::wstring value)
		{
			if (value.empty()) return L"Imported";
			for (wchar_t& ch : value)
			{
				switch (ch)
				{
				case L'\\': case L'/': case L':': case L'*': case L'?': case L'"': case L'<': case L'>': case L'|':
					ch = L'_'; break;
				default: break;
				}
			}
			return value;
		}

		static std::filesystem::path BuildImportedMaterialFilePath(
			const std::filesystem::path& materialsDir, const std::wstring& modelName, const std::wstring& materialName)
		{
			const std::wstring safeModelName = SanitizeName(modelName.empty() ? L"Model" : modelName);
			const std::wstring safeMaterialName = SanitizeName(materialName.empty() ? L"Material" : materialName);
			return MakeUniqueFilePath(materialsDir, safeModelName + L"_" + safeMaterialName + WMaterialFile::Extension);
		}

		static std::wstring GetSafeTextureFileName(const std::wstring& preferredName, const std::wstring& fallbackStem, const std::wstring& fallbackExtension)
		{
			std::filesystem::path filePath(preferredName);
			const std::wstring fileStem = SanitizeName(filePath.stem().wstring());
			std::wstring fileExtension = filePath.extension().wstring();
			if (!fileStem.empty()) { if (fileExtension.empty()) fileExtension = fallbackExtension; return fileStem + fileExtension; }
			return fallbackStem + fallbackExtension;
		}

		static bool IsEmbeddedTextureToken(const std::wstring& textureReference)
		{
			if (textureReference.empty() || textureReference[0] != L'*') return false;
			for (size_t index = 1; index < textureReference.size(); ++index)
				if (!iswdigit(textureReference[index])) return false;
			return textureReference.size() > 1;
		}

		static std::wstring ResolvePreferredTextureName(
			const aiString& texturePath, const aiTexture* embeddedTexture,
			const std::wstring& fallbackStem, const std::wstring& fallbackExtension)
		{
			const std::wstring textureReference = SString::UTF8ToWstring(texturePath.C_Str());
			if (!textureReference.empty() && !IsEmbeddedTextureToken(textureReference))
				return GetSafeTextureFileName(textureReference, fallbackStem, fallbackExtension);
			if (embeddedTexture != nullptr)
			{
				const std::wstring embeddedFileName = SString::UTF8ToWstring(embeddedTexture->mFilename.C_Str());
				if (!embeddedFileName.empty()) return GetSafeTextureFileName(embeddedFileName, fallbackStem, fallbackExtension);
			}
			return fallbackStem + fallbackExtension;
		}

		static std::filesystem::path CopyImportedTextureToProject(
			const std::filesystem::path& sourcePath, const std::filesystem::path& extractedTextureDir)
		{
			if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) return {};
			std::filesystem::create_directories(extractedTextureDir);
			const std::filesystem::path preferredOutputPath = extractedTextureDir / sourcePath.filename();
			std::error_code errorCode;
			if (std::filesystem::exists(preferredOutputPath))
			{
				if (std::filesystem::equivalent(sourcePath, preferredOutputPath, errorCode)) return preferredOutputPath;
				errorCode.clear();
				return MakeUniqueFilePath(extractedTextureDir, sourcePath.filename().wstring());
			}
			return preferredOutputPath;
		}

		static std::wstring BuildImportedModelFileName(const std::wstring& modelName)
		{ return SanitizeName(modelName.empty() ? L"Model" : modelName) + WModelFile::Extension; }

		static std::wstring BuildModelMaterialId(UINT materialIndex) { return L"mat_" + std::to_wstring(materialIndex); }
		static std::wstring BuildModelMeshId(UINT meshIndex) { return L"mesh_" + std::to_wstring(meshIndex); }

		static std::wstring BuildRelativeAssetPath(const std::filesystem::path& path, const std::filesystem::path& baseDirectory)
		{
			std::error_code errorCode;
			const std::filesystem::path relativePath = std::filesystem::relative(path, baseDirectory, errorCode);
			if (!errorCode && !relativePath.empty()) return relativePath.generic_wstring();
			const std::filesystem::path lexicalRelativePath = path.lexically_relative(baseDirectory);
			if (!lexicalRelativePath.empty()) return lexicalRelativePath.generic_wstring();
			return path.generic_wstring();
		}
	};
}

namespace AssimpImpl
{
	// ═══════════════════════════════════════════════
	//  日志文本辅助
	// ═══════════════════════════════════════════════

	struct AssimpLoaderLogTextHelpers
	{
		static std::wstring BuildTextureUsageSummary(const ImportedTextureSource& textureSource, const wchar_t* slotName)
		{
			if (textureSource.AssetName.empty() && textureSource.Path.empty())
				return L"";

			const std::wstring textureName = !textureSource.AssetName.empty()
				? textureSource.AssetName
				: std::filesystem::path(textureSource.Path).filename().wstring();
			return std::wstring(slotName) + L"=" + textureName;
		}
	};

	using PathHelpers = AssimpLoaderPathHelpers;
	using LogTextHelpers = AssimpLoaderLogTextHelpers;

	inline std::wstring GetSafeTextureFileName(const std::wstring& preferredName, const std::wstring& fallbackStem, const std::wstring& fallbackExtension) { return PathHelpers::GetSafeTextureFileName(preferredName, fallbackStem, fallbackExtension); }
	inline bool IsEmbeddedTextureToken(const std::wstring& textureReference) { return PathHelpers::IsEmbeddedTextureToken(textureReference); }
	inline std::filesystem::path MakeUniqueFilePath(const std::filesystem::path& directory, const std::wstring& preferredFileName) { return PathHelpers::MakeUniqueFilePath(directory, preferredFileName); }
	inline std::wstring BuildTextureUsageSummary(const ImportedTextureSource& textureSource, const wchar_t* slotName) { return LogTextHelpers::BuildTextureUsageSummary(textureSource, slotName); }
	// ═══════════════════════════════════════════════
	//  层级构建辅助：节点树遍历、模型层级节点创建
	// ═══════════════════════════════════════════════

	struct AssimpLoaderHierarchyHelpers
	{
		static Transform MakeIdentityTransform()
		{
			return Transform();
		}

		static DirectX::XMFLOAT3 QuaternionToEulerDegrees(const aiQuaternion& rotation)
		{
			const double x = static_cast<double>(rotation.x);
			const double y = static_cast<double>(rotation.y);
			const double z = static_cast<double>(rotation.z);
			const double w = static_cast<double>(rotation.w);

			const double sinrCosp = 2.0 * (w * x + y * z);
			const double cosrCosp = 1.0 - 2.0 * (x * x + y * y);
			const double roll = std::atan2(sinrCosp, cosrCosp);

			const double sinp = 2.0 * (w * y - z * x);
			const double pitch = std::abs(sinp) >= 1.0 ? std::copysign(DirectX::XM_PIDIV2, sinp) : std::asin(sinp);

			const double sinyCosp = 2.0 * (w * z + x * y);
			const double cosyCosp = 1.0 - 2.0 * (y * y + z * z);
			const double yaw = std::atan2(sinyCosp, cosyCosp);

			return DirectX::XMFLOAT3(
				static_cast<float>(roll * 180.0 / DirectX::XM_PI),
				static_cast<float>(pitch * 180.0 / DirectX::XM_PI),
				static_cast<float>(yaw * 180.0 / DirectX::XM_PI));
		}

		static Transform ConvertAiTransform(const aiMatrix4x4& transformMatrix)
		{
			aiVector3D scale;
			aiQuaternion rotation;
			aiVector3D position;
			transformMatrix.Decompose(scale, rotation, position);

			Transform transform;
			transform.position = DirectX::XMFLOAT3(position.x, position.y, position.z);
			transform.rotation = QuaternionToEulerDegrees(rotation);
			transform.scale = DirectX::XMFLOAT3(scale.x, scale.y, scale.z);
			return transform;
		}


		static WModelNodeData BuildModelHierarchyNode(
			aiNode* node,
			const aiScene* scene,
			const std::vector<std::wstring>& meshIds,
			const std::vector<std::wstring>& materialIds,
			UINT& nodeCounter,
			const std::wstring& fallbackName,
			const aiNode* normalizedTransformRoot = nullptr)
		{
			WModelNodeData nodeData;
			nodeData.Id = L"node_" + std::to_wstring(nodeCounter++);
			if (node != nullptr)
			{
				const std::wstring nodeName = PathHelpers::SanitizeName(SString::UTF8ToWstring(node->mName.C_Str()));
				nodeData.Name = !nodeName.empty() ? nodeName : PathHelpers::SanitizeName(fallbackName.empty() ? L"Node" : fallbackName);
			}
			else
			{
				nodeData.Name = PathHelpers::SanitizeName(fallbackName.empty() ? L"Node" : fallbackName);
			}
			nodeData.Type = WModelNodeType::Empty;
			// 蒙皮骨架会以参考根的逆矩阵归一化 Bind/Animation 姿势。
			// 模型层级必须剥离同一根变换，否则静态网格会额外吃到该转换
			// （例如 Blender glTF 的 Armature X=-90°），而播放动画时又被骨架抵消。
			nodeData.LocalTransform =
				node != nullptr && node != normalizedTransformRoot
				? ConvertAiTransform(node->mTransformation)
				: MakeIdentityTransform();

			if (node != nullptr)
			{
				for (UINT localMeshIndex = 0; localMeshIndex < node->mNumMeshes; ++localMeshIndex)
				{
					const UINT meshIndex = node->mMeshes[localMeshIndex];
					if (meshIndex >= meshIds.size() || meshIndex >= scene->mNumMeshes)
						continue;

					aiMesh* mesh = scene->mMeshes[meshIndex];
					WModelNodeData meshNode;
					meshNode.Id = nodeData.Id + L"_mesh_" + std::to_wstring(localMeshIndex);
					if (mesh != nullptr)
					{
						const std::wstring meshName = PathHelpers::SanitizeName(SString::UTF8ToWstring(mesh->mName.C_Str()));
						meshNode.Name = !meshName.empty() ? meshName : (PathHelpers::SanitizeName(nodeData.Name) + L"_Mesh_" + std::to_wstring(meshIndex));
					}
					else
					{
						meshNode.Name = PathHelpers::SanitizeName(nodeData.Name) + L"_Mesh_" + std::to_wstring(meshIndex);
					}
					meshNode.Type = WModelNodeType::Mesh;
					meshNode.LocalTransform = MakeIdentityTransform();
					meshNode.MeshRef = meshIds[meshIndex];

					if (mesh != nullptr && mesh->mMaterialIndex < materialIds.size())
					{
						WModelMaterialSlot slot;
						slot.Index = 0;
						slot.MaterialRef = materialIds[mesh->mMaterialIndex];
						meshNode.MaterialSlots.push_back(std::move(slot));
					}

					nodeData.Children.push_back(std::move(meshNode));
				}

				for (UINT childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
				{
					const std::wstring childFallbackName = nodeData.Name + L"_Child_" + std::to_wstring(childIndex);
					nodeData.Children.push_back(
						BuildModelHierarchyNode(
							node->mChildren[childIndex],
							scene,
							meshIds,
							materialIds,
							nodeCounter,
							childFallbackName,
							normalizedTransformRoot));
				}
			}

			return nodeData;
		}

		static Transform CombineTransforms(const Transform& parentTransform, const Transform& localTransform)
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

		static DirectX::XMFLOAT4 ConvertAiQuaternion(const aiQuaternion& quaternion)
		{
			const DirectX::XMFLOAT4 raw(quaternion.x, quaternion.y, quaternion.z, quaternion.w);
			DirectX::XMFLOAT4 normalized = raw;
			if (TryNormalizeQuaternion(raw, normalized))
				return normalized;
			return DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		}

		static Witchcraft::Animation::BoneLocalPose ConvertAiBoneLocalPose(const aiMatrix4x4& transformMatrix)
		{
			aiVector3D scale;
			aiQuaternion rotation;
			aiVector3D position;
			transformMatrix.Decompose(scale, rotation, position);

			// 转置：aiMatrix4x4 是行优先的，但运行时动画系统（SkeletonPoseSystem / SkinningPaletteSystem）以列优先布局存储 XMFLOAT4X4，
			// 因此通过 ConvertFloat4x4ToAiMatrix / ConvertAiMatrixToFloat4x4 进行往返转换会产生正确的 aiMatrix4x4 值。
			Witchcraft::Animation::BoneLocalPose pose;
			pose.Translation = DirectX::XMFLOAT3(position.x, position.y, position.z);
			pose.Rotation = ConvertAiQuaternion(rotation);
			pose.Scale = DirectX::XMFLOAT3(scale.x, scale.y, scale.z);
			pose.Matrix = MathHelps::ConvertAiMatrixToFloat4x4(transformMatrix);
			pose.HasMatrix = true;
			return pose;
		}

		static DirectX::XMFLOAT4X4 ComposeBoneLocalPoseMatrix(const Witchcraft::Animation::BoneLocalPose& pose)
		{
			const DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&pose.Scale);
			const DirectX::XMVECTOR rotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&pose.Rotation));
			const DirectX::XMVECTOR translation = DirectX::XMLoadFloat3(&pose.Translation);
			const DirectX::XMMATRIX matrix =
				DirectX::XMMatrixAffineTransformation(scale, DirectX::XMVectorZero(), rotation, translation);

			DirectX::XMFLOAT4X4 result;
			DirectX::XMStoreFloat4x4(&result, matrix);
			return result;
		}

		static Witchcraft::Animation::BoneLocalPose DecomposeMatrixToBoneLocalPose(const DirectX::XMFLOAT4X4& matrixValue)
		{
			Witchcraft::Animation::BoneLocalPose pose;

			const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&matrixValue);
			DirectX::XMVECTOR scaleVector;
			DirectX::XMVECTOR rotationQuaternion;
			DirectX::XMVECTOR translationVector;
			if (!DirectX::XMMatrixDecompose(&scaleVector, &rotationQuaternion, &translationVector, matrix))
				return pose;

			DirectX::XMStoreFloat3(&pose.Scale, scaleVector);
			DirectX::XMStoreFloat4(&pose.Rotation, DirectX::XMQuaternionNormalize(rotationQuaternion));
			DirectX::XMStoreFloat3(&pose.Translation, translationVector);
			pose.Matrix = matrixValue;
			pose.HasMatrix = true;
			if (!IsFiniteFloat3(pose.Translation) || !IsFiniteFloat4(pose.Rotation) || !IsFiniteFloat3(pose.Scale))
				return Witchcraft::Animation::BoneLocalPose();
			return pose;
		}

		static DirectX::XMFLOAT4X4 MultiplyMatrices(
			const DirectX::XMFLOAT4X4& lhs,
			const DirectX::XMFLOAT4X4& rhs)
		{
			const DirectX::XMMATRIX lhsMatrix = DirectX::XMLoadFloat4x4(&lhs);
			const DirectX::XMMATRIX rhsMatrix = DirectX::XMLoadFloat4x4(&rhs);

			DirectX::XMFLOAT4X4 result;
			DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixMultiply(lhsMatrix, rhsMatrix));
			return result;
		}

		static const aiNode* FindNodeByNameRecursive(const aiNode* node, const std::wstring& name)
		{
			if (node == nullptr)
				return nullptr;

			const std::wstring rawNodeName = SString::UTF8ToWstring(node->mName.C_Str());
			// 骨架资产会把 ':' 等文件系统不安全字符替换为 '_'，而 Assimp 节点
			// 仍保留原始名称（例如 mixamorig:Hips）。两种名称都必须能定位到同一节点。
			if (rawNodeName == name || AssimpLoaderPathHelpers::SanitizeName(rawNodeName) == name)
				return node;

			for (UINT childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
			{
				if (const aiNode* childResult = FindNodeByNameRecursive(node->mChildren[childIndex], name))
					return childResult;
			}

			return nullptr;
		}

		static aiMatrix4x4 ComputeNodeGlobalTransform(const aiNode* node)
		{
			aiMatrix4x4 global = node != nullptr ? node->mTransformation : aiMatrix4x4();
			for (const aiNode* parent = node != nullptr ? node->mParent : nullptr; parent != nullptr; parent = parent->mParent)
				global = parent->mTransformation * global;
			return global;
		}

		static const aiNode* FindNodeReferencingMeshIndexRecursive(const aiNode* node, UINT meshIndex)
		{
			if (node == nullptr)
				return nullptr;

			for (UINT localMeshIndex = 0; localMeshIndex < node->mNumMeshes; ++localMeshIndex)
			{
				if (node->mMeshes[localMeshIndex] == meshIndex)
					return node;
			}

			for (UINT childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
			{
				if (const aiNode* childResult = FindNodeReferencingMeshIndexRecursive(node->mChildren[childIndex], meshIndex))
					return childResult;
			}

			return nullptr;
		}

		static bool CanCollapseLeadingWrapperNode(const aiNode* node)
		{
			return
				node != nullptr &&
				node->mNumMeshes == 0 &&
				node->mNumChildren == 1;
		}

		static const aiNode* FindCollapsedImportRootNode(const aiScene* scene)
		{
			if (scene == nullptr || scene->mRootNode == nullptr)
				return nullptr;

			const aiNode* collapsedRootNode = scene->mRootNode;
			while (CanCollapseLeadingWrapperNode(collapsedRootNode))
				collapsedRootNode = collapsedRootNode->mChildren[0];

			return collapsedRootNode;
		}

		static aiVector3D TransformAiPoint(const aiMatrix4x4& matrix, const aiVector3D& point)
		{
			return aiVector3D(
				matrix.a1 * point.x + matrix.a2 * point.y + matrix.a3 * point.z + matrix.a4,
				matrix.b1 * point.x + matrix.b2 * point.y + matrix.b3 * point.z + matrix.b4,
				matrix.c1 * point.x + matrix.c2 * point.y + matrix.c3 * point.z + matrix.c4);
		}

		static aiVector3D TransformAiDirection(const aiMatrix4x4& matrix, const aiVector3D& direction)
		{
			return aiVector3D(
				matrix.a1 * direction.x + matrix.a2 * direction.y + matrix.a3 * direction.z,
				matrix.b1 * direction.x + matrix.b2 * direction.y + matrix.b3 * direction.z,
				matrix.c1 * direction.x + matrix.c2 * direction.y + matrix.c3 * direction.z);
		}

		static aiMatrix4x4 ComputeBoneLocalTransformRelativeToAncestorBone(
			const aiNode* node,
			const aiNode* ancestorBoneNode)
		{
			if (node == nullptr)
				return aiMatrix4x4();

			const aiMatrix4x4 nodeGlobal = ComputeNodeGlobalTransform(node);
			if (ancestorBoneNode == nullptr)
				return nodeGlobal;

			aiMatrix4x4 ancestorGlobal = ComputeNodeGlobalTransform(ancestorBoneNode);
			ancestorGlobal.Inverse();
			return ancestorGlobal * nodeGlobal;
		}

		static const aiNode* FindNearestAncestorBoneNode(
			const aiNode* node,
			const std::set<std::wstring>& boneNames)
		{
			for (const aiNode* current = node != nullptr ? node->mParent : nullptr; current != nullptr; current = current->mParent)
			{
				const std::wstring currentName = PathHelpers::SanitizeName(SString::UTF8ToWstring(current->mName.C_Str()));
				if (boneNames.find(currentName) != boneNames.end())
					return current;
			}

			return nullptr;
		}

		static aiMatrix4x4 ComputeAnimationChannelPrefixTransform(
			const aiNode* animatedNode,
			const aiNode* ancestorBoneNode)
		{
			if (animatedNode == nullptr)
				return aiMatrix4x4();

			aiMatrix4x4 parentGlobal = animatedNode->mParent != nullptr
				? ComputeNodeGlobalTransform(animatedNode->mParent)
				: aiMatrix4x4();

			if (ancestorBoneNode == nullptr)
				return parentGlobal;

			aiMatrix4x4 ancestorGlobal = ComputeNodeGlobalTransform(ancestorBoneNode);
			ancestorGlobal.Inverse();
			return ancestorGlobal * parentGlobal;
		}

		static Witchcraft::Animation::BoneLocalPose SampleAiNodeAnimLocalPose(
			const aiNodeAnim* channel,
			double time,
			const Witchcraft::Animation::BoneLocalPose& fallbackPose,
			bool convertFromRightHanded = false)
		{
			Witchcraft::Animation::BoneLocalPose pose = fallbackPose;

			if (channel == nullptr)
				return pose;

			auto sampleVectorKeys =
				[time, convertFromRightHanded](
					const aiVectorKey* keys,
					UINT keyCount,
					const DirectX::XMFLOAT3& fallbackValue) -> DirectX::XMFLOAT3
				{
					if (keys == nullptr || keyCount == 0)
						return fallbackValue;

					std::vector<UINT> filteredIndices;
					filteredIndices.reserve(keyCount);
					for (UINT keyIndex = 0; keyIndex < keyCount; ++keyIndex)
					{
						const aiVectorKey& key = keys[keyIndex];
						if (!std::isfinite(key.mTime))
							continue;

					DirectX::XMFLOAT3 value(
							key.mValue.x,
							key.mValue.y,
							key.mValue.z);
					if (convertFromRightHanded)
						value.z = -value.z;
						if (!IsFiniteFloat3(value))
							continue;

						filteredIndices.push_back(keyIndex);
					}

					if (filteredIndices.empty())
						return fallbackValue;

					const auto getValue =
					[keys, convertFromRightHanded](UINT keyIndex) -> DirectX::XMFLOAT3
					{
						DirectX::XMFLOAT3 value(
							keys[keyIndex].mValue.x,
							keys[keyIndex].mValue.y,
							keys[keyIndex].mValue.z);
						if (convertFromRightHanded)
							value.z = -value.z;
						return value;
					};

					std::vector<double> normalizedTimes;
					normalizedTimes.reserve(filteredIndices.size());
					for (UINT keyIndex : filteredIndices)
						normalizedTimes.push_back(NormalizeImportedAnimationTime(keys[keyIndex].mTime));

					const double normalizedSampleTime = NormalizeImportedAnimationTime(time);
					constexpr double kSampleTimeEpsilon = 1.0e-8;
					if (normalizedSampleTime + kSampleTimeEpsilon < normalizedTimes.front())
						return fallbackValue;

					size_t keySlot = 0;
					for (size_t index = 0; index < normalizedTimes.size(); ++index)
					{
						if (normalizedTimes[index] <= normalizedSampleTime + kSampleTimeEpsilon)
							keySlot = index;
						else
							break;
					}

					const UINT keyIndexA = filteredIndices[keySlot];
					const DirectX::XMFLOAT3 valueA = getValue(keyIndexA);
					if (keySlot + 1 >= filteredIndices.size())
						return valueA;

					const double timeA = normalizedTimes[keySlot];
					const double timeB = normalizedTimes[keySlot + 1];
					if (normalizedSampleTime <= timeA + kSampleTimeEpsilon)
						return valueA;

					const UINT keyIndexB = filteredIndices[keySlot + 1];
					const DirectX::XMFLOAT3 valueB = getValue(keyIndexB);
					const double duration = timeB - timeA;
					if (!std::isfinite(duration) || std::abs(duration) <= kSampleTimeEpsilon)
						return valueB;

					const float alpha = static_cast<float>((normalizedSampleTime - timeA) / duration);
					DirectX::XMFLOAT3 result = fallbackValue;
					DirectX::XMStoreFloat3(
						&result,
						DirectX::XMVectorLerp(
							DirectX::XMLoadFloat3(&valueA),
							DirectX::XMLoadFloat3(&valueB),
							(std::max)(0.0f, (std::min)(1.0f, alpha))));
					return IsFiniteFloat3(result) ? result : valueA;
				};

			auto sampleQuaternionKeys =
				[time, convertFromRightHanded](
					const aiQuatKey* keys,
					UINT keyCount,
					const DirectX::XMFLOAT4& fallbackValue) -> DirectX::XMFLOAT4
				{
					if (keys == nullptr || keyCount == 0)
						return fallbackValue;

					std::vector<UINT> filteredIndices;
					filteredIndices.reserve(keyCount);
					for (UINT keyIndex = 0; keyIndex < keyCount; ++keyIndex)
					{
						const aiQuatKey& key = keys[keyIndex];
						if (!std::isfinite(key.mTime))
							continue;

						DirectX::XMFLOAT4 value = ConvertAiQuaternion(key.mValue);
						if (convertFromRightHanded)
						{
							value.x = -value.x;
							value.y = -value.y;
						}
						if (!TryNormalizeQuaternion(value, value))
							continue;

						filteredIndices.push_back(keyIndex);
					}

					if (filteredIndices.empty())
						return fallbackValue;

					const auto getValue =
					[keys, convertFromRightHanded](UINT keyIndex) -> DirectX::XMFLOAT4
					{
						DirectX::XMFLOAT4 value = ConvertAiQuaternion(keys[keyIndex].mValue);
						if (convertFromRightHanded)
						{
							value.x = -value.x;
							value.y = -value.y;
						}
							if (!TryNormalizeQuaternion(value, value))
								return DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
							return value;
						};

					std::vector<double> normalizedTimes;
					normalizedTimes.reserve(filteredIndices.size());
					for (UINT keyIndex : filteredIndices)
						normalizedTimes.push_back(NormalizeImportedAnimationTime(keys[keyIndex].mTime));

					const double normalizedSampleTime = NormalizeImportedAnimationTime(time);
					constexpr double kSampleTimeEpsilon = 1.0e-8;
					if (normalizedSampleTime + kSampleTimeEpsilon < normalizedTimes.front())
						return fallbackValue;

					size_t keySlot = 0;
					for (size_t index = 0; index < normalizedTimes.size(); ++index)
					{
						if (normalizedTimes[index] <= normalizedSampleTime + kSampleTimeEpsilon)
							keySlot = index;
						else
							break;
					}

					const UINT keyIndexA = filteredIndices[keySlot];
					const DirectX::XMFLOAT4 valueA = getValue(keyIndexA);
					if (keySlot + 1 >= filteredIndices.size())
						return valueA;

					const double timeA = normalizedTimes[keySlot];
					const double timeB = normalizedTimes[keySlot + 1];
					if (normalizedSampleTime <= timeA + kSampleTimeEpsilon)
						return valueA;

					const UINT keyIndexB = filteredIndices[keySlot + 1];
					const DirectX::XMFLOAT4 valueB = getValue(keyIndexB);
					const double duration = timeB - timeA;
					if (!std::isfinite(duration) || std::abs(duration) <= kSampleTimeEpsilon)
						return valueB;

					float alpha = static_cast<float>((normalizedSampleTime - timeA) / duration);
					alpha = (std::max)(0.0f, (std::min)(1.0f, alpha));

					DirectX::XMVECTOR quatA = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&valueA));
					DirectX::XMVECTOR quatB = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&valueB));
					if (DirectX::XMVectorGetX(DirectX::XMVector4Dot(quatA, quatB)) < 0.0f)
						quatB = DirectX::XMVectorNegate(quatB);

					DirectX::XMFLOAT4 result = fallbackValue;
					DirectX::XMStoreFloat4(
						&result,
						DirectX::XMQuaternionNormalize(DirectX::XMQuaternionSlerp(quatA, quatB, alpha)));
					return IsFiniteFloat4(result) ? result : valueA;
				};

			if (channel->mNumPositionKeys > 0)
			{
				pose.Translation = sampleVectorKeys(
					channel->mPositionKeys,
					channel->mNumPositionKeys,
					pose.Translation);
			}

			if (channel->mNumRotationKeys > 0)
			{
				pose.Rotation = sampleQuaternionKeys(
					channel->mRotationKeys,
					channel->mNumRotationKeys,
					pose.Rotation);
			}

			if (channel->mNumScalingKeys > 0)
			{
				pose.Scale = sampleVectorKeys(
					channel->mScalingKeys,
					channel->mNumScalingKeys,
					pose.Scale);
			}

			return pose;
		}

		static aiMatrix4x4 BuildAiMatrixFromBoneLocalPose(const Witchcraft::Animation::BoneLocalPose& pose)
		{
			aiMatrix4x4 scaleMatrix;
			aiMatrix4x4::Scaling(aiVector3D(pose.Scale.x, pose.Scale.y, pose.Scale.z), scaleMatrix);

			const aiQuaternion rotationQuaternion(
				pose.Rotation.w,
				pose.Rotation.x,
				pose.Rotation.y,
				pose.Rotation.z);
			const aiMatrix4x4 rotationMatrix = aiMatrix4x4(rotationQuaternion.GetMatrix());

			aiMatrix4x4 translationMatrix;
			aiMatrix4x4::Translation(aiVector3D(pose.Translation.x, pose.Translation.y, pose.Translation.z), translationMatrix);

			return translationMatrix * rotationMatrix * scaleMatrix;
		}

		static void CollectChannelKeyTimes(const aiNodeAnim* channel, std::vector<double>& outKeyTimes)
		{
			outKeyTimes.clear();
			if (channel == nullptr)
				return;

			outKeyTimes.reserve(
				static_cast<size_t>(channel->mNumPositionKeys) +
				static_cast<size_t>(channel->mNumRotationKeys) +
				static_cast<size_t>(channel->mNumScalingKeys));

			for (UINT keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex)
			{
				const double keyTime = channel->mPositionKeys[keyIndex].mTime;
				if (std::isfinite(keyTime))
					outKeyTimes.push_back(NormalizeImportedAnimationTime(keyTime));
			}
			for (UINT keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex)
			{
				const double keyTime = channel->mRotationKeys[keyIndex].mTime;
				if (std::isfinite(keyTime))
					outKeyTimes.push_back(NormalizeImportedAnimationTime(keyTime));
			}
			for (UINT keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex)
			{
				const double keyTime = channel->mScalingKeys[keyIndex].mTime;
				if (std::isfinite(keyTime))
					outKeyTimes.push_back(NormalizeImportedAnimationTime(keyTime));
			}

			if (outKeyTimes.empty())
				outKeyTimes.push_back(0.0);

			std::sort(outKeyTimes.begin(), outKeyTimes.end());
			outKeyTimes.erase(
				std::unique(
					outKeyTimes.begin(),
					outKeyTimes.end(),
					[](double lhs, double rhs)
					{
						return std::abs(lhs - rhs) <= 0.000001;
					}),
				outKeyTimes.end());
		}


		static SceneEntityBase* FindFirstMeshEntityRecursive(WitchcraECS* ecs, SceneEntityBase* rootEntity)
		{
			if (ecs == nullptr || rootEntity == nullptr)
				return nullptr;

			if (ecs->HasComponent<MeshComponent>(rootEntity))
				return rootEntity;

			std::vector<SceneEntityBase*> pendingEntities = ecs->GetSceneChildren(rootEntity);
			while (!pendingEntities.empty())
			{
				SceneEntityBase* currentEntity = pendingEntities.back();
				pendingEntities.pop_back();
				if (currentEntity == nullptr)
					continue;

				if (ecs->HasComponent<MeshComponent>(currentEntity))
					return currentEntity;

				for (SceneEntityBase* childEntity : ecs->GetSceneChildren(currentEntity))
					pendingEntities.push_back(childEntity);
			}

			return nullptr;
		}

		static std::vector<SceneEntityBase*> CollectMeshEntitiesRecursive(WitchcraECS* ecs, SceneEntityBase* rootEntity)
		{
			std::vector<SceneEntityBase*> meshEntities;
			if (ecs == nullptr || rootEntity == nullptr)
				return meshEntities;

			std::vector<SceneEntityBase*> pendingEntities = { rootEntity };
			while (!pendingEntities.empty())
			{
				SceneEntityBase* currentEntity = pendingEntities.back();
				pendingEntities.pop_back();
				if (currentEntity == nullptr)
					continue;

				if (ecs->HasComponent<MeshComponent>(currentEntity))
					meshEntities.push_back(currentEntity);

				for (SceneEntityBase* childEntity : ecs->GetSceneChildren(currentEntity))
					pendingEntities.push_back(childEntity);
			}

			return meshEntities;
		}

	};

	using HierarchyHelpers = AssimpLoaderHierarchyHelpers;

	inline Transform MakeIdentityTransform() { return HierarchyHelpers::MakeIdentityTransform(); }
	inline DirectX::XMFLOAT3 QuaternionToEulerDegrees(const aiQuaternion& rotation) { return HierarchyHelpers::QuaternionToEulerDegrees(rotation); }
	inline Transform ConvertAiTransform(const aiMatrix4x4& transformMatrix) { return HierarchyHelpers::ConvertAiTransform(transformMatrix); }
	inline WModelNodeData BuildModelHierarchyNode(aiNode* node, const aiScene* scene, const std::vector<std::wstring>& meshIds, const std::vector<std::wstring>& materialIds, UINT& nodeCounter, const std::wstring& fallbackName, const aiNode* normalizedTransformRoot = nullptr) { return HierarchyHelpers::BuildModelHierarchyNode(node, scene, meshIds, materialIds, nodeCounter, fallbackName, normalizedTransformRoot); }
	inline Transform CombineTransforms(const Transform& parentTransform, const Transform& localTransform) { return HierarchyHelpers::CombineTransforms(parentTransform, localTransform); }
	inline DirectX::XMFLOAT4 ConvertAiQuaternion(const aiQuaternion& quaternion) { return HierarchyHelpers::ConvertAiQuaternion(quaternion); }
	inline Witchcraft::Animation::BoneLocalPose ConvertAiBoneLocalPose(const aiMatrix4x4& transformMatrix) { return HierarchyHelpers::ConvertAiBoneLocalPose(transformMatrix); }
	inline DirectX::XMFLOAT4X4 ComposeBoneLocalPoseMatrix(const Witchcraft::Animation::BoneLocalPose& pose) { return HierarchyHelpers::ComposeBoneLocalPoseMatrix(pose); }
	inline Witchcraft::Animation::BoneLocalPose DecomposeMatrixToBoneLocalPose(const DirectX::XMFLOAT4X4& matrixValue) { return HierarchyHelpers::DecomposeMatrixToBoneLocalPose(matrixValue); }
	inline DirectX::XMFLOAT4X4 MultiplyMatrices(const DirectX::XMFLOAT4X4& lhs, const DirectX::XMFLOAT4X4& rhs) { return HierarchyHelpers::MultiplyMatrices(lhs, rhs); }
	inline const aiNode* FindNodeByNameRecursive(const aiNode* node, const std::wstring& name) { return HierarchyHelpers::FindNodeByNameRecursive(node, name); }
	inline aiMatrix4x4 ComputeNodeGlobalTransform(const aiNode* node) { return HierarchyHelpers::ComputeNodeGlobalTransform(node); }
	inline const aiNode* FindNodeReferencingMeshIndexRecursive(const aiNode* node, UINT meshIndex) { return HierarchyHelpers::FindNodeReferencingMeshIndexRecursive(node, meshIndex); }
	inline const aiNode* FindCollapsedImportRootNode(const aiScene* scene) { return HierarchyHelpers::FindCollapsedImportRootNode(scene); }
	inline aiVector3D TransformAiPoint(const aiMatrix4x4& matrix, const aiVector3D& point) { return HierarchyHelpers::TransformAiPoint(matrix, point); }
	inline aiVector3D TransformAiDirection(const aiMatrix4x4& matrix, const aiVector3D& direction) { return HierarchyHelpers::TransformAiDirection(matrix, direction); }
	inline aiMatrix4x4 ComputeBoneLocalTransformRelativeToAncestorBone(const aiNode* node, const aiNode* ancestorBoneNode) { return HierarchyHelpers::ComputeBoneLocalTransformRelativeToAncestorBone(node, ancestorBoneNode); }
	inline const aiNode* FindNearestAncestorBoneNode(const aiNode* node, const std::set<std::wstring>& boneNames) { return HierarchyHelpers::FindNearestAncestorBoneNode(node, boneNames); }
	inline aiMatrix4x4 ComputeAnimationChannelPrefixTransform(const aiNode* animatedNode, const aiNode* ancestorBoneNode) { return HierarchyHelpers::ComputeAnimationChannelPrefixTransform(animatedNode, ancestorBoneNode); }
	inline Witchcraft::Animation::BoneLocalPose SampleAiNodeAnimLocalPose(const aiNodeAnim* channel, double time, const Witchcraft::Animation::BoneLocalPose& fallbackPose, bool convertFromRightHanded = false) { return HierarchyHelpers::SampleAiNodeAnimLocalPose(channel, time, fallbackPose, convertFromRightHanded); }
	inline aiMatrix4x4 BuildAiMatrixFromBoneLocalPose(const Witchcraft::Animation::BoneLocalPose& pose) { return HierarchyHelpers::BuildAiMatrixFromBoneLocalPose(pose); }
	inline void CollectChannelKeyTimes(const aiNodeAnim* channel, std::vector<double>& outKeyTimes) { HierarchyHelpers::CollectChannelKeyTimes(channel, outKeyTimes); }
	inline SceneEntityBase* FindFirstMeshEntityRecursive(WitchcraECS* ecs, SceneEntityBase* rootEntity) { return HierarchyHelpers::FindFirstMeshEntityRecursive(ecs, rootEntity); }
	inline std::vector<SceneEntityBase*> CollectMeshEntitiesRecursive(WitchcraECS* ecs, SceneEntityBase* rootEntity) { return HierarchyHelpers::CollectMeshEntitiesRecursive(ecs, rootEntity); }

	bool SceneContainsBones(const aiScene* scene)
	{
		if (scene == nullptr)
			return false;

		for (UINT meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
		{
			const aiMesh* mesh = scene->mMeshes[meshIndex];
			if (mesh != nullptr && mesh->mNumBones > 0)
				return true;
		}

		return false;
	}

	ImportedTextureSource BuildImportedTextureSource(const std::filesystem::path& materialFilePath, const std::wstring& textureName)
	{
		ImportedTextureSource source;
		if (textureName.empty())
			return source;

		std::filesystem::path texturePath(textureName);
		if (texturePath.is_relative())
		{
			if (!texturePath.has_parent_path())
				texturePath = materialFilePath.parent_path().parent_path() / L"Textures" / texturePath;
			else
				texturePath = materialFilePath.parent_path() / texturePath;
		}

		source.Path = texturePath.lexically_normal().wstring();
		source.AssetName = std::filesystem::path(textureName).filename().wstring();
		return source;
	}

	ImportedMaterialInfo ConvertMaterialFileDataToImportedInfo(const WMaterialFileData& materialData, const std::filesystem::path& materialFilePath)
	{
		ImportedMaterialInfo info;
		info.Name = materialData.MaterialName.empty() ? materialFilePath.stem().wstring() : materialData.MaterialName;
		info.DiffuseColor = materialData.DiffuseColor;
		info.Emissive = materialData.Emissive;
		info.Metallic = materialData.Metallic;
		info.Roughness = materialData.Roughness;
		info.Opacity = materialData.Opacity;
		info.DiffuseTexture = BuildImportedTextureSource(materialFilePath, materialData.DiffuseTexture);
		info.NormalTexture = BuildImportedTextureSource(materialFilePath, materialData.NormalTexture);
		info.MetallicTexture = BuildImportedTextureSource(materialFilePath, materialData.MetallicTexture);
		info.RoughnessTexture = BuildImportedTextureSource(materialFilePath, materialData.RoughnessTexture);
		return info;
	}

	SceneEntityBase* CreateEntityForModelNode(
		WitchcraECS* ecs,
		const WModelNodeData& nodeData,
		const std::wstring& entityName,
		SceneEntityBase* parentEntity,
		const std::wstring& filePath,
		const std::wstring& MeshName,
		const WModelMeshData* meshData)
	{
		if (ecs == nullptr)
			return nullptr;

		const bool isMeshNode = nodeData.Type == WModelNodeType::Mesh && meshData != nullptr;
		SceneEntityBase* entity = isMeshNode
			? ecs->CreateMeshEntity(entityName, parentEntity)
			: ecs->CreateBasicEntity(entityName, parentEntity, ComponentType::Co_Unk);
		if (entity == nullptr)
			return nullptr;
		ecs->SetEntitySceneType(entity, SceneEntityType::StaticScenery, false);

		if (isMeshNode)
		{
			if (!ecs->ConfigureMeshEntity(
				entity,
				nullptr,
				entityName,
				filePath,
				MeshName,
				不透明物体渲染项目,
				L""))
			{
				return entity;
			}

			// 这里先只回填 CPU 侧网格数据，真正的 GPU 资源创建放在后面的 SetupMesh 中完成。
			ecs->AppendMeshEntityVertices(entity, meshData->Vertices);
			ecs->AppendMeshEntityIndices(entity, meshData->Indices);
		}

		return entity;
	}

	bool TryBuildInlineSkinnedMeshData(
		const std::wstring& meshName,
		const std::vector<Vertex>& vertices,
		const std::vector<Witchcraft::Animation::VertexBoneInfluence4>& skinning,
		const std::vector<std::uint32_t>& indices,
		ImportedSkinnedMeshData* outMeshData)
	{
		if (outMeshData == nullptr ||
			vertices.empty() ||
			indices.empty() ||
			skinning.size() != vertices.size())
		{
			return false;
		}

		ImportedSkinnedMeshData meshData;
		meshData.Name = meshName;
		meshData.Vertices.reserve(vertices.size());
		meshData.Indices = indices;
		for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex)
		{
			Witchcraft::Animation::SkinnedVertex vertex;
			vertex.StaticVertex = vertices[vertexIndex];
			vertex.Skinning = skinning[vertexIndex];
			float weightSum = 0.0f;
			for (float weight : vertex.Skinning.BoneWeights)
			{
				if (!std::isfinite(weight) || weight < 0.0f)
					return false;
				weightSum += weight;
			}
			if (!std::isfinite(weightSum) || weightSum <= 0.00001f)
				return false;
			vertex.Skinning.Normalize();
			meshData.Vertices.push_back(vertex);
		}

		*outMeshData = std::move(meshData);
		return true;
	}
}

using namespace AssimpImpl;

// ═══════════════════════════════════════════════
//  AssimpLoader 静态类方法：转发到 AssimpImpl 空间同名自由函数
// ═══════════════════════════════════════════════

std::wstring AssimpLoader::SanitizeName(std::wstring value)
{
	return PathHelpers::SanitizeName(std::move(value));
}

std::wstring AssimpLoader::BuildImportedModelFileName(const std::wstring& modelName)
{
	return PathHelpers::BuildImportedModelFileName(modelName);
}

std::wstring AssimpLoader::BuildModelMaterialId(UINT materialIndex)
{
	return PathHelpers::BuildModelMaterialId(materialIndex);
}

std::wstring AssimpLoader::BuildModelMeshId(UINT meshIndex)
{
	return PathHelpers::BuildModelMeshId(meshIndex);
}

std::wstring AssimpLoader::BuildRelativeAssetPath(const std::filesystem::path& path, const std::filesystem::path& baseDirectory)
{
	return PathHelpers::BuildRelativeAssetPath(path, baseDirectory);
}

std::filesystem::path AssimpLoader::BuildImportedMaterialFilePath(
	const std::filesystem::path& materialsDir,
	const std::wstring& modelName,
	const std::wstring& materialName)
{
	return PathHelpers::BuildImportedMaterialFilePath(materialsDir, modelName, materialName);
}

std::wstring AssimpLoader::ResolvePreferredTextureName(
	const aiString& texturePath,
	const aiTexture* embeddedTexture,
	const std::wstring& fallbackStem,
	const std::wstring& fallbackExtension)
{
	return PathHelpers::ResolvePreferredTextureName(texturePath, embeddedTexture, fallbackStem, fallbackExtension);
}

std::filesystem::path AssimpLoader::CopyImportedTextureToProject(
	const std::filesystem::path& sourcePath,
	const std::filesystem::path& extractedTextureDir)
{
	return PathHelpers::CopyImportedTextureToProject(sourcePath, extractedTextureDir);
}

std::wstring AssimpLoader::ResolveModelNodeName(const aiNode* node, const std::wstring& fallbackName)
{
	if (node != nullptr)
	{
		const std::wstring nodeName = AssimpLoaderPathHelpers::SanitizeName(SString::UTF8ToWstring(node->mName.C_Str()));
		if (!nodeName.empty())
			return nodeName;
	}

	return AssimpLoaderPathHelpers::SanitizeName(fallbackName.empty() ? L"Node" : fallbackName);
}

std::wstring AssimpLoader::ResolveModelMeshName(const aiMesh* mesh, UINT meshIndex, const std::wstring& fallbackPrefix)
{
	if (mesh != nullptr)
	{
		const std::wstring meshName = AssimpLoaderPathHelpers::SanitizeName(SString::UTF8ToWstring(mesh->mName.C_Str()));
		if (!meshName.empty())
			return meshName;
	}

	return AssimpLoaderPathHelpers::SanitizeName(fallbackPrefix) + L"_Mesh_" + std::to_wstring(meshIndex);
}

std::wstring AssimpLoader::BuildImportedAnimationFileName(const std::wstring& rootName, const std::wstring& clipName)
{
	return AssimpLoaderPathHelpers::SanitizeName(rootName.empty() ? L"Skeleton" : rootName) + L"_" +
		AssimpLoaderPathHelpers::SanitizeName(clipName.empty() ? L"Clip" : clipName) + WAnimationFile::Extension;
}

std::wstring AssimpLoader::BuildImportedSkeletonFileName(const std::wstring& rootName)
{
	return AssimpLoaderPathHelpers::SanitizeName(rootName.empty() ? L"Skeleton" : rootName) + WSkeletonFile::Extension;
}

std::wstring AssimpLoader::BuildImportedSkinnedMeshFileName(const std::wstring& rootName)
{
	return AssimpLoaderPathHelpers::SanitizeName(rootName.empty() ? L"SkinnedMesh" : rootName);
}

std::wstring AssimpLoader::NormalizeBindingName(const std::wstring& value)
{
	std::wstring normalized;
	normalized.reserve(value.size());
	for (wchar_t ch : value)
	{
		if (std::iswalnum(ch) == 0)
			continue;
		normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
	}
	return normalized;
}

int AssimpLoader::ComputeSkinnedBindingScore(
	const std::wstring& submeshName,
	const std::wstring& submeshMaterialName,
	std::uint32_t submeshIndexCount,
	const std::wstring& meshName,
	const std::wstring& entityName,
	const std::wstring& meshMaterialName,
	std::uint32_t meshIndexCount)
{
	int score = 0;

	const std::wstring normalizedSubmeshName = NormalizeBindingName(submeshName);
	const std::wstring normalizedMeshName = NormalizeBindingName(meshName);
	const std::wstring normalizedEntityName = NormalizeBindingName(entityName);
	const std::wstring normalizedSubmeshMaterialName = NormalizeBindingName(submeshMaterialName);
	const std::wstring normalizedMeshMaterialName = NormalizeBindingName(meshMaterialName);

	if (!normalizedSubmeshName.empty())
	{
		if (!normalizedMeshName.empty())
		{
			if (normalizedSubmeshName == normalizedMeshName)
				score += 1000;
			else if (normalizedMeshName.find(normalizedSubmeshName) != std::wstring::npos ||
				normalizedSubmeshName.find(normalizedMeshName) != std::wstring::npos)
				score += 600;
		}

		if (!normalizedEntityName.empty())
		{
			if (normalizedSubmeshName == normalizedEntityName)
				score += 700;
			else if (normalizedEntityName.find(normalizedSubmeshName) != std::wstring::npos ||
				normalizedSubmeshName.find(normalizedEntityName) != std::wstring::npos)
				score += 400;
		}
	}

	if (submeshIndexCount == meshIndexCount)
		score += 120;

	if (!normalizedSubmeshMaterialName.empty() &&
		!normalizedMeshMaterialName.empty() &&
		normalizedSubmeshMaterialName == normalizedMeshMaterialName)
	{
		score += 40;
	}

	return score;
}

void AssimpLoader::Create(Engine* engine)
{
	m_engine = engine;
}

void AssimpLoader::SetConsoleWindow(ConsoleWindow* consoleWindow)
{
	m_consoleWindow = consoleWindow;
}

bool AssimpLoader::UploadSkinnedMeshGeometry(
	const std::wstring& geometryName,
	const ImportedSkinnedMeshData& skinnedMeshData) const
{
	if (geometryName.empty() ||
		skinnedMeshData.Vertices.empty() || skinnedMeshData.Indices.empty())
		return false;

	constexpr std::uint32_t kMaxSupportedSkinBoneIndex = 255u;
	for (size_t vertexIndex = 0; vertexIndex < skinnedMeshData.Vertices.size(); ++vertexIndex)
	{
		const auto& influence = skinnedMeshData.Vertices[vertexIndex].Skinning;
		for (std::uint32_t influenceIndex = 0; influenceIndex < Witchcraft::Animation::MaxBoneInfluenceCountPerVertex; ++influenceIndex)
		{
			if (influence.BoneWeights[influenceIndex] <= 0.0f)
				continue;
			if (influence.BoneIndices[influenceIndex] <= kMaxSupportedSkinBoneIndex)
				continue;

			if (m_consoleWindow != nullptr)
			{
				m_consoleWindow->AddErrorMessage(
					L"[SkinnedMesh] 顶点骨骼索引超出当前渲染上限：geo=%s vertex=%u influence=%u boneIndex=%u maxSupported=%u。已禁止该蒙皮几何上传，避免 GPU 崩溃。",
					geometryName.c_str(),
					static_cast<UINT>(vertexIndex),
					static_cast<UINT>(influenceIndex),
					static_cast<UINT>(influence.BoneIndices[influenceIndex]),
					static_cast<UINT>(kMaxSupportedSkinBoneIndex));
			}
			return false;
		}
	}

	for (size_t indexPosition = 0; indexPosition < skinnedMeshData.Indices.size(); ++indexPosition)
	{
		const std::uint32_t indexValue = skinnedMeshData.Indices[indexPosition];
		if (indexValue < skinnedMeshData.Vertices.size())
			continue;

		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddErrorMessage(
				L"[SkinnedMesh] 索引越界：geo=%s indexPos=%u indexValue=%u vertexCount=%u。已禁止该蒙皮几何上传，避免 GPU 崩溃。",
				geometryName.c_str(),
				static_cast<UINT>(indexPosition),
				static_cast<UINT>(indexValue),
				static_cast<UINT>(skinnedMeshData.Vertices.size()));
		}
		return false;
	}

	for (size_t submeshIndex = 0; submeshIndex < skinnedMeshData.Submeshes.size(); ++submeshIndex)
	{
		const auto& submesh = skinnedMeshData.Submeshes[submeshIndex];
		const std::uint64_t indexEnd =
			static_cast<std::uint64_t>(submesh.IndexStart) +
			static_cast<std::uint64_t>(submesh.IndexCount);
		if (indexEnd <= skinnedMeshData.Indices.size())
			continue;

		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddErrorMessage(
				L"[SkinnedMesh] 子网格范围越界：geo=%s submesh=%u indexStart=%u indexCount=%u totalIndexCount=%u。已禁止该蒙皮几何上传，避免 GPU 崩溃。",
				geometryName.c_str(),
				static_cast<UINT>(submeshIndex),
				static_cast<UINT>(submesh.IndexStart),
				static_cast<UINT>(submesh.IndexCount),
				static_cast<UINT>(skinnedMeshData.Indices.size()));
		}
		return false;
	}

	ID3D12Device* device = m_engine->GetD3DWindow()->GetDevice();
	if (device == nullptr)
		return false;

	ComPtr<ID3D12CommandAllocator> uploadCommandAllocator = nullptr;
	ComPtr<ID3D12GraphicsCommandList> uploadCommandList = nullptr;
	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(uploadCommandAllocator.GetAddressOf())));
	ThrowIfFailed(device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		uploadCommandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(uploadCommandList.GetAddressOf())));

	const UINT vbByteSize = static_cast<UINT>(skinnedMeshData.Vertices.size() * sizeof(Witchcraft::Animation::SkinnedVertex));
	const UINT ibByteSize = static_cast<UINT>(skinnedMeshData.Indices.size() * sizeof(std::uint32_t));

	MeshGeometry geo;
	geo.Name = geometryName;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
	CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), skinnedMeshData.Vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
	CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), skinnedMeshData.Indices.data(), ibByteSize);

	geo.VertexBufferGPU = D3DWindow::CreateDefaultBuffer(
		device,
		uploadCommandList.Get(),
		skinnedMeshData.Vertices.data(),
		vbByteSize,
		geo.VertexBufferUploader);

	geo.IndexBufferGPU = D3DWindow::CreateDefaultBuffer(
		device,
		uploadCommandList.Get(),
		skinnedMeshData.Indices.data(),
		ibByteSize,
		geo.IndexBufferUploader);

	geo.vertexBufferView.BufferLocation = geo.VertexBufferGPU->GetGPUVirtualAddress();
	geo.vertexBufferView.StrideInBytes = sizeof(Witchcraft::Animation::SkinnedVertex);
	geo.vertexBufferView.SizeInBytes = vbByteSize;
	geo.VertexByteStride = sizeof(Witchcraft::Animation::SkinnedVertex);

	geo.indexBufferView.BufferLocation = geo.IndexBufferGPU->GetGPUVirtualAddress();
	geo.indexBufferView.Format = m_engine->GetD3DWindow()->GetIndexBufferFormat();
	geo.indexBufferView.SizeInBytes = ibByteSize;

	m_engine->GetD3DWindow()->AddShapeGeometry(&geo);

	ThrowIfFailed(uploadCommandList->Close());
	ID3D12CommandList* uploadCommandLists[] = { uploadCommandList.Get() };
	m_engine->GetD3DWindow()->GetCommandQueue()->ExecuteCommandLists(_countof(uploadCommandLists), uploadCommandLists);
	m_engine->GetD3DWindow()->FlushCommandQueue();
	return true;
}

bool AssimpLoader::UploadInlineSkinnedMeshGeometry(
	const std::wstring& geometryName,
	const std::vector<Vertex>& vertices,
	const std::vector<Witchcraft::Animation::VertexBoneInfluence4>& skinning,
	const std::vector<std::uint32_t>& indices) const
{
	ImportedSkinnedMeshData skinnedMeshData;
	return TryBuildInlineSkinnedMeshData(
		geometryName,
		vertices,
		skinning,
		indices,
		&skinnedMeshData) &&
		UploadSkinnedMeshGeometry(geometryName, skinnedMeshData);
}

bool AssimpLoader::UploadAndBindInlineSkinnedMeshGeometry(
	WitchcraECS* ecs,
	SceneEntityBase* entity,
	const std::wstring& geometryName,
	const std::vector<Vertex>& vertices,
	const std::vector<Witchcraft::Animation::VertexBoneInfluence4>& skinning,
	const std::vector<std::uint32_t>& indices,
	const std::wstring& skeletonAssetPath,
	bool rebuildBoundingBox) const
{
	if (ecs == nullptr || entity == nullptr || geometryName.empty())
		return false;

	if (!UploadInlineSkinnedMeshGeometry(geometryName, vertices, skinning, indices))
		return false;

	AggregateGraphicObj aggregateGraphicObj{};
	aggregateGraphicObj.IndexCount = static_cast<UINT>(indices.size());
	aggregateGraphicObj.StartIndexLocation = 0u;
	aggregateGraphicObj.BaseVertexLocation = 0;
	if (!ecs->SetMeshEntityExternalGeometry(entity, geometryName, &aggregateGraphicObj))
		return false;

	if (SkinnedMeshComponent* skinnedMeshComponent = ecs->AddComponent<SkinnedMeshComponent>(entity))
	{
		if (!skeletonAssetPath.empty())
			skinnedMeshComponent->SetSkeletonAssetPath(skeletonAssetPath);
	}

	if (rebuildBoundingBox)
	{
		if (MeshComponent* meshComponent = ecs->GetComponent<MeshComponent>(entity))
		{
			if (TransformComponent* transformComponent = ecs->GetComponent<TransformComponent>(entity))
				meshComponent->CreateBoundingBox(transformComponent);
		}
	}

	return true;
}


bool AssimpLoader::ImportSkinnedModelAssets(
	const std::wstring& path,
	const std::wstring& rootName,
	std::filesystem::path* outSkeletonFilePath,
	std::vector<std::filesystem::path>* outAnimationFilePaths,
	std::filesystem::path* outSkinnedMeshFilePath)
{
	ImportedSkinnedModelBundle bundle;
	if (!ExtractSkinnedModelBundle(path, rootName, &bundle))
		return false;

	return SaveImportedSkinnedModelBundle(
		bundle,
		rootName.empty() ? std::filesystem::path(path).stem().wstring() : rootName,
		outSkeletonFilePath,
		outAnimationFilePaths,
		outSkinnedMeshFilePath);
}

bool AssimpLoader::ExtractSkinnedModelBundle(
	const std::wstring& path,
	const std::wstring& rootName,
	ImportedSkinnedModelBundle* outBundle) const
{
	if (outBundle == nullptr || path.empty() || !std::filesystem::exists(path))
		return false;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(
		SString::WstringToUTF8(path),
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_CalcTangentSpace |
		aiProcess_GenSmoothNormals |
		aiProcess_JoinIdenticalVertices);
	if (scene == nullptr || scene->mRootNode == nullptr)
		return false;

	const bool useRawGltfAnimationChannels = false;

	ImportedSkinnedModelBundle bundle;
	const std::wstring importBaseName =
		SanitizeName(rootName.empty() ? std::filesystem::path(path).stem().wstring() : rootName);
	bundle.Skeleton.Name = importBaseName;
	bundle.SkinnedMesh.Name = importBaseName;
	bundle.SkinnedMesh.SkeletonName = importBaseName;

	const aiNode* importReferenceRootNode = FindCollapsedImportRootNode(scene);
	const aiMatrix4x4 importReferenceRootGlobal =
		importReferenceRootNode != nullptr ? ComputeNodeGlobalTransform(importReferenceRootNode) : aiMatrix4x4();
	aiMatrix4x4 importReferenceRootGlobalInverse = importReferenceRootGlobal;
	importReferenceRootGlobalInverse.Inverse();

	const std::filesystem::path modelPath(path);
	const std::filesystem::path importedBaseDir = std::filesystem::path(EngineUtils::GetProjectDirPath()) / L"ImportedAssets" / importBaseName;
	const std::filesystem::path extractedTextureDir = importedBaseDir / L"Textures";

	for (UINT materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
	{
		bundle.Materials.push_back(BuildImportedMaterial(
			scene->mMaterials[materialIndex],
			scene,
			modelPath,
			extractedTextureDir,
			materialIndex));
	}

	std::set<std::wstring> boneNames;
	std::unordered_map<std::wstring, aiMatrix4x4> inverseBindByBoneName;
	for (UINT meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
	{
		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (mesh == nullptr)
			continue;

		const aiNode* meshNode = FindNodeReferencingMeshIndexRecursive(scene->mRootNode, meshIndex);
		const aiMatrix4x4 meshGlobalTransform =
			meshNode != nullptr ? ComputeNodeGlobalTransform(meshNode) : aiMatrix4x4();
		aiMatrix4x4 meshGlobalInverse = meshGlobalTransform;
		meshGlobalInverse.Inverse();

		for (UINT boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
		{
			const aiBone* bone = mesh->mBones[boneIndex];
			if (bone == nullptr)
				continue;

			const std::wstring boneName = SanitizeName(SString::UTF8ToWstring(bone->mName.C_Str()));
			if (boneName.empty())
				continue;

			boneNames.insert(boneName);
			const aiMatrix4x4 normalizedInverseBind =
				bone->mOffsetMatrix * meshGlobalInverse * importReferenceRootGlobal;
			const auto existingInverseBindIt = inverseBindByBoneName.find(boneName);
			if (existingInverseBindIt == inverseBindByBoneName.end())
				inverseBindByBoneName.emplace(boneName, normalizedInverseBind);
		}
	}

	if (boneNames.empty())
		return false;

	std::unordered_map<std::wstring, std::uint32_t> boneIndexByName;
	std::function<void(const aiNode*, std::int32_t, const aiNode*)> collectSkeletonRecursive =
		[&](const aiNode* node, std::int32_t parentBoneIndex, const aiNode* parentBoneNode)
		{
			if (node == nullptr)
				return;

			const std::wstring nodeName = SanitizeName(SString::UTF8ToWstring(node->mName.C_Str()));
			std::int32_t currentBoneIndex = parentBoneIndex;
			const aiNode* currentParentBoneNode = parentBoneNode;
			if (boneNames.find(nodeName) != boneNames.end())
			{
				currentBoneIndex = static_cast<std::int32_t>(bundle.Skeleton.Topology.Bones.size());
				Witchcraft::Animation::SkeletonBone bone;
				bone.Name = nodeName;
				bone.ParentIndex = parentBoneIndex;
				bone.BindLocalPose =
					ConvertAiBoneLocalPose(
						parentBoneNode != nullptr
						? ComputeBoneLocalTransformRelativeToAncestorBone(node, parentBoneNode)
						: importReferenceRootGlobalInverse * ComputeNodeGlobalTransform(node));
				bone.BindGlobalMatrix =
					MathHelps::ConvertAiMatrixToFloat4x4(importReferenceRootGlobalInverse * ComputeNodeGlobalTransform(node));

				const auto inverseBindIt = inverseBindByBoneName.find(nodeName);
				if (inverseBindIt != inverseBindByBoneName.end())
					bone.InverseBindPose = MathHelps::ConvertAiMatrixToFloat4x4(inverseBindIt->second);

				boneIndexByName[nodeName] = static_cast<std::uint32_t>(currentBoneIndex);
				bundle.Skeleton.Topology.Bones.push_back(std::move(bone));
				currentParentBoneNode = node;
			}

			for (UINT childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
				collectSkeletonRecursive(node->mChildren[childIndex], currentBoneIndex, currentParentBoneNode);
		};

	collectSkeletonRecursive(scene->mRootNode, -1, nullptr);

	for (const std::wstring& boneName : boneNames)
	{
		if (boneIndexByName.find(boneName) != boneIndexByName.end())
			continue;

		Witchcraft::Animation::SkeletonBone bone;
		bone.Name = boneName;
		const aiNode* node = FindNodeByNameRecursive(scene->mRootNode, boneName);
		if (node != nullptr)
		{
			const aiNode* parentBoneNode = FindNearestAncestorBoneNode(node, boneNames);
			if (parentBoneNode != nullptr)
			{
				const std::wstring parentBoneName = SanitizeName(SString::UTF8ToWstring(parentBoneNode->mName.C_Str()));
				const auto parentBoneIt = boneIndexByName.find(parentBoneName);
				if (parentBoneIt != boneIndexByName.end())
					bone.ParentIndex = static_cast<std::int32_t>(parentBoneIt->second);
			}

			bone.BindLocalPose =
				ConvertAiBoneLocalPose(
					parentBoneNode != nullptr
					? ComputeBoneLocalTransformRelativeToAncestorBone(node, parentBoneNode)
					: importReferenceRootGlobalInverse * ComputeNodeGlobalTransform(node));
			bone.BindGlobalMatrix =
				MathHelps::ConvertAiMatrixToFloat4x4(importReferenceRootGlobalInverse * ComputeNodeGlobalTransform(node));
		}

		const auto inverseBindIt = inverseBindByBoneName.find(boneName);
		if (inverseBindIt != inverseBindByBoneName.end())
			bone.InverseBindPose = MathHelps::ConvertAiMatrixToFloat4x4(inverseBindIt->second);

		boneIndexByName[boneName] = static_cast<std::uint32_t>(bundle.Skeleton.Topology.Bones.size());
		bundle.Skeleton.Topology.Bones.push_back(std::move(bone));
	}

	bundle.Skeleton.Topology.RebuildNameToIndexMap();
	{
		std::vector<aiMatrix4x4> reconstructedBindGlobals(
			bundle.Skeleton.Topology.Bones.size(),
			aiMatrix4x4());
		std::vector<bool> hasInverseBindDerivedGlobal(
			bundle.Skeleton.Topology.Bones.size(),
			false);

		for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(bundle.Skeleton.Topology.Bones.size()); ++boneIndex)
		{
			const auto inverseBindIt = inverseBindByBoneName.find(bundle.Skeleton.Topology.Bones[boneIndex].Name);
			if (inverseBindIt == inverseBindByBoneName.end())
				continue;

			aiMatrix4x4 bindGlobal = inverseBindIt->second;
			bindGlobal.Inverse();
			reconstructedBindGlobals[boneIndex] = bindGlobal;
			hasInverseBindDerivedGlobal[boneIndex] = true;
		}

		for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(bundle.Skeleton.Topology.Bones.size()); ++boneIndex)
		{
			if (!hasInverseBindDerivedGlobal[boneIndex])
			{
				reconstructedBindGlobals[boneIndex] =
					MathHelps::ConvertFloat4x4ToAiMatrix(bundle.Skeleton.Topology.Bones[boneIndex].BindGlobalMatrix);
			}

			bundle.Skeleton.Topology.Bones[boneIndex].BindGlobalMatrix =
				MathHelps::ConvertAiMatrixToFloat4x4(reconstructedBindGlobals[boneIndex]);
		}

		for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(bundle.Skeleton.Topology.Bones.size()); ++boneIndex)
		{
			const std::int32_t parentIndex = bundle.Skeleton.Topology.Bones[boneIndex].ParentIndex;
			aiMatrix4x4 bindLocalMatrix = reconstructedBindGlobals[boneIndex];
			if (parentIndex >= 0 &&
				parentIndex < static_cast<std::int32_t>(reconstructedBindGlobals.size()))
			{
				aiMatrix4x4 parentBindGlobal = reconstructedBindGlobals[static_cast<std::uint32_t>(parentIndex)];
				parentBindGlobal.Inverse();
				bindLocalMatrix = parentBindGlobal * bindLocalMatrix;
			}

			bundle.Skeleton.Topology.Bones[boneIndex].BindLocalPose =
				ConvertAiBoneLocalPose(bindLocalMatrix);
		}
	}

	for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(bundle.Skeleton.Topology.Bones.size()); ++boneIndex)
	{
		if (bundle.Skeleton.Topology.Bones[boneIndex].ParentIndex < 0)
		{
			bundle.Skeleton.Topology.RootBoneIndex = static_cast<std::int32_t>(boneIndex);
			break;
		}
	}

	// aiBone::mWeights 使用的是所属 aiMesh 的局部顶点索引。保留每个子网格的
	// 顶点顺序，才能和 ConvertModelToWModel 中写入 .wmodel 的顶点流一一对应。
	// 两个导入路径都使用 JoinIdenticalVertices，避免这份蒙皮流和模型顶点流发生偏移。
	std::uint32_t mergedBaseVertex = 0;
	bool hasWeightedSkinnedMesh = false;
	for (UINT meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
	{
		aiMesh* mesh = scene->mMeshes[meshIndex];
		if (mesh == nullptr)
			continue;

		Mesh staticMesh = ProcessRawMesh(scene->mRootNode, mesh, scene);
		if (staticMesh.vertices.size() != static_cast<size_t>(mesh->mNumVertices))
			return false;

		std::vector<std::vector<std::pair<std::uint32_t, float>>> influencesByVertex(mesh->mNumVertices);
		for (UINT meshBoneIndex = 0; meshBoneIndex < mesh->mNumBones; ++meshBoneIndex)
		{
			const aiBone* bone = mesh->mBones[meshBoneIndex];
			if (bone == nullptr)
				continue;

			const std::wstring boneName = SanitizeName(SString::UTF8ToWstring(bone->mName.C_Str()));
			const auto boneIndexIt = boneIndexByName.find(boneName);
			if (boneIndexIt == boneIndexByName.end())
				continue;

			for (UINT weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
			{
				const aiVertexWeight& sourceWeight = bone->mWeights[weightIndex];
				if (sourceWeight.mVertexId >= mesh->mNumVertices ||
					!std::isfinite(sourceWeight.mWeight) || sourceWeight.mWeight <= 0.0f)
					continue;

				influencesByVertex[sourceWeight.mVertexId].emplace_back(
					boneIndexIt->second,
					sourceWeight.mWeight);
			}
		}

		const bool isSkinnedMesh = mesh->mNumBones > 0;
		if (isSkinnedMesh)
		{
			for (UINT vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
			{
				auto& sourceInfluences = influencesByVertex[vertexIndex];
				if (sourceInfluences.empty())
					return false;

				std::sort(sourceInfluences.begin(), sourceInfluences.end(),
					[](const auto& lhs, const auto& rhs)
					{
						return lhs.second > rhs.second;
					});

				Witchcraft::Animation::SkinnedVertex skinnedVertex;
				skinnedVertex.StaticVertex = staticMesh.vertices[vertexIndex];
				const size_t influenceCount = (std::min)(
					sourceInfluences.size(),
					static_cast<size_t>(Witchcraft::Animation::MaxBoneInfluenceCountPerVertex));
				for (size_t influenceIndex = 0; influenceIndex < influenceCount; ++influenceIndex)
				{
					skinnedVertex.Skinning.BoneIndices[influenceIndex] = sourceInfluences[influenceIndex].first;
					skinnedVertex.Skinning.BoneWeights[influenceIndex] = sourceInfluences[influenceIndex].second;
				}
				skinnedVertex.Skinning.Normalize();
				bundle.SkinnedMesh.Vertices.push_back(std::move(skinnedVertex));
			}
			hasWeightedSkinnedMesh = true;
		}
		else
		{
			for (const Vertex& staticVertex : staticMesh.vertices)
			{
				Witchcraft::Animation::SkinnedVertex vertex;
				vertex.StaticVertex = staticVertex;
				bundle.SkinnedMesh.Vertices.push_back(std::move(vertex));
			}
		}

		ImportedSkinnedSubmeshData submesh;
		submesh.Name = ResolveModelMeshName(mesh, meshIndex, L"Mesh");
		submesh.IndexStart = static_cast<std::uint32_t>(bundle.SkinnedMesh.Indices.size());
		submesh.IndexCount = static_cast<std::uint32_t>(staticMesh.indices32.size());
		submesh.BaseVertex = mergedBaseVertex;
		bundle.SkinnedMesh.Submeshes.push_back(std::move(submesh));
		for (std::uint32_t index : staticMesh.indices32)
			bundle.SkinnedMesh.Indices.push_back(mergedBaseVertex + index);
		mergedBaseVertex += static_cast<std::uint32_t>(staticMesh.vertices.size());
	}

	if (!hasWeightedSkinnedMesh)
		return false;

	for (UINT animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
	{
		const aiAnimation* animation = scene->mAnimations[animationIndex];
		if (animation == nullptr)
			continue;

		ImportedAnimationClipData clipData;
		clipData.Clip.Name = SanitizeName(SString::UTF8ToWstring(animation->mName.C_Str()));
		if (clipData.Clip.Name.empty())
			clipData.Clip.Name = L"Clip_" + std::to_wstring(animationIndex);
		clipData.Clip.Duration = static_cast<float>(animation->mDuration);
		clipData.Clip.TicksPerSecond = animation->mTicksPerSecond > 0.0 ? static_cast<float>(animation->mTicksPerSecond) : 25.0f;
		clipData.Clip.Loop = true;

		std::unordered_map<std::wstring, const aiNodeAnim*> animationChannelByName;
		animationChannelByName.reserve(animation->mNumChannels);
		std::unordered_map<std::wstring, const aiNodeAnim*> animationChannelByCanonicalName;
		animationChannelByCanonicalName.reserve(animation->mNumChannels);
		const auto makeAnimationChannelLookupKey = [](const std::wstring& name)
		{
			std::wstring key;
			key.reserve(name.size());
			for (const wchar_t ch : name)
			{
				if (std::iswalnum(ch))
					key.push_back(static_cast<wchar_t>(std::towlower(ch)));
			}
			return key;
		};
		std::vector<double> clipKeyTimes;
		for (UINT channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
		{
			const aiNodeAnim* channel = animation->mChannels[channelIndex];
			if (channel == nullptr)
				continue;

			const std::wstring nodeName = SanitizeName(SString::UTF8ToWstring(channel->mNodeName.C_Str()));
			if (!nodeName.empty())
			{
				animationChannelByName[nodeName] = channel;
				const std::wstring canonicalNodeName = makeAnimationChannelLookupKey(nodeName);
				if (!canonicalNodeName.empty())
					animationChannelByCanonicalName.emplace(canonicalNodeName, channel);
			}

			for (UINT keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex)
			{
				const double keyTime = channel->mPositionKeys[keyIndex].mTime;
				if (std::isfinite(keyTime))
					clipKeyTimes.push_back(NormalizeImportedAnimationTime(keyTime));
			}
			for (UINT keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex)
			{
				const double keyTime = channel->mRotationKeys[keyIndex].mTime;
				if (std::isfinite(keyTime))
					clipKeyTimes.push_back(NormalizeImportedAnimationTime(keyTime));
			}
			for (UINT keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex)
			{
				const double keyTime = channel->mScalingKeys[keyIndex].mTime;
				if (std::isfinite(keyTime))
					clipKeyTimes.push_back(NormalizeImportedAnimationTime(keyTime));
			}
		}

		const auto findAnimationChannel =
			[&](const std::wstring& nodeName) -> const aiNodeAnim*
			{
				const auto exactIt = animationChannelByName.find(nodeName);
				if (exactIt != animationChannelByName.end())
					return exactIt->second;

				const std::wstring canonicalNodeName = makeAnimationChannelLookupKey(nodeName);
				const auto canonicalIt = animationChannelByCanonicalName.find(canonicalNodeName);
				return canonicalIt != animationChannelByCanonicalName.end() ? canonicalIt->second : nullptr;
			};

		if (clipKeyTimes.empty())
			clipKeyTimes.push_back(0.0);
		std::sort(clipKeyTimes.begin(), clipKeyTimes.end());
		clipKeyTimes.erase(
			std::unique(
				clipKeyTimes.begin(),
				clipKeyTimes.end(),
				[](double lhs, double rhs)
				{
					return std::abs(lhs - rhs) <= 0.000001;
				}),
			clipKeyTimes.end());

		clipData.Clip.Tracks.clear();
		clipData.Clip.Tracks.reserve(bundle.Skeleton.Topology.Bones.size());
		for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(bundle.Skeleton.Topology.Bones.size()); ++boneIndex)
		{
			Witchcraft::Animation::BoneAnimationTrack track;
			track.BoneName = bundle.Skeleton.Topology.Bones[boneIndex].Name;
			track.BoneIndex = static_cast<std::int32_t>(boneIndex);
			clipData.Clip.Tracks.push_back(std::move(track));
		}

		std::function<aiMatrix4x4(const aiNode*, double, std::unordered_map<const aiNode*, aiMatrix4x4>&)> evaluateNodeGlobalAtTime;
		evaluateNodeGlobalAtTime =
			[&](const aiNode* node, double time, std::unordered_map<const aiNode*, aiMatrix4x4>& globalCache) -> aiMatrix4x4
			{
				if (node == nullptr)
					return aiMatrix4x4();

				auto cacheIt = globalCache.find(node);
				if (cacheIt != globalCache.end())
					return cacheIt->second;

				const std::wstring nodeName = SanitizeName(SString::UTF8ToWstring(node->mName.C_Str()));
				const Witchcraft::Animation::BoneLocalPose bindLocalPose =
					ConvertAiBoneLocalPose(node->mTransformation);
				Witchcraft::Animation::BoneLocalPose localPose = bindLocalPose;
				const aiNodeAnim* channel = findAnimationChannel(nodeName);
				if (channel != nullptr)
				{
					localPose = SampleAiNodeAnimLocalPose(
						channel, time, localPose, useRawGltfAnimationChannels);
					// aiNodeAnim存储glTF节点的绝对本地TRS。
					// 将绑定方向附近的关键帧视为增量会应用两次绑定旋转，这会在蒙皮开始之前更改第一帧。
				}

				const aiMatrix4x4 localMatrix = BuildAiMatrixFromBoneLocalPose(localPose);
				aiMatrix4x4 globalMatrix = localMatrix;
				if (node->mParent != nullptr)
					globalMatrix = evaluateNodeGlobalAtTime(node->mParent, time, globalCache) * localMatrix;

				globalCache.emplace(node, globalMatrix);
				return globalMatrix;
			};

		auto evaluateBoneGlobalInSourceSpace =
			[&](const aiNode* boneNode, double time, std::unordered_map<const aiNode*, aiMatrix4x4>& globalCache) -> aiMatrix4x4
			{
				// 动画轨迹携带节点局部变换。在此保持其源空间层次结构不变；
				// BindGlobalMatrix和InverseBindPose仅在运行时构建蒙皮调色板时使用。
				return importReferenceRootGlobalInverse *
					evaluateNodeGlobalAtTime(boneNode, time, globalCache);
			};

		for (auto& track : clipData.Clip.Tracks)
		{
			const aiNode* boneNode = FindNodeByNameRecursive(scene->mRootNode, track.BoneName);
			if (boneNode == nullptr)
				continue;

			const aiNodeAnim* directChannel = findAnimationChannel(track.BoneName);
			std::vector<double> directKeyTimes;
			if (directChannel != nullptr)
				CollectChannelKeyTimes(directChannel, directKeyTimes);

			if (directChannel != nullptr && !directKeyTimes.empty())
			{
				track.TranslationKeys.clear();
				track.RotationKeys.clear();
				track.ScaleKeys.clear();
				track.MatrixKeys.clear();

				// glTF/Assimp 的 aiNodeAnim 保存节点的局部 TRS。
				// 直接导出该局部姿势；先重建全局矩阵、求父逆再分解会在非均匀缩放
				// 或坐标转换链中引入不可分解矩阵，并会把整条动画轨道错误地丢弃。
				const Witchcraft::Animation::BoneLocalPose fallbackLocalPose =
					ConvertAiBoneLocalPose(boneNode->mTransformation);
				const bool isSkeletonRootTrack =
					track.BoneIndex >= 0 &&
					static_cast<size_t>(track.BoneIndex) < bundle.Skeleton.Topology.Bones.size() &&
					bundle.Skeleton.Topology.Bones[static_cast<size_t>(track.BoneIndex)].ParentIndex < 0;

				for (double keyTime : directKeyTimes)
				{
					const Witchcraft::Animation::BoneLocalPose rawLocalPose =
						SampleAiNodeAnimLocalPose(
							directChannel, keyTime, fallbackLocalPose, useRawGltfAnimationChannels);
					Witchcraft::Animation::BoneLocalPose localPose = rawLocalPose;

					// 参考根（常见于 Blender glTF 的 Armature X=-90° / Scale=0.01）
					// 已从静态层级与 bind pose 中剥离。根骨动画同样必须转换到该归一化
					// 空间，否则播放时会重新引入横躺和 100 倍缩放；子骨仍保持各自原始局部 TRS。
					if (isSkeletonRootTrack)
					{
						const aiMatrix4x4 normalizedRootLocal =
							importReferenceRootGlobal * BuildAiMatrixFromBoneLocalPose(rawLocalPose);
						localPose = ConvertAiBoneLocalPose(normalizedRootLocal);
					}
					if (!IsFiniteFloat3(localPose.Translation) ||
						!IsFiniteFloat4(localPose.Rotation) ||
						!IsFiniteFloat3(localPose.Scale))
					{
						continue;
					}

					Witchcraft::Animation::BoneTranslationKey translationKey;
					translationKey.Time = static_cast<float>(keyTime);
					translationKey.Value = localPose.Translation;
					track.TranslationKeys.push_back(translationKey);

					Witchcraft::Animation::BoneRotationKey rotationKey;
					rotationKey.Time = static_cast<float>(keyTime);
					rotationKey.Value = localPose.Rotation;
					track.RotationKeys.push_back(rotationKey);

					Witchcraft::Animation::BoneScaleKey scaleKey;
					scaleKey.Time = static_cast<float>(keyTime);
					scaleKey.Value = localPose.Scale;
					track.ScaleKeys.push_back(scaleKey);

					Witchcraft::Animation::BoneMatrixKey matrixKey;
					matrixKey.Time = static_cast<float>(keyTime);
					// BoneLocalPose 位于 Witchcraft::Animation 命名空间；非限定调用会同时
					// 命中 ADL 找到的公共函数和当前导入器辅助函数，导致 VS 的重载歧义。
					matrixKey.Value = Witchcraft::Animation::ComposeBoneLocalPoseMatrix(localPose);
					track.MatrixKeys.push_back(matrixKey);
				}
			}
			else
			{
				track.TranslationKeys.clear();
				track.RotationKeys.clear();
				track.ScaleKeys.clear();
				track.MatrixKeys.clear();

				const aiNode* parentBoneNode = FindNearestAncestorBoneNode(boneNode, boneNames);
				const Witchcraft::Animation::BoneLocalPose fallbackLocalPose =
					ConvertAiBoneLocalPose(boneNode->mTransformation);
				const aiNodeAnim* localChannel = findAnimationChannel(track.BoneName);
				for (double keyTime : clipKeyTimes)
				{
					std::unordered_map<const aiNode*, aiMatrix4x4> globalCache;
					const aiMatrix4x4 boneGlobal = evaluateBoneGlobalInSourceSpace(boneNode, keyTime, globalCache);

					aiMatrix4x4 localMatrix = boneGlobal;
					if (parentBoneNode != nullptr)
					{
						aiMatrix4x4 parentGlobal = evaluateBoneGlobalInSourceSpace(parentBoneNode, keyTime, globalCache);
						parentGlobal.Inverse();
						localMatrix = parentGlobal * boneGlobal;
					}
					else
						localMatrix = boneGlobal;

					Witchcraft::Animation::BoneLocalPose rebasedPose = ConvertAiBoneLocalPose(localMatrix);

					if (!IsFiniteFloat3(rebasedPose.Translation) ||
						!IsFiniteFloat4(rebasedPose.Rotation) ||
						!IsFiniteFloat3(rebasedPose.Scale))
					{
						continue;
					}

					Witchcraft::Animation::BoneTranslationKey translationKey;
					translationKey.Time = static_cast<float>(keyTime);
					translationKey.Value = rebasedPose.Translation;
					track.TranslationKeys.push_back(translationKey);

					Witchcraft::Animation::BoneRotationKey rotationKey;
					rotationKey.Time = static_cast<float>(keyTime);
					rotationKey.Value = rebasedPose.Rotation;
					track.RotationKeys.push_back(rotationKey);

					Witchcraft::Animation::BoneScaleKey scaleKey;
					scaleKey.Time = static_cast<float>(keyTime);
					scaleKey.Value = rebasedPose.Scale;
					track.ScaleKeys.push_back(scaleKey);

					// 转置：aiMatrix4x4 是行优先的，但运行时动画系统（SkeletonPoseSystem / SkinningPaletteSystem）以列优先布局存储 XMFLOAT4X4，
					// 因此通过 ConvertFloat4x4ToAiMatrix / ConvertAiMatrixToFloat4x4 进行往返转换会产生正确的 aiMatrix4x4 值。
					Witchcraft::Animation::BoneMatrixKey matrixKey;
					matrixKey.Time = static_cast<float>(keyTime);
					matrixKey.Value = MathHelps::ConvertAiMatrixToFloat4x4(localMatrix);
					track.MatrixKeys.push_back(matrixKey);
				}
			}

			std::sort(track.TranslationKeys.begin(), track.TranslationKeys.end(),
				[](const Witchcraft::Animation::BoneTranslationKey& lhs, const Witchcraft::Animation::BoneTranslationKey& rhs)
				{
					return lhs.Time < rhs.Time;
				});
			std::sort(track.RotationKeys.begin(), track.RotationKeys.end(),
				[](const Witchcraft::Animation::BoneRotationKey& lhs, const Witchcraft::Animation::BoneRotationKey& rhs)
				{
					return lhs.Time < rhs.Time;
				});
			std::sort(track.ScaleKeys.begin(), track.ScaleKeys.end(),
				[](const Witchcraft::Animation::BoneScaleKey& lhs, const Witchcraft::Animation::BoneScaleKey& rhs)
				{
					return lhs.Time < rhs.Time;
				});
			std::sort(track.MatrixKeys.begin(), track.MatrixKeys.end(),
				[](const Witchcraft::Animation::BoneMatrixKey& lhs, const Witchcraft::Animation::BoneMatrixKey& rhs)
				{
					return lhs.Time < rhs.Time;
				});

			// 动画轨迹是由本地姿势创作的。允许片段从绑定姿势开始，因此保持采样值不变。
			// 对每一个不匹配的第一帧重新设置基线会独立地更改每个骨骼的参考空间，并使动画层次路径与源动画偏离。

			AssimpImpl::CompressTranslationKeys(track.TranslationKeys);
			AssimpImpl::CompressRotationKeys(track.RotationKeys);
			AssimpImpl::CompressScaleKeys(track.ScaleKeys);

		}

		bundle.Animations.push_back(std::move(clipData));
	}

	*outBundle = std::move(bundle);
	return true;
}

bool AssimpLoader::SaveImportedSkinnedModelBundle(
	const ImportedSkinnedModelBundle& bundle,
	const std::wstring& rootName,
	std::filesystem::path* outSkeletonFilePath,
	std::vector<std::filesystem::path>* outAnimationFilePaths,
	std::filesystem::path* outSkinnedMeshFilePath) const
{
	if (outSkeletonFilePath != nullptr)
		outSkeletonFilePath->clear();
	if (outSkinnedMeshFilePath != nullptr)
		outSkinnedMeshFilePath->clear();
	if (outAnimationFilePaths != nullptr)
		outAnimationFilePaths->clear();

	if (bundle.Skeleton.Topology.Bones.empty())
		return false;

	const std::wstring importBaseName = SanitizeName(rootName.empty() ? bundle.Skeleton.Name : rootName);
	const std::filesystem::path importedBaseDir = std::filesystem::path(EngineUtils::GetProjectDirPath()) / L"ImportedAssets" / importBaseName;
	const std::filesystem::path animationDir = importedBaseDir / L"Animation";
	std::filesystem::create_directories(animationDir);

	//导入的.wmodel使用稳定名称引用骨架和片段。
	//重新导入相同的GLB必须刷新这些资源，而不是将场景绑定到早期的动画烘焙。
	const std::filesystem::path skeletonFilePath = animationDir / BuildImportedSkeletonFileName(importBaseName);
	WSkeletonFileData skeletonData;
	skeletonData.Name = bundle.Skeleton.Name;
	skeletonData.Topology = bundle.Skeleton.Topology;
	if (!WSkeletonFile::SaveToFile(skeletonFilePath, skeletonData))
		return false;

	if (outSkeletonFilePath != nullptr)
		*outSkeletonFilePath = skeletonFilePath;

	for (const ImportedAnimationClipData& clip : bundle.Animations)
	{
		const std::filesystem::path animationFilePath =
			animationDir / BuildImportedAnimationFileName(importBaseName, clip.Clip.Name);
		WAnimationFileData animationData;
		animationData.Clip = clip.Clip;
		if (!WAnimationFile::SaveToFile(animationFilePath, animationData))
			return false;

		if (outAnimationFilePaths != nullptr)
			outAnimationFilePaths->push_back(animationFilePath);
	}

	if (outSkinnedMeshFilePath != nullptr)
		outSkinnedMeshFilePath->clear();

	if (m_consoleWindow != nullptr)
	{
		m_consoleWindow->AddInfoMessage(
			L"[Import][Skinned] 导出完成：skeleton=%s, animations=%u",
			skeletonFilePath.filename().wstring().c_str(),
			static_cast<UINT>(bundle.Animations.size()));
	}

	return true;
}

std::vector<Mesh> AssimpLoader::LoadRawModel(std::wstring path)
{
	assert(!path.empty());
	assert(std::filesystem::exists(path));

	std::vector<Mesh> buffer;

	Assimp::Importer importer;
	const aiScene* pScene = importer.ReadFile(SString::WstringToUTF8(path), aiProcess_Triangulate | aiProcess_ConvertToLeftHanded);

	if (pScene == nullptr)
		return buffer;

	// 将整棵节点树中的子网格拍平到一个数组，便于后续构建 Mesh。
	ProcessRawNode(pScene->mRootNode, pScene, buffer);
	return buffer;
}

void AssimpLoader::ProcessRawNode(aiNode* node, const aiScene* scene, std::vector<Mesh>& arg)
{
	// 当前节点引用的每个 aiMesh 都独立转成一个 Mesh，保留子网格边界。
	for (UINT i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		arg.push_back(ProcessRawMesh(node, mesh, scene));
	}

	for (UINT i = 0; i < node->mNumChildren; i++)
		ProcessRawNode(node->mChildren[i], scene, arg);
}

Mesh AssimpLoader::ProcessRawMesh(aiNode* node, aiMesh* mesh, const aiScene* scene) const
{
	Mesh buffer;
	bool needBounds = false;
	float xMin = FLT_MAX, xMax = -FLT_MAX;
	float yMin = FLT_MAX, yMax = -FLT_MAX;
	float zMin = FLT_MAX, zMax = -FLT_MAX;
	if (needBounds)
	{
		for (UINT v = 0; v < mesh->mNumVertices; ++v)
		{
			if (mesh->mVertices[v].x < xMin) xMin = mesh->mVertices[v].x;
			if (mesh->mVertices[v].x > xMax) xMax = mesh->mVertices[v].x;
			if (mesh->mVertices[v].y < yMin) yMin = mesh->mVertices[v].y;
			if (mesh->mVertices[v].y > yMax) yMax = mesh->mVertices[v].y;
			if (mesh->mVertices[v].z < zMin) zMin = mesh->mVertices[v].z;
			if (mesh->mVertices[v].z > zMax) zMax = mesh->mVertices[v].z;
		}
	}
	const float dx = xMax - xMin, dy = yMax - yMin, dz = zMax - zMin;
	const float yRange = (dy > 0.0001f) ? dy : 1.0f;
	float maxExtent = dx; if (dy > maxExtent) maxExtent = dy; if (dz > maxExtent) maxExtent = dz;
	// 平面判定：某方向极薄（< 10% 最大范围）
	const bool isPlanar =
		(dx / maxExtent < 0.1f) || (dy / maxExtent < 0.1f) || (dz / maxExtent < 0.1f);

	// 原始导入阶段只做数据搬运，不在这里推导材质或场景对象。
	for (UINT i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;

		// positions
		vertex.Pos.x = mesh->mVertices[i].x;
		vertex.Pos.y = mesh->mVertices[i].y;
		vertex.Pos.z = mesh->mVertices[i].z;

		// color
		if (mesh->mColors[0])
		{
			vertex.Color.x = mesh->mColors[0][i].r;
			vertex.Color.y = mesh->mColors[0][i].g;
			vertex.Color.z = mesh->mColors[0][i].b;
			vertex.Color.w = mesh->mColors[0][i].a;
		}
		else
		{
			vertex.Color.x = 0.0f;
			vertex.Color.y = 0.0f;
			vertex.Color.z = 0.0f;
			vertex.Color.w = 1.0f;
		}

		// normals
		if (mesh->HasNormals())
		{
			vertex.Normal.x = mesh->mNormals[i].x;
			vertex.Normal.y = mesh->mNormals[i].y;
			vertex.Normal.z = mesh->mNormals[i].z;
		}

		// texture coordinates
		if (mesh->mTextureCoords[0])
		{
			vertex.TexC.x = mesh->mTextureCoords[0][i].x;
			vertex.TexC.y = mesh->mTextureCoords[0][i].y;
		}
		else if (mesh->HasNormals())
		{
			if (isPlanar)
			{
				// 平面投影：取最薄方向以外的两轴做 UV
				if (dy / maxExtent < 0.1f || dz / maxExtent < 0.1f)
				{
					// Y 或 Z 薄 → 投影到 XZ 或 XY
					if (dy / maxExtent < 0.1f)
					{
						vertex.TexC.x = (mesh->mVertices[i].x - xMin) / (dx > 0.0001f ? dx : 1.0f);
						vertex.TexC.y = (mesh->mVertices[i].z - zMin) / (dz > 0.0001f ? dz : 1.0f);
					}
					else
					{
						vertex.TexC.x = (mesh->mVertices[i].x - xMin) / (dx > 0.0001f ? dx : 1.0f);
						vertex.TexC.y = (mesh->mVertices[i].y - yMin) / (dy > 0.0001f ? dy : 1.0f);
					}
				}
				else
				{
					// X 薄 → 投影到 YZ
					vertex.TexC.x = (mesh->mVertices[i].y - yMin) / (dy > 0.0001f ? dy : 1.0f);
					vertex.TexC.y = (mesh->mVertices[i].z - zMin) / (dz > 0.0001f ? dz : 1.0f);
				}
			}
			else
			{
				// 圆柱投影：U=方位角, V=Y 线性映射
				vertex.TexC.x = std::atan2(-mesh->mVertices[i].z, mesh->mVertices[i].x) / (2.0f * DirectX::XM_PI) + 0.5f;
				vertex.TexC.y = (mesh->mVertices[i].y - yMin) / yRange;
			}
		}
		else
		{
			vertex.TexC = DirectX::XMFLOAT2(0.0f, 0.0f);
		}

		// Tangents Bitangent
		if (mesh->HasTangentsAndBitangents())
		{
			vertex.Tangent.x = mesh->mTangents[i].x;
			vertex.Tangent.y = mesh->mTangents[i].y;
			vertex.Tangent.z = mesh->mTangents[i].z;
			vertex.Bitangent.x = mesh->mBitangents[i].x;
			vertex.Bitangent.y = mesh->mBitangents[i].y;
			vertex.Bitangent.z = mesh->mBitangents[i].z;
		}
		else
		{
			vertex.Tangent.x = -0.1f;
			vertex.Tangent.y = 0.0f;
			vertex.Tangent.z = +0.1f;
			vertex.Bitangent.x = -0.1f;
			vertex.Bitangent.y = 0.0f;
			vertex.Bitangent.z = +0.1f;
		}

		buffer.vertices.push_back(vertex);
	}

	for (UINT i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (UINT j = 0; j < face.mNumIndices; j++)
			buffer.indices32.push_back(face.mIndices[j]);
	}

	// 仅对无原始 UV 的非平面 mesh 修复极点
	if (!buffer.vertices.empty() && !buffer.indices32.empty() && !mesh->mTextureCoords[0] && !isPlanar)
	{
		std::vector<Vertex>& verts = buffer.vertices;
		std::vector<std::uint32_t>& idxs = buffer.indices32;
		const size_t triCount = idxs.size() / 3;
		const float yEpsilon = yRange * 0.001f;

		// 判定极点顶点
		auto isTopPole = [&](UINT vi) { return std::abs(verts[vi].Pos.y - yMax) < yEpsilon; };
		auto isBotPole = [&](UINT vi) { return std::abs(verts[vi].Pos.y - yMin) < yEpsilon; };

		for (size_t t = 0; t < triCount; ++t)
		{
			const size_t i0 = t * 3, i1 = i0 + 1, i2 = i0 + 2;
			UINT vi[3] = { idxs[i0], idxs[i1], idxs[i2] };

			// 核实索引有效
			if (vi[0] >= verts.size() || vi[1] >= verts.size() || vi[2] >= verts.size())
				continue;

			// 处理极点顶点：为每个面复制一份，U 取对面两顶点中点
			for (int j = 0; j < 3; ++j)
			{
				if (!isTopPole(vi[j]) && !isBotPole(vi[j]))
					continue;
				const UINT vj0 = vi[(j + 1) % 3];
				const UINT vj1 = vi[(j + 2) % 3];
				Vertex dup = verts[vi[j]];
				dup.TexC.x = (verts[vj0].TexC.x + verts[vj1].TexC.x) * 0.5f;
				idxs[(j == 0) ? i0 : (j == 1) ? i1 : i2] = static_cast<UINT>(verts.size());
				verts.push_back(dup);
			}

			// 处理跨接缝：U 极差 > 0.5
			const float u0 = verts[vi[0]].TexC.x;
			const float u1 = verts[vi[1]].TexC.x;
			const float u2 = verts[vi[2]].TexC.x;
			float uMin = u0; if (u1 < uMin) uMin = u1; if (u2 < uMin) uMin = u2;
			float uMax = u0; if (u1 > uMax) uMax = u1; if (u2 > uMax) uMax = u2;
			if (uMax - uMin > 0.5f)
			{
				for (int j = 0; j < 3; ++j)
				{
					const size_t idx = (j == 0) ? i0 : (j == 1) ? i1 : i2;
					const UINT v = idxs[idx];
					if (v >= verts.size()) continue;
					if (verts[v].TexC.x < 0.25f)
					{
						Vertex dup = verts[v];
						dup.TexC.x += 1.0f;
						idxs[idx] = static_cast<UINT>(verts.size());
						verts.push_back(dup);
					}
				}
			}

		}
	}

	return buffer;
}

ImportedTextureSource AssimpLoader::ResolveImportedTexture(
	const aiScene* scene,
	const std::filesystem::path& modelPath,
	const std::filesystem::path& extractedTextureDir,
	const aiString& texturePath,
	UINT materialIndex,
	const wchar_t* slotName) const
{
	ImportedTextureSource result;

	if (texturePath.length == 0)
		return result;

	// 先尝试按 Assimp 的内嵌纹理表解析，适配 glb / fbx 等打包纹理场景。
	const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(texturePath.C_Str());
	if (embeddedTexture != nullptr)
	{
		std::filesystem::create_directories(extractedTextureDir);

		std::string formatHint = embeddedTexture->achFormatHint;
		if (formatHint.empty())
			formatHint = "png";

		std::wstring extension = L"." + SString::UTF8ToWstring(formatHint);
		const std::wstring preferredFileName = ResolvePreferredTextureName(
			texturePath,
			embeddedTexture,
			L"mat_" + std::to_wstring(materialIndex) + L"_" + slotName,
			extension);
		std::filesystem::path outputPath = MakeUniqueFilePath(extractedTextureDir, preferredFileName);

		if (embeddedTexture->mHeight == 0)
		{
			std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
			if (output.is_open())
			{
				output.write(reinterpret_cast<const char*>(embeddedTexture->pcData), embeddedTexture->mWidth);
				output.close();
				result.Path = outputPath.wstring();
				result.AssetName = outputPath.filename().wstring();
			}
		}

		return result;
	}

	// 否则按模型目录下的相对路径解析外部贴图。
	std::filesystem::path resolvedPath = SString::UTF8ToWstring(texturePath.C_Str());
	if (resolvedPath.is_relative())
		resolvedPath = modelPath.parent_path() / resolvedPath;

	resolvedPath = resolvedPath.lexically_normal();
	if (std::filesystem::exists(resolvedPath))
	{
		const std::filesystem::path copiedPath = CopyImportedTextureToProject(resolvedPath, extractedTextureDir);
		if (!copiedPath.empty())
		{
			std::error_code errorCode;
			if (!std::filesystem::exists(copiedPath))
				std::filesystem::copy_file(resolvedPath, copiedPath, std::filesystem::copy_options::overwrite_existing, errorCode);

			if (!errorCode)
			{
				result.Path = copiedPath.wstring();
				result.AssetName = copiedPath.filename().wstring();
			}
		}
	}

	return result;
}

ImportedMaterialInfo AssimpLoader::BuildImportedMaterial(
	aiMaterial* material,
	const aiScene* scene,
	const std::filesystem::path& modelPath,
	const std::filesystem::path& extractedTextureDir,
	UINT materialIndex) const
{
	ImportedMaterialInfo info;
	info.Name = SanitizeName(SString::UTF8ToWstring(material->GetName().C_Str()));
	if (info.Name.empty())
		info.Name = L"Material_" + std::to_wstring(materialIndex);

	// 优先读取 PBR BaseColor，但在引擎侧统一按 Diffuse 颜色处理。
	aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
	if (material->Get(AI_MATKEY_BASE_COLOR, baseColor) != AI_SUCCESS)
		material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);

	info.DiffuseColor = DirectX::XMFLOAT4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);

	aiColor3D emissiveColor(0.0f, 0.0f, 0.0f);
	if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS)
		info.Emissive = DirectX::XMFLOAT3(emissiveColor.r, emissiveColor.g, emissiveColor.b);

	float metallic = 0.0f;
	if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
		info.Metallic = metallic;

	float roughness = 1.0f;
	if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
		info.Roughness = roughness;

	float opacity = baseColor.a;
	material->Get(AI_MATKEY_OPACITY, opacity);
	info.Opacity = opacity;

	// 只抽取材质系统当前会使用的贴图槽。
	aiString texturePath;
	if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath) == AI_SUCCESS ||
		material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS)
	{
		info.DiffuseTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"diffuse");
	}

	if (material->GetTexture(aiTextureType_NORMALS, 0, &texturePath) == AI_SUCCESS ||
		material->GetTexture(aiTextureType_HEIGHT, 0, &texturePath) == AI_SUCCESS)
	{
		info.NormalTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"normal");
	}

	if (material->GetTexture(aiTextureType_SPECULAR, 0, &texturePath) == AI_SUCCESS)
	{
		info.SpecularTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"specular");
	}

	if (material->GetTexture(aiTextureType_METALNESS, 0, &texturePath) == AI_SUCCESS)
	{
		info.MetallicTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"metallic");
	}

	if (material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texturePath) == AI_SUCCESS)
	{
		info.RoughnessTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"roughness");
	}

	if (material->GetTexture(aiTextureType_EMISSIVE, 0, &texturePath) == AI_SUCCESS)
	{
		info.EmissiveTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"emissive");
	}

	if (material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texturePath) == AI_SUCCESS)
	{
		info.AmbientOcclusionTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"ao");
	}

	// 优先读取材质语义里的透明贴图；若存在则自动启用透明分层。
	if (material->GetTexture(aiTextureType_OPACITY, 0, &texturePath) == AI_SUCCESS)
	{
		info.OpacityTexture = ResolveImportedTexture(scene, modelPath, extractedTextureDir, texturePath, materialIndex, L"opacity");
		info.UseOpacityTexture = !info.OpacityTexture.Path.empty();
	}

	// glTF 的 BaseColor alpha 透明通常通过 alphaMode=BLEND 描述，不一定有独立 opacity 贴图槽。
	// 此处按语义自动启用透明分层，避免把普通 PNG 一律当透明材质。
	if (!info.UseOpacityTexture)
	{
		aiString gltfAlphaMode;
		if (material->Get("$mat.gltf.alphaMode", 0, 0, gltfAlphaMode) == AI_SUCCESS)
		{
			std::wstring alphaMode = SString::UTF8ToWstring(gltfAlphaMode.C_Str());
			std::transform(alphaMode.begin(), alphaMode.end(), alphaMode.begin(), towupper);
			if (alphaMode == L"BLEND")
			{
				info.UseOpacityTexture = !info.DiffuseTexture.Path.empty();
				if (info.UseOpacityTexture)
					info.OpacityTexture = info.DiffuseTexture;
			}
		}
	}

	return info;
}

/* ------------------------------------ */

bool AssimpLoader::ConvertModelToWModel(
	const std::wstring& path,
	const std::wstring& rootName,
	const std::filesystem::path* skeletonFilePath,
	std::filesystem::path& outModelFilePath)
{
	outModelFilePath.clear();
	if (path.empty() || !std::filesystem::exists(path))
		return false;

	Assimp::Importer hierarchyImporter;
	hierarchyImporter.SetPropertyInteger(AI_CONFIG_FBX_CONVERT_TO_M, FALSE);

	const aiScene* hierarchyScene = hierarchyImporter.ReadFile(
		SString::WstringToUTF8(path),
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_CalcTangentSpace |
		aiProcess_GenSmoothNormals |
		aiProcess_JoinIdenticalVertices);

	if (hierarchyScene == nullptr || hierarchyScene->mNumMeshes == 0 || hierarchyScene->mRootNode == nullptr)
		return false;

	const std::filesystem::path modelPath(path);
	const std::wstring importBaseName = SanitizeName(rootName.empty() ? modelPath.stem().wstring() : rootName);
	const std::filesystem::path importedAssetDir =
		std::filesystem::path(EngineUtils::GetProjectDirPath()) /
		L"ImportedAssets" /
		importBaseName;
	const std::filesystem::path extractedTextureDir = importedAssetDir / L"Textures";
	const std::filesystem::path importedMaterialDir = importedAssetDir / L"Materials";

	if (m_consoleWindow != nullptr)
	{
		m_consoleWindow->AddInfoMessage(L"[Import] 读取模型：%s", modelPath.filename().c_str());
		m_consoleWindow->AddInfoMessage(L"[Import] 对象数量：%u，材质数量：%u",
			hierarchyScene->mNumMeshes, hierarchyScene->mNumMaterials);
	}

	std::vector<std::wstring> modelMaterialIds(hierarchyScene->mNumMaterials);
	std::vector<std::filesystem::path> materialFilePaths(hierarchyScene->mNumMaterials);
	for (UINT materialIndex = 0; materialIndex < hierarchyScene->mNumMaterials; ++materialIndex)
	{
		ImportedMaterialInfo materialInfo = BuildImportedMaterial(
			hierarchyScene->mMaterials[materialIndex],
			hierarchyScene,
			modelPath,
			extractedTextureDir,
			materialIndex);

		if (m_consoleWindow != nullptr)
		{
			std::vector<std::wstring> textureUsages;
			auto pushTextureUsage = [&](const ImportedTextureSource& source, const wchar_t* slotName)
				{
					const std::wstring usage = BuildTextureUsageSummary(source, slotName);
					if (!usage.empty())
						textureUsages.push_back(usage);
				};

			pushTextureUsage(materialInfo.DiffuseTexture, L"Diffuse");
			pushTextureUsage(materialInfo.NormalTexture, L"Normal");
			pushTextureUsage(materialInfo.SpecularTexture, L"Specular");
			pushTextureUsage(materialInfo.MetallicTexture, L"Metallic");
			pushTextureUsage(materialInfo.RoughnessTexture, L"Roughness");
			pushTextureUsage(materialInfo.EmissiveTexture, L"Emissive");
			pushTextureUsage(materialInfo.AmbientOcclusionTexture, L"AO");

			std::wstring textureSummary = L"无贴图";
			if (!textureUsages.empty())
			{
				textureSummary.clear();
				for (size_t textureIndex = 0; textureIndex < textureUsages.size(); ++textureIndex)
				{
					if (textureIndex > 0)
						textureSummary += L", ";
					textureSummary += textureUsages[textureIndex];
				}
			}

			m_consoleWindow->AddInfoMessage(
				L"[Import] 材质编号[%u] 材质名：%s | 贴图摘要：%s",
				materialIndex,
				materialInfo.Name.c_str(),
				textureSummary.c_str());
		}

		const WMaterialFileData materialFileData = WMaterialFile::FromImportedMaterial(materialInfo);
		const std::filesystem::path materialFilePath =
			BuildImportedMaterialFilePath(importedMaterialDir, importBaseName, materialFileData.MaterialName);
		if (!WMaterialFile::SaveToFile(materialFilePath, materialFileData))
		{
			if (m_consoleWindow != nullptr)
				m_consoleWindow->AddWarningMessage(L"[Import] 材质文件保存失败：%s", materialFilePath.wstring().c_str());
			return false;
		}

		materialFilePaths[materialIndex] = materialFilePath;
		modelMaterialIds[materialIndex] = BuildModelMaterialId(materialIndex);
	}

	WModelFileData modelFileData;
	modelFileData.Name = importBaseName;
	modelFileData.SourceFile = modelPath.wstring();

	ImportedSkinnedModelBundle skinnedBundle;
	const bool sceneContainsBones = SceneContainsBones(hierarchyScene);
	const bool hasSkinnedBundle =
		sceneContainsBones &&
		ExtractSkinnedModelBundle(path, importBaseName, &skinnedBundle);
	if (hasSkinnedBundle)
	{
		if (skeletonFilePath != nullptr && !skeletonFilePath->empty())
		{
			modelFileData.SkeletonAsset = BuildRelativeAssetPath(*skeletonFilePath, importedAssetDir);
		}
		else
		{
			modelFileData.SkeletonName = skinnedBundle.Skeleton.Name;
			modelFileData.Skeleton = skinnedBundle.Skeleton.Topology;
		}
	}
	else if (sceneContainsBones && m_consoleWindow != nullptr)
	{
		m_consoleWindow->AddWarningMessage(
			L"[Import][Skinned] 检测到骨骼，但写入 .wmodel 骨架引用/权重失败：%s",
			modelPath.filename().c_str());
	}

	for (UINT materialIndex = 0; materialIndex < materialFilePaths.size(); ++materialIndex)
	{
		WModelMaterialRef materialRef;
		materialRef.Id = modelMaterialIds[materialIndex];
		materialRef.File = BuildRelativeAssetPath(materialFilePaths[materialIndex], importedAssetDir);
		modelFileData.Materials.push_back(std::move(materialRef));
	}

	std::vector<std::wstring> meshIds(hierarchyScene->mNumMeshes);
	size_t skinnedVertexOffset = 0;
	for (UINT meshIndex = 0; meshIndex < hierarchyScene->mNumMeshes; ++meshIndex)
	{
		meshIds[meshIndex] = BuildModelMeshId(meshIndex);

		aiMesh* importedMesh = hierarchyScene->mMeshes[meshIndex];
		const size_t meshVertexCount = static_cast<size_t>(importedMesh != nullptr ? importedMesh->mNumVertices : 0u);
		Mesh meshData = ProcessRawMesh(hierarchyScene->mRootNode, importedMesh, hierarchyScene);
		if (meshData.vertices.empty() || meshData.indices32.empty())
		{
			if (hasSkinnedBundle)
				skinnedVertexOffset += meshVertexCount;
			continue;
		}

		WModelMeshData modelMeshData;
		modelMeshData.Id = meshIds[meshIndex];
		modelMeshData.Name = ResolveModelMeshName(importedMesh, meshIndex, L"Mesh");
		modelMeshData.Vertices = std::move(meshData.vertices);
		if (hasSkinnedBundle && importedMesh != nullptr && importedMesh->mNumBones > 0)
		{
			if (meshVertexCount > 0 &&
				skinnedVertexOffset + meshVertexCount <= skinnedBundle.SkinnedMesh.Vertices.size())
			{
				modelMeshData.Skinning.reserve(meshVertexCount);
				for (size_t vertexIndex = 0; vertexIndex < meshVertexCount; ++vertexIndex)
				{
					modelMeshData.Skinning.push_back(
						skinnedBundle.SkinnedMesh.Vertices[skinnedVertexOffset + vertexIndex].Skinning);
				}
			}
		}
		modelMeshData.Indices = std::move(meshData.indices32);
		modelFileData.Meshes.push_back(std::move(modelMeshData));

		if (hasSkinnedBundle)
			skinnedVertexOffset += meshVertexCount;
	}

	UINT modelNodeCounter = 0;
	modelFileData.RootNode = BuildModelHierarchyNode(
		hierarchyScene->mRootNode,
		hierarchyScene,
		meshIds,
		modelMaterialIds,
		modelNodeCounter,
		importBaseName,
		// 与 ExtractSkinnedModelBundle 使用完全相同的参考根，确保静态层级、
		// Bind Pose 和动画轨道处于同一个坐标空间。
		hasSkinnedBundle ? FindCollapsedImportRootNode(hierarchyScene) : nullptr);

	outModelFilePath = importedAssetDir / BuildImportedModelFileName(importBaseName);
	if (!WModelFile::SaveToFile(outModelFilePath, modelFileData))
	{
		if (m_consoleWindow != nullptr)
			m_consoleWindow->AddWarningMessage(L"[Import] .wmodel 文件保存失败：%s", outModelFilePath.wstring().c_str());
		outModelFilePath.clear();
		return false;
	}

	if (m_consoleWindow != nullptr)
	{
		m_consoleWindow->AddInfoMessage(L"[Import] 已生成模型文件：%s", outModelFilePath.filename().c_str());
	}

	return true;
}

bool AssimpLoader::ImportModelToScene(const std::wstring& path, WitchcraECS* ecs, const std::wstring& rootName, const Transform& transform, SceneEntityType sceneType)
{
	D3DWindow* dx = m_engine != nullptr ? m_engine->GetD3DWindow() : nullptr;
	if (dx == nullptr || ecs == nullptr || path.empty() || !std::filesystem::exists(path))
		return false;

	std::filesystem::path generatedSkeletonFilePath;
	std::vector<std::filesystem::path> generatedAnimationFilePaths;

	// 当前主线：若检测到骨骼模型，则在导入时额外导出骨架/动画资源。
	// 运行时挂载与播放不在此函数里做旧链式自动注入，而统一走 .wmodel 加载链路。
	{
		Assimp::Importer skeletonProbeImporter;
		const aiScene* probeScene = skeletonProbeImporter.ReadFile(
			SString::WstringToUTF8(path),
			aiProcess_Triangulate |
			aiProcess_ConvertToLeftHanded |
			aiProcess_JoinIdenticalVertices);
		if (SceneContainsBones(probeScene))
		{
			const std::wstring importBaseName =
				rootName.empty() ? std::filesystem::path(path).stem().wstring() : rootName;

			if (ImportSkinnedModelAssets(
				path,
				importBaseName,
				&generatedSkeletonFilePath,
				&generatedAnimationFilePaths,
				nullptr))
			{
				if (m_consoleWindow != nullptr)
				{
					m_consoleWindow->AddInfoMessage(
						L"[Import][Skinned] 检测到骨骼模型，已自动生成骨骼资源：%s",
						generatedSkeletonFilePath.empty() ? L"<none>" : generatedSkeletonFilePath.filename().wstring().c_str());
					if (!generatedAnimationFilePaths.empty())
					{
						m_consoleWindow->AddInfoMessage(
							L"[Import][Skinned] 动画资源数量：%u",
							static_cast<UINT>(generatedAnimationFilePaths.size()));
					}
				}
			}
			else if (m_consoleWindow != nullptr)
			{
				m_consoleWindow->AddWarningMessage(
					L"[Import][Skinned] 检测到骨骼模型，但自动导出 .wskeleton/.wanim 失败：%s",
					path.c_str());
			}
		}
	}

	// 与“打开场景文件”流程保持一致：在批量创建材质/网格前先等待 GPU 空闲，
	// 避免导入时写入描述符/几何资源与上一帧在飞命令发生资源访问冲突。
	dx->FlushCommandQueue();

	// 内部统一走 .wmodel 加载链路：
	// 外部模型先转换成 .wmodel，再用同一套逻辑创建实体/材质/网格。
	if (_wcsicmp(std::filesystem::path(path).extension().c_str(), WModelFile::Extension) == 0)
		return LoadWModelToSceneInternal(path, ecs, rootName, transform, sceneType, true);

	std::filesystem::path modelFilePath;
	if (!ConvertModelToWModel(path, rootName, &generatedSkeletonFilePath, modelFilePath))
		return false;

	if (m_consoleWindow != nullptr)
		m_consoleWindow->AddInfoMessage(L"[Import] 转换完成，将改用 .wmodel 流程加载。");

	return LoadWModelToSceneInternal(modelFilePath.wstring(), ecs, rootName, transform, sceneType, true);
}

bool AssimpLoader::LoadWModelToScene(const std::wstring& path, WitchcraECS* ecs, const std::wstring& rootName, const Transform& transform, SceneEntityType sceneType)
{
	return LoadWModelToSceneInternal(path, ecs, rootName, transform, sceneType, false);
}

bool AssimpLoader::LoadWModelToSceneInternal(
	const std::wstring& path,
	WitchcraECS* ecs,
	const std::wstring& rootName,
	const Transform& transform,
	SceneEntityType sceneType,
	bool stripEmptyHierarchyEntities)
{
	D3DWindow* dx = m_engine != nullptr ? m_engine->GetD3DWindow() : nullptr;
	if (dx == nullptr || ecs == nullptr || path.empty() || !std::filesystem::exists(path))
		return false;

	Witchcraft::WModelRuntime::WModelRuntimeAsset wmodelAsset;
	if (!Witchcraft::WModelRuntime::LoadWModelRuntimeAsset(std::filesystem::path(path), &wmodelAsset))
		return false;

	const std::filesystem::path modelPath = wmodelAsset.ModelPath;
	const std::filesystem::path modelDirectory = wmodelAsset.ModelDirectory;
	const WModelFileData& modelFileData = wmodelAsset.Data;
	const std::filesystem::path projectDirectory = std::filesystem::path(EngineUtils::GetProjectDirPath());
	const std::wstring importBaseName = SanitizeName(rootName.empty() ? (modelFileData.Name.empty() ? modelPath.stem().wstring() : modelFileData.Name) : rootName);
	std::wstring runtimeSkeletonAssetPath;
	if (!modelFileData.SkeletonAsset.empty())
	{
		const std::filesystem::path resolvedSkeletonAssetPath = wmodelAsset.SkeletonAssetPath;
		runtimeSkeletonAssetPath = BuildRelativeAssetPath(resolvedSkeletonAssetPath, projectDirectory);
		if (runtimeSkeletonAssetPath.empty())
			runtimeSkeletonAssetPath = resolvedSkeletonAssetPath.wstring();
	}
	const bool hasLegacyInlineSkeleton = !modelFileData.Skeleton.Bones.empty();
	bool hasSkinnedMeshEntities = false;

	if (m_consoleWindow != nullptr)
	{
		m_consoleWindow->AddInfoMessage(
			L"[WModel] 开始读取模型：%s",
			modelPath.filename().c_str());
		m_consoleWindow->AddInfoMessage(
			L"[WModel] 网格数量：%u，材质引用数量：%u",
			static_cast<UINT>(modelFileData.Meshes.size()),
			static_cast<UINT>(modelFileData.Materials.size()));
	}

	std::unordered_map<std::wstring, std::wstring> MaterialNames;
	for (const WModelMaterialRef& materialRef : modelFileData.Materials)
	{
		// 先读取 .wmat，再转换成运行时材质对象，并记录 “材质引用ID -> 运行时材质名” 映射。
		const std::filesystem::path materialPath = Witchcraft::WModelRuntime::ResolveWModelReferencedPath(modelDirectory, materialRef.File);
		WMaterialFileData materialFileData;
		if (!WMaterialFile::LoadFromFile(materialPath, &materialFileData))
		{
			if (m_consoleWindow != nullptr)
			{
				m_consoleWindow->AddWarningMessage(
					L"[WModel] 无法读取材质：%s",
					materialPath.wstring().c_str());
			}
			continue;
		}

		const ImportedMaterialInfo importedMaterialInfo = ConvertMaterialFileDataToImportedInfo(materialFileData, materialPath);
		const std::wstring MaterialBaseName = importBaseName + L"_" + importedMaterialInfo.Name;
		MaterialNames[materialRef.Id] = dx->CreateMaterialFromImport(MaterialBaseName, importedMaterialInfo);
	}

	std::unordered_map<std::wstring, std::uint32_t> meshDescendantCountByNodeId;
	std::function<std::uint32_t(const WModelNodeData&)> countMeshDescendants =
		[&](const WModelNodeData& nodeData) -> std::uint32_t
		{
			auto cacheIt = meshDescendantCountByNodeId.find(nodeData.Id);
			if (cacheIt != meshDescendantCountByNodeId.end())
				return cacheIt->second;

			std::uint32_t meshDescendantCount = 0;
			if (!nodeData.MeshRef.empty())
			{
				auto meshIt = wmodelAsset.MeshesById.find(nodeData.MeshRef);
				if (meshIt != wmodelAsset.MeshesById.end() &&
					nodeData.Type == WModelNodeType::Mesh &&
					meshIt->second != nullptr)
				{
					meshDescendantCount = 1;
				}
			}

			for (const WModelNodeData& childNode : nodeData.Children)
				meshDescendantCount += countMeshDescendants(childNode);

			meshDescendantCountByNodeId.emplace(nodeData.Id, meshDescendantCount);
			return meshDescendantCount;
		};
	(void)countMeshDescendants(modelFileData.RootNode);

	// 构建骨骼名称到索引的查找表，供 createNodeRecursive 中为子网格实体添加 BoneAttachmentComponent。
	std::unordered_map<std::wstring, std::int32_t> attachedBoneNameToIndex;
	for (std::int32_t boneIndex = 0; boneIndex < static_cast<std::int32_t>(modelFileData.Skeleton.Bones.size()); ++boneIndex)
	{
		const std::wstring& boneName = modelFileData.Skeleton.Bones[boneIndex].Name;
		if (!boneName.empty())
			attachedBoneNameToIndex[boneName] = boneIndex;
	}

	// 递归恢复 wmodel 中保存的层级结构。
	// stripEmptyHierarchyEntities=true 时会跳过非根 Empty 节点，并把它们的局部变换折叠到子节点。
	std::function<SceneEntityBase* (const WModelNodeData&, SceneEntityBase*, bool, const Transform&)> createNodeRecursive =
		[&](const WModelNodeData& nodeData, SceneEntityBase* parentEntity, bool isRootNode, const Transform& inheritedTransform) -> SceneEntityBase*
		{
			const Transform localTransform = CombineTransforms(inheritedTransform, nodeData.LocalTransform);

			const WModelMeshData* meshData = nullptr;
			if (!nodeData.MeshRef.empty())
			{
				auto meshIt = wmodelAsset.MeshesById.find(nodeData.MeshRef);
				if (meshIt != wmodelAsset.MeshesById.end())
					meshData = meshIt->second;
			}

			const bool isMeshNode = nodeData.Type == WModelNodeType::Mesh && meshData != nullptr;
			std::uint32_t meshDescendantCount = 0;
			auto meshDescendantCountIt = meshDescendantCountByNodeId.find(nodeData.Id);
			if (meshDescendantCountIt != meshDescendantCountByNodeId.end())
				meshDescendantCount = meshDescendantCountIt->second;
			if (stripEmptyHierarchyEntities && !isRootNode && meshDescendantCount == 0)
				return nullptr;

			Witchcraft::WModelRuntime::WModelMeshPayload meshPayload;
			const bool hasMeshPayload = Witchcraft::WModelRuntime::BuildWModelMeshPayload(
				wmodelAsset,
				nodeData,
				stripEmptyHierarchyEntities && !isMeshNode && nodeData.Type == WModelNodeType::Empty,
				nullptr,
				&meshPayload);
			const bool hasAbsorbedMeshChildren = !meshPayload.AbsorbedMeshChildren.empty();
			const bool keepAsEntity =
				!stripEmptyHierarchyEntities ||
				isRootNode ||
				isMeshNode ||
				hasAbsorbedMeshChildren ||
				meshDescendantCount > 1;

			SceneEntityBase* entity = nullptr;
			SceneEntityBase* firstCreatedEntity = nullptr;
			if (keepAsEntity)
			{
				const std::wstring desiredName = isRootNode
					? importBaseName
					: SanitizeName(nodeData.Name.empty() ? L"Node" : nodeData.Name);
				const std::wstring entityName = ecs->GetUniqueEntityName(desiredName, parentEntity);
				const bool entityHasMesh = hasMeshPayload;

				entity = entityHasMesh
					? ecs->CreateMeshEntity(entityName, parentEntity)
					: ecs->CreateBasicEntity(entityName, parentEntity, ComponentType::Co_Unk);
				if (entity == nullptr)
					return nullptr;
				firstCreatedEntity = entity;
				// MeshName 同时是 D3DWindow::AllRitems 的全局键。nodeId 只在单个
				// .wmodel 内唯一；同一资源多实例时必须附加 ECS 实体 ID，避免后一实例
				// 覆盖前一实例的世界矩阵、SourceEntity 与 skinning constant buffer。
				const std::wstring meshName = importBaseName + L"_" + nodeData.Id +
					L"_entity_" + std::to_wstring(static_cast<unsigned long long>(entity->entity));
				ecs->SetEntitySceneType(entity, sceneType, false);

				ecs->SetEntityEditableLocalTransform(entity, localTransform);

				if (entityHasMesh)
				{
					if (ecs->ConfigureMeshEntity(
						entity,
						m_engine,
						entityName,
						path,
						meshName,
						不透明物体渲染项目,
						L""))
					{
						if (!meshPayload.Vertices.empty() && !meshPayload.Indices.empty())
						{
							ecs->AppendMeshEntityVertices(entity, meshPayload.Vertices);
							ecs->AppendMeshEntityIndices(entity, meshPayload.Indices);
							if (meshPayload.ContainsSkinning && !meshPayload.HasCompleteSkinning)
							{
								if (m_consoleWindow != nullptr)
								{
									m_consoleWindow->AddErrorMessage(
										L"[WModel][Skinning] 蒙皮顶点流不完整，已拒绝加载网格：entity=%s vertices=%u skinning=%u",
										entityName.c_str(),
										static_cast<UINT>(meshPayload.Vertices.size()),
										static_cast<UINT>(meshPayload.Skinning.size()));
								}
								return nullptr;
							}
							const bool canUseInlineSkinning =
								!runtimeSkeletonAssetPath.empty() &&
								meshPayload.HasCompleteSkinning;

							bool uploadedSkinnedGeometry = false;
							if (canUseInlineSkinning)
							{
								const std::wstring skinnedGeometryName = meshName + L"_SkinnedGeo";
								uploadedSkinnedGeometry = UploadAndBindInlineSkinnedMeshGeometry(
									ecs,
									entity,
									skinnedGeometryName,
									meshPayload.Vertices,
									meshPayload.Skinning,
									meshPayload.Indices,
									runtimeSkeletonAssetPath,
									true);
								if (!uploadedSkinnedGeometry)
								{
									if (m_consoleWindow != nullptr)
										m_consoleWindow->AddErrorMessage(L"[WModel][Skinning] 无法创建完整的蒙皮几何：entity=%s", entityName.c_str());
									return nullptr;
								}
								else
								{
									hasSkinnedMeshEntities = true;
								}
							}

							if (!uploadedSkinnedGeometry)
							{
								// 顺序很重要：
								// 1. 创建 GPU 网格
								// 2. 绑定材质
								// 3. 最后统一由 ECS -> D3DWindow 汇总渲染项
								ecs->SetupMeshEntity(entity, dx);

								// 缺少完整权重流的网格保持静态；不能仅因场景存在骨架而标记为 IsSkinned。
							}
						}

						if (!meshPayload.PrimaryMaterialRef.empty())
						{
							// 当前节点使用首个可用材质槽作为默认材质。
							const auto materialIt = MaterialNames.find(meshPayload.PrimaryMaterialRef);
							if (materialIt != MaterialNames.end() && !materialIt->second.empty())
								ecs->SetMeshEntityMaterial(entity, materialIt->second);
						}

						ecs->SetEntitySceneType(entity, sceneType);
					}
				}

			}

			SceneEntityBase* nextParent = keepAsEntity ? entity : parentEntity;
			const Transform nextInherited = keepAsEntity ? MakeIdentityTransform() : localTransform;
			if (hasAbsorbedMeshChildren)
			{
				for (const WModelNodeData* absorbedChildNode : meshPayload.AbsorbedMeshChildren)
				{
					for (const WModelNodeData& absorbedGrandChildNode : absorbedChildNode->Children)
					{
						SceneEntityBase* childRoot = createNodeRecursive(
							absorbedGrandChildNode,
							nextParent,
							false,
							absorbedChildNode->LocalTransform);
						if (firstCreatedEntity == nullptr && childRoot != nullptr)
							firstCreatedEntity = childRoot;
					}
				}
			}

			for (const WModelNodeData& childNode : nodeData.Children)
			{
				if (hasAbsorbedMeshChildren &&
					std::find(meshPayload.AbsorbedMeshChildren.begin(), meshPayload.AbsorbedMeshChildren.end(), &childNode) != meshPayload.AbsorbedMeshChildren.end())
					continue;
				SceneEntityBase* childRoot = createNodeRecursive(childNode, nextParent, false, nextInherited);
				if (firstCreatedEntity == nullptr && childRoot != nullptr)
					firstCreatedEntity = childRoot;
			}

			return firstCreatedEntity;
		};

	WModelNodeData rootNodeData = Witchcraft::WModelRuntime::CollapseLeadingWModelWrapperNodes(modelFileData.RootNode);
	SceneEntityBase* importedRoot = createNodeRecursive(rootNodeData, nullptr, true, transform);

	if (importedRoot != nullptr)
	{
		if (!runtimeSkeletonAssetPath.empty() || hasLegacyInlineSkeleton)
		{
			// 只要有骨骼拓扑，就添加骨骼动画所需组件，与是否有蒙皮网格解耦。
			// SkinningRuntimeComponent 不仅是蒙皮网格的容器，也是 EvaluateAnimatedEntity
			// 接收 GlobalPose 计算结果的必需参数——即使无蒙皮网格，播放骨骼动画也需要它。
			(void)ecs->EnsureEntitySkeletonRuntime(
				importedRoot,
				runtimeSkeletonAssetPath,
				hasLegacyInlineSkeleton ? &modelFileData.Skeleton : nullptr,
				true,
				true,
				true,
				true);
		}
		if (m_consoleWindow != nullptr)
		{
		}
		ecs->SelectEntityForHierarchy(importedRoot);
		if (importedRoot != nullptr)
		{
			// 导入完成后改为走与 OpenScene 相同的全量重建路径，
			// 降低增量挂载路径在复杂层级/材质切层下产生的渲染缓存不一致风险。
			dx->FlushCommandQueue();
			dx->RebuildRenderItemsFromEntities(ecs);
		}

		if (m_consoleWindow != nullptr)
		{
			m_consoleWindow->AddInfoMessage(
				L"[WModel] 导入完成：根实体=%s",
				importedRoot != nullptr ? ecs->GetEntityName(importedRoot).c_str() : L"<null>");
		}
		return importedRoot != nullptr;
	}

	if (m_consoleWindow != nullptr)
		m_consoleWindow->AddErrorMessage(L"[WModel] 导入失败：未能从模型层级创建根实体：%s", modelPath.filename().c_str());
	return false;
}
