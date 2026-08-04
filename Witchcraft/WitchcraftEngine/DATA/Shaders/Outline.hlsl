#include "Core.hlsl"
#include "SkinningHelpers.hlsli"

struct VertexIn
{
	float3 PosL : POSITION;
	float4 Color : COLOR;
	float3 NormalL : NORMAL;
	float2 TexC : TEXCOORD;
	float3 TangentU : TANGENT;
	float3 BitangentU : BINORMAL;
#if defined(SKINNED_MESH) && SKINNED_MESH
	uint4 BoneIndices : BLENDINDICES;
	float4 BoneWeights : BLENDWEIGHT;
#endif
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float4 Color : COLOR;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

#if defined(SKINNED_MESH) && SKINNED_MESH
	const float4x4 skinningMatrix = BuildSkinningMatrixFromWeights(vin.BoneIndices, vin.BoneWeights);
	const float3 positionL = SkinPositionL(vin.PosL, skinningMatrix);
	const float3 normalL = SkinDirectionL(vin.NormalL, skinningMatrix);
#else
	const float3 positionL = vin.PosL;
	const float3 normalL = vin.NormalL;
#endif

	float4 posW = mul(float4(positionL, 1.0f), g_WorldTransform);
	float4 posH = mul(posW, g_ViewProj);
	float3 normalW = normalize(mul(normalL, (float3x3)g_WorldTransform));
	float3 normalV = mul(normalW, (float3x3)g_View);

	const float outlineThicknessPixels = 4.2f;
	const float minDirectionEpsilon = 1e-4f;
	const float2 invRenderTargetSize = 1.0f / max(g_RenderTargetSize, float2(1.0f, 1.0f));

	float2 normalDir = normalV.xy;
	float normalDirLen = length(normalDir);

	float4 centerW = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), g_WorldTransform);
	float4 centerH = mul(centerW, g_ViewProj);
	float safePosW = max(abs(posH.w), minDirectionEpsilon);
	float safeCenterW = max(abs(centerH.w), minDirectionEpsilon);
	float2 radialDir = (posH.xy / safePosW) - (centerH.xy / safeCenterW);
	float radialDirLen = length(radialDir);

	float2 outlineDir = float2(1.0f, 0.0f);
	if (normalDirLen > minDirectionEpsilon)
	{
		outlineDir = normalDir / normalDirLen;
		if (radialDirLen > minDirectionEpsilon)
		{
			float2 blendedDir = outlineDir + (radialDir / radialDirLen) * 0.35f;
			float blendedDirLen = length(blendedDir);
			if (blendedDirLen > minDirectionEpsilon)
				outlineDir = blendedDir / blendedDirLen;
		}
	}
	else if (radialDirLen > minDirectionEpsilon)
	{
		outlineDir = radialDir / radialDirLen;
	}

	float2 ndcOffset = outlineDir * (outlineThicknessPixels * 2.0f) * invRenderTargetSize;
	posH.xy += ndcOffset * posH.w;

	const float outlineDepthPush = 1.5e-3f;
	posH.z += outlineDepthPush * posH.w;

	vout.PosH = posH;
	vout.Color = saturate(vin.Color);
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	const float2 invRenderTargetSize = 1.0f / max(g_RenderTargetSize, float2(1.0f, 1.0f));
	float2 uv = pin.PosH.xy * invRenderTargetSize;
	float insideMask = 0.0f;
	if (uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f)
		insideMask = g_SkyTextureArray.Sample(g_SamPointClamp, uv).r;

	if (insideMask > 0.5f)
		discard;

	return float4(saturate(pin.Color.rgb), 1.0f);
}
