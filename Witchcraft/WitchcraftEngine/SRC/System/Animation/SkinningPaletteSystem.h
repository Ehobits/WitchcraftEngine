#pragma once

#include "Common/SkeletonSharedTypes.h"

class SkinningRuntimeComponent;

class SkinningPaletteSystem
{
public:
	void UpdatePalette(
		const Witchcraft::Animation::SkeletonData* skeletonData,
		SkinningRuntimeComponent* runtimeComponent,
		const Witchcraft::Animation::SkeletonTopology& topology) const;

private:
	bool ArePaletteMatricesEquivalent(
		const std::vector<DirectX::XMFLOAT4X4>& lhs,
		const std::vector<DirectX::XMFLOAT4X4>& rhs) const;

};
