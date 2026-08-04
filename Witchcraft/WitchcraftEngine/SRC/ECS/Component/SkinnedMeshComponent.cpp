#include "SkinnedMeshComponent.h"

void SkinnedMeshComponent::SetSkinnedMeshAssetPath(const std::wstring& assetPath)
{
	mSkinnedMeshAssetPath = assetPath;
}

const std::wstring& SkinnedMeshComponent::GetSkinnedMeshAssetPath() const
{
	return mSkinnedMeshAssetPath;
}

void SkinnedMeshComponent::SetSkeletonAssetPath(const std::wstring& assetPath)
{
	mSkeletonAssetPath = assetPath;
}

const std::wstring& SkinnedMeshComponent::GetSkeletonAssetPath() const
{
	return mSkeletonAssetPath;
}

void SkinnedMeshComponent::SetMaterialSlots(const std::vector<std::wstring>& materialSlots)
{
	mMaterialSlots = materialSlots;
}

const std::vector<std::wstring>& SkinnedMeshComponent::GetMaterialSlots() const
{
	return mMaterialSlots;
}
