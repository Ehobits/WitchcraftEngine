#include "SkinningRuntimeComponent.h"

void SkinningRuntimeComponent::SetPalette(const Witchcraft::Animation::SkinningPalette& palette)
{
	mPalette = palette;
}

Witchcraft::Animation::SkinningPalette& SkinningRuntimeComponent::GetPalette()
{
	return mPalette;
}

const Witchcraft::Animation::SkinningPalette& SkinningRuntimeComponent::GetPalette() const
{
	return mPalette;
}

void SkinningRuntimeComponent::SetPaletteDirty(bool dirty)
{
	mPaletteDirty = dirty;
}

bool SkinningRuntimeComponent::IsPaletteDirty() const
{
	return mPaletteDirty;
}
