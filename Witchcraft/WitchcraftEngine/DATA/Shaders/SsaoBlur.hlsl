#include "SsaoCommon.hlsli"

Texture2D gInputMap : register(t2);

static const int gBlurRadius = 5;

cbuffer cbBlurDirection : register(b1)
{
	uint gHorizontalBlur;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
};

VertexOut VS(uint vid : SV_VertexID)
{
	VertexOut vout;

	vout.TexC = BuildFullscreenQuadTexCoord(vid);
	vout.PosH = BuildFullscreenQuadPositionH(vout.TexC);

	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	float blurWeights[12] =
	{
		gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
		gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
		gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
	};

	const float2 invRenderTargetSize = ComputeSsaoInvRenderTargetSize();
	float2 texOffset = gHorizontalBlur != 0u ?
		float2(invRenderTargetSize.x, 0.0f) :
		float2(0.0f, invRenderTargetSize.y);

	float4 color = blurWeights[gBlurRadius] * gInputMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f);
	float totalWeight = blurWeights[gBlurRadius];

	float3 centerNormal = gNormalMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz;
	centerNormal = normalize(centerNormal);
	float centerDepth = NdcDepthToViewDepth(gDepthMap.SampleLevel(gsamDepthMap, pin.TexC, 0.0f).r);

	[unroll]
	for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
	{
		if (i == 0)
			continue;

		float2 tex = pin.TexC + i * texOffset;
		if (tex.x < 0.0f || tex.x > 1.0f || tex.y < 0.0f || tex.y > 1.0f)
			continue;

		float3 neighborNormal = gNormalMap.SampleLevel(gsamPointClamp, tex, 0.0f).xyz;
		neighborNormal = normalize(neighborNormal);
		float neighborDepth = NdcDepthToViewDepth(gDepthMap.SampleLevel(gsamDepthMap, tex, 0.0f).r);

		// 适度放宽双边模糊阈值，避免原始 SSAO 的随机高频残留成全局颗粒噪点。
		// 仍保留法线/深度边界保护，尽量不把接触阴影与物体轮廓糊穿。
		const float normalThreshold = 0.89f;
		const float depthThreshold = max(0.04f, abs(centerDepth) * 0.020f);
		
		if (dot(neighborNormal, centerNormal) >= normalThreshold &&
			abs(neighborDepth - centerDepth) <= depthThreshold)
		{
			float weight = blurWeights[i + gBlurRadius];
			color += weight * gInputMap.SampleLevel(gsamPointClamp, tex, 0.0f);
			totalWeight += weight;
		}
	}

	return color / max(totalWeight, 1e-4f);
}
