#include "FullscreenQuad.hlsli"

Texture2D g_NormalMap : register(t0);
Texture2D g_DepthMap : register(t1);
Texture2D g_InputShadowMask : register(t2);

SamplerState g_SamPointClamp : register(s0);
SamplerState g_SamLinearClamp : register(s1);

cbuffer cbShadowMaskBlur : register(b0)
{
	uint g_HorizontalBlur;
};

cbuffer cbPass : register(b1)
{
	float4x4 g_View;
	float4x4 g_InvView;
	float4x4 g_Proj;
	float4x4 g_InvProj;
	float4x4 g_ViewProj;
	float4x4 g_InvViewProj;
	float4x4 g_ViewProjTex;
	float3 g_CameraPosW;
	float __g_pass_PAD000;
	float2 g_RenderTargetSize;
	float2 __g_pass_PAD_RenderTargetSize;
	float4x4 g_ShadowTransform[256];
	float2 g_ShadowSettings;
	float2 g_AOSettings;
	float4 g_ShadowMaskSettings;
	float4 g_DirectionalShadowCascadeSplits;
	float4 g_DirectionalShadowCascadeSettings;
	float4 g_DirectionalShadowCascadeWorldTexelSize;
	float4 g_DirectionalShadowCascadeDepthScale;
	uint g_LightConst;
	float3 __g_pass_PAD001;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
};

VertexOut VS(uint vertexId : SV_VertexID)
{
	VertexOut vout;
	vout.TexC = BuildFullscreenQuadTexCoord(vertexId);
	vout.PosH = BuildFullscreenQuadPositionH(vout.TexC);
	return vout;
}

uint GetDirectionalShadowCascadeCount()
{
	return (uint) clamp(g_DirectionalShadowCascadeSettings.x, 1.0f, 4.0f);
}

float GetDirectionalShadowCascadeSplit(uint index)
{
	if (index == 0u)
		return g_DirectionalShadowCascadeSplits.x;
	if (index == 1u)
		return g_DirectionalShadowCascadeSplits.y;
	if (index == 2u)
		return g_DirectionalShadowCascadeSplits.z;
	return g_DirectionalShadowCascadeSplits.w;
}

float ReconstructViewZ(float2 uv, float depth)
{
	const float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
	const float4 viewPos = mul(float4(ndc, depth, 1.0f), g_InvProj);
	return viewPos.z / max(viewPos.w, 1e-5f);
}

uint SelectDirectionalShadowCascade(float viewZ)
{
	const uint cascadeCount = GetDirectionalShadowCascadeCount();
	uint cascadeIndex = 0u;

	[unroll]
	for (uint splitIndex = 0u; splitIndex < 3u; ++splitIndex)
	{
		if (splitIndex + 1u >= cascadeCount)
			break;
		if (viewZ > GetDirectionalShadowCascadeSplit(splitIndex))
			cascadeIndex = splitIndex + 1u;
	}

	return cascadeIndex;
}

float ComputeBaseBlurRadiusPixels(uint cascadeIndex)
{
	if (cascadeIndex == 0u)
		return g_ShadowMaskSettings.y;
	if (cascadeIndex == 1u)
		return g_ShadowMaskSettings.z;
	return g_ShadowMaskSettings.w;
}

