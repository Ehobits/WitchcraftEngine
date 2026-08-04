#pragma once

#include <xstring>
#include <vector>

#include "BaseComponent.h"

class SkinnedMeshComponent : public BaseComponent
{
public:
	void SetSkinnedMeshAssetPath(const std::wstring& assetPath);
	const std::wstring& GetSkinnedMeshAssetPath() const;

	void SetSkeletonAssetPath(const std::wstring& assetPath);
	const std::wstring& GetSkeletonAssetPath() const;

	void SetMaterialSlots(const std::vector<std::wstring>& materialSlots);
	const std::vector<std::wstring>& GetMaterialSlots() const;

	virtual ComponentType GetComponentType() override { return mComponentType; }

private:
	std::wstring mSkinnedMeshAssetPath;
	std::wstring mSkeletonAssetPath;
	std::vector<std::wstring> mMaterialSlots;
	ComponentType mComponentType = ComponentType::Co_SkinnedMesh;
};
