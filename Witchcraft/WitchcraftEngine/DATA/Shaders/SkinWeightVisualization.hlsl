#include "Core.hlsl"

#define SKINNED_MESH 1
#include "SkinningHelpers.hlsli"

static const float G_WEIGHT_VISUALIZATION_ALPHA = 0.55f;
static const float G_GOLDEN_RATIO = 0.61803398875f;
static const float G_HUE_BASE = 0.13f;  // 避免骨骼 0 正好是红色

struct SkinWeightVertexIn
{
	float3 PositionL : POSITION;
	float4 Color : COLOR;
	float3 NormalL : NORMAL;
	float2 TexCoord : TEXCOORD;
	float3 TangentL : TANGENT;
	float3 BitangentL : BINORMAL;
	uint4 BoneIndices : BLENDINDICES;
	float4 BoneWeights : BLENDWEIGHT;
};

struct SkinWeightVertexOut
{
	float4 PositionH : SV_POSITION;
	float4 Color : COLOR;
};

// HSL → RGB
float3 HUEToRGB(float h)
{
	h = frac(h);
	float r = abs(h * 6.0f - 3.0f) - 1.0f;
	float g = 2.0f - abs(h * 6.0f - 2.0f);
	float b = 2.0f - abs(h * 6.0f - 4.0f);
	return saturate(float3(r, g, b));
}

float3 GetBoneColor(uint boneIndex, float saturation, float lightness)
{
	float hue = frac((float)boneIndex * G_GOLDEN_RATIO + G_HUE_BASE);
	float3 rgb = HUEToRGB(hue);
	// 向灰白方向混合以控制饱和度与亮度
	float3 gray = float3(lightness, lightness, lightness);
	return lerp(gray, rgb, saturation);
}

float3 BlendBoneColors(uint3 boneIndices, float3 boneWeights)
{
	float3 w = saturate(boneWeights);
	float wSum = w.x + w.y + w.z;
	// 零权重或无蒙皮数据 → 白色
	if (wSum < 1e-5f) return float3(0.92f, 0.92f, 0.92f);
	w /= wSum;

	float3 color = float3(0.0f, 0.0f, 0.0f);
	color += w.x * GetBoneColor(boneIndices.x, 0.85f, 0.55f);
	color += w.y * GetBoneColor(boneIndices.y, 0.85f, 0.55f);
	color += w.z * GetBoneColor(boneIndices.z, 0.85f, 0.55f);
	return saturate(color);
}

SkinWeightVertexOut VS(SkinWeightVertexIn input)
{
	SkinWeightVertexOut output;
	const float4x4 skinningMatrix = BuildSkinningMatrixFromWeights(input.BoneIndices, input.BoneWeights);
	const float3 skinnedPositionL = SkinPositionL(input.PositionL, skinningMatrix);
	const float4 positionW = mul(float4(skinnedPositionL, 1.0f), g_WorldTransform);
	output.PositionH = mul(positionW, g_ViewProj);
	output.Color = float4(BlendBoneColors(input.BoneIndices, input.BoneWeights), G_WEIGHT_VISUALIZATION_ALPHA);
	return output;
}

float4 PS(SkinWeightVertexOut input) : SV_TARGET
{
	return input.Color;
}
