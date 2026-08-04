#include "BoneAttachmentComponent.h"

void BoneAttachmentComponent::SetBoneName(const std::wstring& boneName)
{
	mBoneName = boneName;
}

const std::wstring& BoneAttachmentComponent::GetBoneName() const
{
	return mBoneName;
}

void BoneAttachmentComponent::SetBoneIndex(std::int32_t boneIndex)
{
	mBoneIndex = boneIndex;
}

std::int32_t BoneAttachmentComponent::GetBoneIndex() const
{
	return mBoneIndex;
}