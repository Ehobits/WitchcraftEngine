#pragma once

#include <xstring>
#include <vector>

#include <DirectXMath.h>

#include "BaseComponent.h"
#include "Common/SkeletonSharedTypes.h"

class SkeletonComponent : public BaseComponent
{
public:
	void SetSkeletonAssetPath(const std::wstring& assetPath);
	const std::wstring& GetSkeletonAssetPath() const;

	void SetTopology(const Witchcraft::Animation::SkeletonTopology& topology);
	const Witchcraft::Animation::SkeletonTopology& GetTopology() const;

	void SetBoneNames(const std::vector<std::wstring>& boneNames);
	const std::vector<std::wstring>& GetBoneNames() const;

	void SetLocalPose(const std::vector<Witchcraft::Animation::BoneLocalPose>& localPose);
	std::vector<Witchcraft::Animation::BoneLocalPose>& GetLocalPose();
	const std::vector<Witchcraft::Animation::BoneLocalPose>& GetLocalPose() const;
	void SetLocalMatrixPose(const std::vector<DirectX::XMFLOAT4X4>& localMatrixPose);
	std::vector<DirectX::XMFLOAT4X4>& GetLocalMatrixPose();
	const std::vector<DirectX::XMFLOAT4X4>& GetLocalMatrixPose() const;

	void SetGlobalPose(const std::vector<DirectX::XMFLOAT4X4>& globalPose);
	std::vector<DirectX::XMFLOAT4X4>& GetGlobalPose();
	const std::vector<DirectX::XMFLOAT4X4>& GetGlobalPose() const;

	void SetDirty(bool dirty);
	bool IsDirty() const;

	virtual ComponentType GetComponentType() override { return mComponentType; }

private:
	bool LoadSkeletonTopologyFromAsset(
		const std::wstring& assetPath,
		Witchcraft::Animation::SkeletonTopology* outTopology);

	std::wstring mSkeletonAssetPath;
	Witchcraft::Animation::SkeletonTopology mTopology;
	std::vector<std::wstring> mBoneNames;
	std::vector<Witchcraft::Animation::BoneLocalPose> mLocalPose;
	std::vector<DirectX::XMFLOAT4X4> mLocalMatrixPose;
	std::vector<DirectX::XMFLOAT4X4> mGlobalPose;
	bool mDirty = true;
	ComponentType mComponentType = ComponentType::Co_Skeleton;
};
