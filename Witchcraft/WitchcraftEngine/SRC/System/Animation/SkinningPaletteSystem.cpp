#include "System/Animation/SkinningPaletteSystem.h"

#include "ECS/Component/SkinningRuntimeComponent.h"
#include "Helpers/MathHelpers.h"

using namespace DirectX;

void SkinningPaletteSystem::UpdatePalette(
	const Witchcraft::Animation::SkeletonData* skeletonData,
	SkinningRuntimeComponent* runtimeComponent,
	const Witchcraft::Animation::SkeletonTopology& topology) const
{
	if (skeletonData == nullptr || runtimeComponent == nullptr)
		return;

	const auto& globalPose = skeletonData->GlobalPose;
	if (globalPose.size() != topology.Bones.size())
		return;

	Witchcraft::Animation::SkinningPalette palette = runtimeComponent->GetPalette();
	const std::vector<DirectX::XMFLOAT4X4> previousMatrices = palette.FinalBoneMatrices;
	palette.FinalBoneMatrices.resize(topology.Bones.size(), Witchcraft::Animation::MakeIdentityFloat4x4());

	for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(topology.Bones.size()); ++boneIndex)
	{
		const aiMatrix4x4 inverseBind = MathHelps::ConvertFloat4x4ToAiMatrix(topology.Bones[boneIndex].InverseBindPose);
		const aiMatrix4x4 currentGlobal = MathHelps::ConvertFloat4x4ToAiMatrix(globalPose[boneIndex]);
		const aiMatrix4x4 finalMatrix = currentGlobal * inverseBind;
		palette.FinalBoneMatrices[boneIndex] = MathHelps::ConvertAiMatrixToFloat4x4(finalMatrix);

		if (!MathHelps::IsFiniteFloat4x4(palette.FinalBoneMatrices[boneIndex]))
		{
			palette.FinalBoneMatrices[boneIndex] = Witchcraft::Animation::MakeIdentityFloat4x4();
		}
	}

	const bool paletteChanged = !ArePaletteMatricesEquivalent(previousMatrices, palette.FinalBoneMatrices);
	if (paletteChanged)
		++palette.Revision;
	runtimeComponent->SetPalette(palette);
	runtimeComponent->SetPaletteDirty(false);

}

bool SkinningPaletteSystem::ArePaletteMatricesEquivalent(const std::vector<DirectX::XMFLOAT4X4>& lhs, const std::vector<DirectX::XMFLOAT4X4>& rhs) const
{
	if (lhs.size() != rhs.size())
		return false;

	constexpr float kEpsilon = 0.00001f;
	for (size_t matrixIndex = 0; matrixIndex < lhs.size(); ++matrixIndex)
	{
		if (!MathHelps::AreFloat4x4NearlyEqual(lhs[matrixIndex], rhs[matrixIndex], kEpsilon))
			return false;
	}

	return true;
}
