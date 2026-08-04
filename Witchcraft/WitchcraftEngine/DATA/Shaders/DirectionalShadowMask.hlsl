#include "FullscreenQuad.hlsli"
#include "LightingUtil.hlsl"

// 该 pass 复用主 PBR 的 CSM 数学约定，但不能直接 include PBRXShadowHelpers：
// 这里的资源/寄存器绑定和屏幕空间重建路径不同。
// 修改级联 bias、filter texel size、seam blend 时，需要同步检查 PBRXShadowHelpers.hlsl。
static const uint G_MAX_DIRECTIONAL_SHADOW_MAP_COUNT = 32u;

Texture2D g_NormalMap : register(t0);
Texture2D g_DepthMap : register(t1);
Texture2D g_InputShadowMask : register(t2);
Texture2D g_DirectionalShadowMap[G_MAX_DIRECTIONAL_SHADOW_MAP_COUNT] : register(t13);

SamplerState g_SamPointClamp : register(s0);
SamplerState g_SamLinearClamp : register(s1);
SamplerComparisonState g_SamShadow : register(s2);

// Shadow map 投影范围外按“全亮”处理，保持和主 PBR 阴影采样一致。
// 这样 screen-space mask 不会把 cascade 覆盖外的区域误写成黑色阴影。
float SampleDirectionalShadowCmpLitOutside(uint shadowIndex, float2 shadowUv, float depth)
{
	if (shadowUv.x < 0.0f || shadowUv.x > 1.0f ||
		shadowUv.y < 0.0f || shadowUv.y > 1.0f ||
		depth < 0.0f || depth > 1.0f)
	{
		return 1.0f;
	}

	return g_DirectionalShadowMap[shadowIndex].SampleCmpLevelZero(g_SamShadow, shadowUv, depth).r;
}

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

