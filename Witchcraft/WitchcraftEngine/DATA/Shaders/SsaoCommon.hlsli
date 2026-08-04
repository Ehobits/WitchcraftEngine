#ifndef WITCHCRAFT_SSAO_COMMON_HLSLI
#define WITCHCRAFT_SSAO_COMMON_HLSLI

#include "FullscreenQuad.hlsli"

cbuffer cbSsao : register(b0)
{
	float4x4 gProj;
	float4x4 gInvProj;
	float4x4 gInvView;
	float4x4 gProjTex;
	float4   gOffsetVectors[14];
	float4 gBlurWeights[3];
	float2 gRenderTargetSize;
	// Coordinates given in view space.
	float gOcclusionRadius;
	float gOcclusionFadeStart;
	float gOcclusionFadeEnd;
	float gSurfaceEpsilon;
	float gProjScaleX;
	float gProjScaleY;
	float gProjDepthA;
	float gProjDepthB;
};

Texture2D gNormalMap : register(t0);
Texture2D gDepthMap  : register(t1);

SamplerState gsamPointClamp  : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState gsamDepthMap    : register(s2);
SamplerState gsamLinearWrap  : register(s3);

float NdcDepthToViewDepth(float z_ndc)
{
	// z_ndc = A + B / viewZ.
	float denom = z_ndc - gProjDepthA;
	if (abs(denom) < 1e-4f)
	{
		denom = (denom >= 0.0f) ? 1e-4f : -1e-4f;
	}
	return gProjDepthB / denom;
}

float3 BuildViewRayFromTexCoord(float2 texC)
{
	float2 ndc = float2(2.0f * texC.x - 1.0f, 1.0f - 2.0f * texC.y);
	return float3(ndc.x / gProjScaleX, ndc.y / gProjScaleY, 1.0f);
}

float2 ProjectViewPosToTexCoord(float3 viewPos)
{
	float invZ = rcp(max(abs(viewPos.z), 1e-4f));
	float2 ndc = float2(
		viewPos.x * gProjScaleX * invZ,
		viewPos.y * gProjScaleY * invZ);
	return float2(0.5f * ndc.x + 0.5f, -0.5f * ndc.y + 0.5f);
}

float3 ReconstructViewPosition(float2 texC, float depthNdc)
{
	const float viewDepth = NdcDepthToViewDepth(depthNdc);
	return BuildViewRayFromTexCoord(texC) * viewDepth;
}

float2 ComputeSsaoInvRenderTargetSize()
{
	return 1.0f / max(gRenderTargetSize, float2(1.0f, 1.0f));
}

#endif
