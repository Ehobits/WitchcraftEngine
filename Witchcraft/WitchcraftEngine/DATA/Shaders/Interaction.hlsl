#include "Core.hlsl"
#include "SkinningHelpers.hlsli"

struct VertexIn
{
	float3 PosL       : POSITION;
	float4 Color      : COLOR;
	float3 NormalL    : NORMAL;
	float2 TexC       : TEXCOORD;
	float3 TangentU   : TANGENT;
	float3 BitangentU : BINORMAL;
#if defined(SKINNED_MESH) && SKINNED_MESH
	uint4 BoneIndices : BLENDINDICES;
	float4 BoneWeights : BLENDWEIGHT;
#endif
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

	float3 positionL = vin.PosL;
#if defined(SKINNED_MESH) && SKINNED_MESH
	positionL = SkinPositionL(vin.PosL, BuildSkinningMatrixFromWeights(vin.BoneIndices, vin.BoneWeights));
#endif

	float4 posW = mul(float4(positionL, 1.0f), g_WorldTransform);
	vout.PosH = mul(posW, g_ViewProj);
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
