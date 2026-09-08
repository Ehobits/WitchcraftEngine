#include "SkeletonComponent.h"

#include <filesystem>
#include <utility>

#include "Engine/EngineUtils.h"
#include "System/Animation/Assets/SkeletonAsset.h"
#include "System/WitchcraftFile/WSkeletonFile.h"

void SkeletonComponent::SetSkeletonAssetPath(const std::wstring& assetPath)
{
	const bool assetChanged = (mSkeletonAssetPath != assetPath);
	if (assetChanged)
		mTopology = {};

	mSkeletonAssetPath = assetPath;
	if (mSkeletonAssetPath.empty())
		return;
	if (!assetChanged && !mTopology.Bones.empty())
		return;

	Witchcraft::Animation::SkeletonTopology topology;
	if (LoadSkeletonTopologyFromAsset(mSkeletonAssetPath, &topology))
		mTopology = std::move(topology);
}

const std::wstring& SkeletonComponent::GetSkeletonAssetPath() const
{
	return mSkeletonAssetPath;
}

void SkeletonComponent::SetTopology(const Witchcraft::Animation::SkeletonTopology& topology)
{
	mTopology = topology;
}

const Witchcraft::Animation::SkeletonTopology& SkeletonComponent::GetTopology() const
{
	return mTopology;
}

void SkeletonComponent::SetBoneNames(const std::vector<std::wstring>& boneNames)
{
	mBoneNames = boneNames;
}

const std::vector<std::wstring>& SkeletonComponent::GetBoneNames() const
{
	return mBoneNames;
}

void SkeletonComponent::SetLocalPose(const std::vector<Witchcraft::Animation::BoneLocalPose>& localPose)
{
	mLocalPose = localPose;
}

std::vector<Witchcraft::Animation::BoneLocalPose>& SkeletonComponent::GetLocalPose()
{
	return mLocalPose;
}

const std::vector<Witchcraft::Animation::BoneLocalPose>& SkeletonComponent::GetLocalPose() const
{
	return mLocalPose;
}

void SkeletonComponent::SetLocalMatrixPose(const std::vector<DirectX::XMFLOAT4X4>& localMatrixPose)
{
	mLocalMatrixPose = localMatrixPose;
}

std::vector<DirectX::XMFLOAT4X4>& SkeletonComponent::GetLocalMatrixPose()
{
	return mLocalMatrixPose;
}

const std::vector<DirectX::XMFLOAT4X4>& SkeletonComponent::GetLocalMatrixPose() const
{
	return mLocalMatrixPose;
}

void SkeletonComponent::SetGlobalPose(const std::vector<DirectX::XMFLOAT4X4>& globalPose)
{
	mGlobalPose = globalPose;
}

std::vector<DirectX::XMFLOAT4X4>& SkeletonComponent::GetGlobalPose()
{
	return mGlobalPose;
}

const std::vector<DirectX::XMFLOAT4X4>& SkeletonComponent::GetGlobalPose() const
{
	return mGlobalPose;
}

void SkeletonComponent::SetDirty(bool dirty)
{
	mDirty = dirty;
}

bool SkeletonComponent::IsDirty() const
{
	return mDirty;
}

bool SkeletonComponent::LoadSkeletonTopologyFromAsset(const std::wstring& assetPath, Witchcraft::Animation::SkeletonTopology* outTopology)
{
	if (outTopology == nullptr || assetPath.empty())
		return false;

	WSkeletonFileData fileData;
	std::filesystem::path path(assetPath);
	if (!path.is_absolute())
		path = EngineUtils::ResolveProjectPath(path);
	if (!WSkeletonFile::LoadFromFile(path, &fileData))
		return false;

	Witchcraft::Animation::SkeletonAsset skeletonAsset;
	skeletonAsset.GetTopology() = fileData.Topology;
	skeletonAsset.RebuildCaches();
	*outTopology = skeletonAsset.GetTopology();
	return true;
}
