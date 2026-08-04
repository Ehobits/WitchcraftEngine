#pragma once

#include "BaseComponent.h"

class BoneAttachmentComponent : public BaseComponent
{
public:
	void SetBoneName(const std::wstring& boneName);
	const std::wstring& GetBoneName() const;

	void SetBoneIndex(std::int32_t boneIndex);
	std::int32_t GetBoneIndex() const;

	virtual ComponentType GetComponentType() override { return mComponentType; }

private:
	std::wstring mBoneName;
	std::int32_t mBoneIndex = -1;
	ComponentType mComponentType = ComponentType::Co_BoneAttachment;
};