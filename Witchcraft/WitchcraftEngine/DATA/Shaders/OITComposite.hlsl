#include "PostProcessCommon.hlsli"

// 不透明底图（Opaque Base Color）：
// 来自前序不透明/天空等 pass 的场景颜色，作为透明合成时的背景输入。
Texture2D g_OpaqueBaseColor : register(t0);
Texture2D g_OitAccum : register(t1);
Texture2D g_OitReveal : register(t2);

PostProcessVertexOut VS(uint vertexId : SV_VertexID)
{
	return BuildPostProcessFullscreenQuadVertex(vertexId);
}

float4 SampleOpaqueBaseColor(float2 uv)
{
	// 读取不透明场景底图颜色，用于与 OIT 透明结果做最终合成。
	return g_OpaqueBaseColor.Sample(g_SamLinearClamp, uv);
}

float4 SampleOitAccum(float2 uv)
{
	return g_OitAccum.Sample(g_SamLinearClamp, uv);
}

float SampleOitReveal(float2 uv)
{
	return g_OitReveal.Sample(g_SamLinearClamp, uv).r;
}

float4 PS(PostProcessVertexOut pin) : SV_Target
{
	float3 opaqueColor = SampleOpaqueBaseColor(pin.TexC).rgb;
	float4 accum = SampleOitAccum(pin.TexC);
	float reveal = saturate(SampleOitReveal(pin.TexC));

	const float accumAlpha = max(accum.a, 1e-5f);
	const float3 transparentColor = accum.rgb / accumAlpha;
	const float transmittance = reveal;

	float3 finalColor = transparentColor * (1.0f - transmittance) + opaqueColor * transmittance;
	return float4(saturate(finalColor), 1.0f);
}
