#pragma once

#include "BaseComponent.h"
#include "Common/SkinningSharedTypes.h"

class SkinningRuntimeComponent : public BaseComponent
{
public:
	void SetPalette(const Witchcraft::Animation::SkinningPalette& palette);
	Witchcraft::Animation::SkinningPalette& GetPalette();
	const Witchcraft::Animation::SkinningPalette& GetPalette() const;

	void SetPaletteDirty(bool dirty);
	bool IsPaletteDirty() const;

	virtual ComponentType GetComponentType() override { return mComponentType; }

private:
	Witchcraft::Animation::SkinningPalette mPalette;
	bool mPaletteDirty = true;
	ComponentType mComponentType = ComponentType::Co_SkinningRuntime;
};
