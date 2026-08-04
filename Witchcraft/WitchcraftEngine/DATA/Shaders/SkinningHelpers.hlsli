#pragma once

#if defined(SKINNED_MESH) && SKINNED_MESH

static const uint G_SKIN_BONE_MATRIX_COUNT = 256u;

uint4 ClampSkinBoneIndices(uint4 boneIndices)
{
	const uint maxBoneIndex = G_SKIN_BONE_MATRIX_COUNT - 1u;
	return min(boneIndices, uint4(maxBoneIndex, maxBoneIndex, maxBoneIndex, maxBoneIndex));
}

bool IsFiniteFloat3(float3 value)
{
	return all(value == value) && all(abs(value) < 1.0e20f);
}

float4x4 BuildSkinningMatrixFromWeights(uint4 boneIndices, float4 boneWeights)
{
	const uint4 safeBoneIndices = ClampSkinBoneIndices(boneIndices);
	float4x4 skinningMatrix = 0.0f;
	skinningMatrix += g_BoneMatrices[safeBoneIndices.x] * boneWeights.x;
	skinningMatrix += g_BoneMatrices[safeBoneIndices.y] * boneWeights.y;
	skinningMatrix += g_BoneMatrices[safeBoneIndices.z] * boneWeights.z;
	skinningMatrix += g_BoneMatrices[safeBoneIndices.w] * boneWeights.w;
	return skinningMatrix;
}

float3 SkinPositionL(float3 positionL, float4x4 skinningMatrix)
{
	const float3 skinnedPosition = mul(float4(positionL, 1.0f), skinningMatrix).xyz;
	return IsFiniteFloat3(skinnedPosition) ? skinnedPosition : positionL;
}

float3 SkinDirectionL(float3 directionL, float4x4 skinningMatrix)
{
	const float3 skinnedDirection = mul(directionL, (float3x3)skinningMatrix);
	return IsFiniteFloat3(skinnedDirection) ? skinnedDirection : directionL;
}

#endif
