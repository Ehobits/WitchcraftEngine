#ifndef WITCHCRAFT_POST_PROCESS_COMMON_HLSLI
#define WITCHCRAFT_POST_PROCESS_COMMON_HLSLI

#include "FullscreenQuad.hlsli"

cbuffer cbPostProcess : register(b0)
{
	float2 g_RenderTargetSize;
	float4 g_FxaaSettings;
};

SamplerState g_SamLinearClamp : register(s3);

struct PostProcessVertexOut
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD0;
};

PostProcessVertexOut BuildPostProcessFullscreenQuadVertex(uint vertexId)
{
	PostProcessVertexOut vout;
	vout.TexC = BuildFullscreenQuadTexCoord(vertexId);
	vout.PosH = BuildFullscreenQuadPositionH(vout.TexC);
	return vout;
}

#endif