float ComputeBlurRadiusPixels(uint cascadeIndex, float viewZ)
{
	const uint cascadeCount = GetDirectionalShadowCascadeCount();
	float radiusPixels = ComputeBaseBlurRadiusPixels(cascadeIndex);
	const float blendRatio = saturate(g_DirectionalShadowCascadeSettings.y) * 0.42f;
	if (blendRatio <= 0.0f || cascadeCount <= 1u)
		return radiusPixels;

	if (cascadeIndex > 0u)
	{
		const float previousSplitDepth = GetDirectionalShadowCascadeSplit(cascadeIndex - 1u);
		const float previousPreviousSplitDepth =
			(cascadeIndex > 1u) ? GetDirectionalShadowCascadeSplit(cascadeIndex - 2u) : 0.0f;
		const float previousSplitRange = max(previousSplitDepth - previousPreviousSplitDepth, 0.001f);
		const float previousBlendBand = max(previousSplitRange * blendRatio, 1.0f);
		const float previousBlendStart = previousSplitDepth - previousBlendBand;
		const float previousBlendEnd = previousSplitDepth + previousBlendBand;
		if (viewZ < previousBlendEnd)
		{
			const float previousRadius = ComputeBaseBlurRadiusPixels(cascadeIndex - 1u);
			const float blendWeight = smoothstep(previousBlendStart, previousBlendEnd, viewZ);
			radiusPixels = lerp(previousRadius, radiusPixels, blendWeight);
		}
	}

	if (cascadeIndex + 1u < cascadeCount)
	{
		const float splitDepth = GetDirectionalShadowCascadeSplit(cascadeIndex);
		const float previousSplitDepth =
			(cascadeIndex > 0u) ? GetDirectionalShadowCascadeSplit(cascadeIndex - 1u) : 0.0f;
		const float splitRange = max(splitDepth - previousSplitDepth, 0.001f);
		const float blendBand = max(splitRange * blendRatio, 1.0f);
		const float blendStart = splitDepth - blendBand;
		const float blendEnd = splitDepth + blendBand;
		if (viewZ > blendStart)
		{
			const float nextRadius = ComputeBaseBlurRadiusPixels(cascadeIndex + 1u);
			const float blendWeight = smoothstep(blendStart, blendEnd, viewZ);
			radiusPixels = lerp(radiusPixels, nextRadius, blendWeight);
		}
	}

	return radiusPixels;
}

float4 PS(VertexOut pin) : SV_Target
{
	const float centerDepth = g_DepthMap.SampleLevel(g_SamPointClamp, pin.TexC, 0.0f).r;
	const float centerMask = g_InputShadowMask.SampleLevel(g_SamLinearClamp, pin.TexC, 0.0f).r;
	if (centerDepth >= 1.0f)
		return centerMask;

	const float centerViewZ = ReconstructViewZ(pin.TexC, centerDepth);
	const uint cascadeIndex = SelectDirectionalShadowCascade(centerViewZ);
	const float radiusPixels = ComputeBlurRadiusPixels(cascadeIndex, centerViewZ);
	if (radiusPixels <= 0.25f)
		return centerMask;

	const float2 invSize = 1.0f / max(g_RenderTargetSize, float2(1.0f, 1.0f));
	const float2 axis = (g_HorizontalBlur != 0u) ? float2(invSize.x, 0.0f) : float2(0.0f, invSize.y);
	const float3 centerNormal = normalize(g_NormalMap.SampleLevel(g_SamPointClamp, pin.TexC, 0.0f).xyz);
	const float depthScale = 120.0f / max(centerViewZ, 1.0f);
	const float normalPower = 12.0f;

	float weightedMask = centerMask;
	float totalWeight = 1.0f;

	[unroll]
	for (int offset = -3; offset <= 3; ++offset)
	{
		if (offset == 0 || abs((float)offset) > radiusPixels)
			continue;

		const float x = (float)offset;
		const float gaussianWeight = exp(-x * x / max(2.0f * radiusPixels * radiusPixels, 1e-4f));
		const float2 sampleUv = saturate(pin.TexC + axis * x);
		const float sampleDepth = g_DepthMap.SampleLevel(g_SamPointClamp, sampleUv, 0.0f).r;
		if (sampleDepth >= 1.0f)
			continue;

		const float sampleViewZ = ReconstructViewZ(sampleUv, sampleDepth);
		const float depthWeight = exp(-abs(sampleViewZ - centerViewZ) * depthScale);
		const float3 sampleNormal = normalize(g_NormalMap.SampleLevel(g_SamPointClamp, sampleUv, 0.0f).xyz);
		const float normalWeight = pow(saturate(dot(sampleNormal, centerNormal)), normalPower);
		const float weight = gaussianWeight * depthWeight * normalWeight;

		weightedMask += g_InputShadowMask.SampleLevel(g_SamLinearClamp, sampleUv, 0.0f).r * weight;
		totalWeight += weight;
	}

	return saturate(weightedMask / max(totalWeight, 1e-5f));
}
