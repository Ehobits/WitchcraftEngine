#include "PostProcessCommon.hlsli"

Texture2D g_SceneColor : register(t0);
typedef PostProcessVertexOut VertexOut;

PostProcessVertexOut VS(uint vertexId : SV_VertexID)
{
	return BuildPostProcessFullscreenQuadVertex(vertexId);
}

float3 SampleSceneColor(float2 uv)
{
	return g_SceneColor.Sample(g_SamLinearClamp, uv).rgb;
}

float4 PS(PostProcessVertexOut pin) : SV_Target
{
	float3 sceneColor = SampleSceneColor(pin.TexC);
	float3 finalColor = saturate(sceneColor);
	return float4(finalColor, 1.0f);
}
