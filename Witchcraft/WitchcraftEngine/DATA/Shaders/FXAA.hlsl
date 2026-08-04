#include "PostProcessCommon.hlsli"

Texture2D g_SceneColor : register(t0);

float2 ComputeInvRenderTargetSize()
{
	return 1.0f / max(g_RenderTargetSize, float2(1.0f, 1.0f));
}

PostProcessVertexOut VS(uint vertexId : SV_VertexID)
{
	return BuildPostProcessFullscreenQuadVertex(vertexId);
}

float3 SampleSceneColor(float2 uv)
{
	return g_SceneColor.Sample(g_SamLinearClamp, uv).rgb;
}

float Luma(float3 color)
{
	return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float3 ApplyFxaa(float2 uv)
{
	float2 texelSize = ComputeInvRenderTargetSize();
	float contrastThreshold = g_FxaaSettings.y;
	float relativeThreshold = g_FxaaSettings.z;
	float spanMax = max(g_FxaaSettings.w, 1.0f);

	float3 rgbM = SampleSceneColor(uv);
	float3 rgbNW = SampleSceneColor(uv + texelSize * float2(-1.0f, -1.0f));
	float3 rgbNE = SampleSceneColor(uv + texelSize * float2(1.0f, -1.0f));
	float3 rgbSW = SampleSceneColor(uv + texelSize * float2(-1.0f, 1.0f));
	float3 rgbSE = SampleSceneColor(uv + texelSize * float2(1.0f, 1.0f));

	float lumaM = Luma(rgbM);
	float lumaNW = Luma(rgbNW);
	float lumaNE = Luma(rgbNE);
	float lumaSW = Luma(rgbSW);
	float lumaSE = Luma(rgbSE);

	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
	float localContrast = lumaMax - lumaMin;

	if (localContrast < max(contrastThreshold, lumaMax * relativeThreshold))
		return rgbM;

	float2 dir = float2(
		-((lumaNW + lumaNE) - (lumaSW + lumaSE)),
		((lumaNW + lumaSW) - (lumaNE + lumaSE)));

	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25f * 0.125f), 1.0f / 128.0f);
	float rcpDirMin = 1.0f / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = clamp(dir * rcpDirMin, float2(-spanMax, -spanMax), float2(spanMax, spanMax)) * texelSize;

	float3 rgbA = 0.5f * (
		SampleSceneColor(uv + dir * (1.0f / 3.0f - 0.5f)) +
		SampleSceneColor(uv + dir * (2.0f / 3.0f - 0.5f)));

	float3 rgbB = rgbA * 0.5f + 0.25f * (
		SampleSceneColor(uv + dir * -0.5f) +
		SampleSceneColor(uv + dir * 0.5f));

	float lumaB = Luma(rgbB);
	if (lumaB < lumaMin || lumaB > lumaMax)
		return rgbA;

	return rgbB;
}

float4 PS(PostProcessVertexOut pin) : SV_Target
{
	float3 sceneColor = ApplyFxaa(pin.TexC);
	float3 finalColor = saturate(sceneColor);
	return float4(finalColor, 1.0f);
}