cbuffer cbLightPass : register(b2)
{
	float4 g_AmbientColor;
	Light g_Lights[256];
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

float GetDirectionalCascadeNormalizedPosition(uint cascadeIndex)
{
	return saturate((float)cascadeIndex / max((float)GetDirectionalShadowCascadeCount() - 1.0f, 1.0f));
}

float GetDirectionalCascadeMiddleWeight(uint cascadeIndex)
{
	const float cascadePosition = GetDirectionalCascadeNormalizedPosition(cascadeIndex);
	return 1.0f - abs(cascadePosition * 2.0f - 1.0f);
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

float GetDirectionalShadowCascadeWorldTexelSize(uint index)
{
	if (index == 0u)
		return g_DirectionalShadowCascadeWorldTexelSize.x;
	if (index == 1u)
		return g_DirectionalShadowCascadeWorldTexelSize.y;
	if (index == 2u)
		return g_DirectionalShadowCascadeWorldTexelSize.z;
	return g_DirectionalShadowCascadeWorldTexelSize.w;
}

float GetDirectionalShadowCascadeDepthScale(uint index)
{
	if (index == 0u)
		return g_DirectionalShadowCascadeDepthScale.x;
	if (index == 1u)
		return g_DirectionalShadowCascadeDepthScale.y;
	if (index == 2u)
		return g_DirectionalShadowCascadeDepthScale.z;
	return g_DirectionalShadowCascadeDepthScale.w;
}

float ReconstructViewZ(float2 uv, float depth)
{
	const float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
	const float4 viewPos = mul(float4(ndc, depth, 1.0f), g_InvProj);
	return viewPos.z / max(viewPos.w, 1e-5f);
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
	const float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
	const float4 worldPos = mul(float4(ndc, depth, 1.0f), g_InvViewProj);
	return worldPos.xyz / max(worldPos.w, 1e-5f);
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

float ComputeDirectionalReceiverBias(float3 normalW, float3 lightDirW, uint cascadeIndex)
{
	const float baseBias = max(g_DirectionalShadowCascadeSettings.z, 0.0f);
	const float3 N = normalize(normalW);
	const float3 L = normalize(-lightDirW);
	const float ndotl = saturate(dot(N, L));
	const float slopeFactor = 1.0f - ndotl;
	const float referenceDepthScale = max(GetDirectionalShadowCascadeDepthScale(0u), 1e-6f);
	const float cascadeDepthScale = max(GetDirectionalShadowCascadeDepthScale(min(cascadeIndex, 3u)), 1e-6f);
	const float worldStableBiasScale = max(cascadeDepthScale / referenceDepthScale, 1.26f);
	const float referenceWorldTexelSize = max(GetDirectionalShadowCascadeWorldTexelSize(0u), 1e-5f);
	const float cascadeWorldTexelSize = max(GetDirectionalShadowCascadeWorldTexelSize(min(cascadeIndex, 3u)), referenceWorldTexelSize);
	const float texelRatio = max(cascadeWorldTexelSize / referenceWorldTexelSize, 1.0f);
	const float coarseCascadeBiasScale = lerp(1.0f, 1.58f, saturate(log2(texelRatio) * 0.24f));
	const float middleCascadeBlend = GetDirectionalCascadeMiddleWeight(cascadeIndex);
	const float middleCascadeBiasScale = lerp(1.0f, 1.16f, middleCascadeBlend);
	return baseBias * max(worldStableBiasScale, coarseCascadeBiasScale) * middleCascadeBiasScale * (1.14f + slopeFactor * 1.75f);
}

float ComputeDirectionalShadowFilterTexelSize(uint cascadeIndex, uint shadowIndex, float desiredWorldTexelSize)
{
	uint shadowWidth, shadowHeight, shadowNumMips;
	g_DirectionalShadowMap[shadowIndex].GetDimensions(0, shadowWidth, shadowHeight, shadowNumMips);

	const float cascadeWorldTexelSize = max(GetDirectionalShadowCascadeWorldTexelSize(min(cascadeIndex, 3u)), 1e-5f);
	const float farCascadeBlend = GetDirectionalCascadeNormalizedPosition(cascadeIndex);
	const float middleCascadeBlend = GetDirectionalCascadeMiddleWeight(cascadeIndex);
	const float minWorldRadiusScale = lerp(1.05f, 2.15f, farCascadeBlend) + middleCascadeBlend * 0.32f;
	const float clampedDesiredWorldTexelSize = max(desiredWorldTexelSize, cascadeWorldTexelSize * minWorldRadiusScale);
	const float worldRadiusScale = min(clampedDesiredWorldTexelSize / cascadeWorldTexelSize, 2.75f);
	return (1.0f / max((float)shadowWidth, 1.0f)) * worldRadiusScale;
}

float SampleDirectionalCascadeShadowWithWorldTexelSize(
	Light light,
	uint cascadeIndex,
	float4 posW,
	float3 normalW,
	float desiredWorldTexelSize)
{
	const uint shadowBaseIndex = (uint)max(light.ShadowTextureIndex, 0.0f);
	const uint shadowIndex = shadowBaseIndex + cascadeIndex;
	if (shadowIndex >= G_MAX_DIRECTIONAL_SHADOW_MAP_COUNT)
		return 1.0f;

	const uint shadowTransformIndex = (uint)max(light.ShadowTransformIndex, 0.0f) + cascadeIndex;
	float4 shadowPos = mul(posW, g_ShadowTransform[shadowTransformIndex]);
	if (shadowPos.w <= 0.0f)
		return 1.0f;
	shadowPos.xyz /= shadowPos.w;

	const float depthBias = ComputeDirectionalReceiverBias(normalW, light.Direction, cascadeIndex) * max(light.ShadowBiasScale, 0.0f);
	const float depth = shadowPos.z - max(depthBias, 0.0f);
	if (shadowPos.x < 0.0f || shadowPos.x > 1.0f ||
		shadowPos.y < 0.0f || shadowPos.y > 1.0f ||
		depth < 0.0f || depth > 1.0f)
	{
		return 1.0f;
	}

	const float filterTexelSize = ComputeDirectionalShadowFilterTexelSize(cascadeIndex, shadowIndex, desiredWorldTexelSize);
	const float centerLit = SampleDirectionalShadowCmpLitOutside(shadowIndex, shadowPos.xy, depth);
	float percentLit = centerLit * 0.28f;

	const float2 offsets[8] =
	{
		float2(+1.0f, 0.0f), float2(-1.0f, 0.0f),
		float2(0.0f, +1.0f), float2(0.0f, -1.0f),
		float2(+1.0f, +1.0f), float2(+1.0f, -1.0f),
		float2(-1.0f, +1.0f), float2(-1.0f, -1.0f)
	};
	const float weights[8] = { 0.12f, 0.12f, 0.12f, 0.12f, 0.06f, 0.06f, 0.06f, 0.06f };

	[unroll]
	for (uint sampleIndex = 0u; sampleIndex < 8u; ++sampleIndex)
		percentLit += SampleDirectionalShadowCmpLitOutside(
			shadowIndex,
			shadowPos.xy + offsets[sampleIndex] * filterTexelSize,
			depth) * weights[sampleIndex];

	const float shadowOpacity = saturate(g_ShadowSettings.x);
	return lerp(1.0f - shadowOpacity, 1.0f, percentLit);
}

float SampleDirectionalCascadeShadow(Light light, uint cascadeIndex, float4 posW, float3 normalW)
{
	const float referenceWorldTexelSize = max(GetDirectionalShadowCascadeWorldTexelSize(0u), 1e-5f);
	return SampleDirectionalCascadeShadowWithWorldTexelSize(
		light,
		cascadeIndex,
		posW,
		normalW,
		referenceWorldTexelSize);
}

float ComputeDirectionalLightShadowMaskFactor(Light light, float4 posW, float3 normalW, float viewZ)
{
	const uint cascadeCount = GetDirectionalShadowCascadeCount();
	const uint cascadeIndex = SelectDirectionalShadowCascade(viewZ);
	float shadowFactor = SampleDirectionalCascadeShadow(light, cascadeIndex, posW, normalW);

	const float blendRatio = saturate(g_DirectionalShadowCascadeSettings.y) * 0.42f;
	if (blendRatio <= 0.0f || cascadeCount <= 1u)
		return shadowFactor;

	const float currentWorldTexelSize = GetDirectionalShadowCascadeWorldTexelSize(cascadeIndex);
	const float protectStrength = 0.022f;

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
			const float previousWorldTexelSize = GetDirectionalShadowCascadeWorldTexelSize(cascadeIndex - 1u);
			const float previousEdgeWeight =
				1.0f - smoothstep(0.0f, previousBlendBand, abs(viewZ - previousSplitDepth));
			const float seamWorldTexelSize = lerp(
				currentWorldTexelSize,
				max(currentWorldTexelSize, previousWorldTexelSize),
				previousEdgeWeight * 0.45f);
			const float currentShadowFactor = SampleDirectionalCascadeShadowWithWorldTexelSize(
				light,
				cascadeIndex,
				posW,
				normalW,
				seamWorldTexelSize);
			const float previousShadowFactor = SampleDirectionalCascadeShadowWithWorldTexelSize(
				light,
				cascadeIndex - 1u,
				posW,
				normalW,
				seamWorldTexelSize);
			const float previousBlendWeight = smoothstep(previousBlendStart, previousBlendEnd, viewZ);
			shadowFactor = lerp(previousShadowFactor, currentShadowFactor, previousBlendWeight);
			shadowFactor = lerp(shadowFactor, max(shadowFactor, previousShadowFactor), previousEdgeWeight * protectStrength);
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
			const float nextWorldTexelSize = GetDirectionalShadowCascadeWorldTexelSize(cascadeIndex + 1u);
			const float nextEdgeWeight =
				1.0f - smoothstep(0.0f, blendBand, abs(viewZ - splitDepth));
			const float seamWorldTexelSize = lerp(
				currentWorldTexelSize,
				max(currentWorldTexelSize, nextWorldTexelSize),
				nextEdgeWeight * 0.45f);
			const float currentShadowFactor = SampleDirectionalCascadeShadowWithWorldTexelSize(
				light,
				cascadeIndex,
				posW,
				normalW,
				seamWorldTexelSize);
			const float nextShadowFactor = SampleDirectionalCascadeShadowWithWorldTexelSize(
				light,
				cascadeIndex + 1u,
				posW,
				normalW,
				seamWorldTexelSize);
			const float blendWeight = smoothstep(blendStart, blendEnd, viewZ);
			shadowFactor = lerp(currentShadowFactor, nextShadowFactor, blendWeight);
			shadowFactor = lerp(shadowFactor, max(shadowFactor, nextShadowFactor), nextEdgeWeight * protectStrength);
		}
	}

	return shadowFactor;
}

float4 PS(VertexOut pin) : SV_Target
{
	const float depth = g_DepthMap.SampleLevel(g_SamPointClamp, pin.TexC, 0.0f).r;
	if (depth >= 1.0f)
		return 1.0f;

	const float viewZ = ReconstructViewZ(pin.TexC, depth);
	const float3 posW = ReconstructWorldPosition(pin.TexC, depth);
	const float3 normalV = normalize(g_NormalMap.SampleLevel(g_SamPointClamp, pin.TexC, 0.0f).xyz);
	const float3 normalW = normalize(mul(normalV, (float3x3)g_InvView));

	const uint lightCount = min(g_LightConst, 256u);
	[loop]
	for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex)
	{
		const Light light = g_Lights[lightIndex];
		const bool isDirectionalShadow =
			(light.ShadowSamplingMode == SHADOW_MODE_DIRECTIONAL_CASCADE || light.Type == DIRCTON_LIT) &&
			light.ShadowTextureIndex >= 0.0f &&
			(uint)max(light.ShadowTextureIndex, 0.0f) == 0u;
		if (isDirectionalShadow)
			return ComputeDirectionalLightShadowMaskFactor(light, float4(posW, 1.0f), normalW, viewZ);
	}

	return 1.0f;
}
