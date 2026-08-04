#include "Core.hlsl"
#include "SkinningHelpers.hlsli"

struct VertexIn
{
	float3 PosL    : POSITION;
	float2 TexC    : TEXCOORD;
#if defined(SKINNED_MESH) && SKINNED_MESH
	uint4 BoneIndices : BLENDINDICES;
	float4 BoneWeights : BLENDWEIGHT;
#endif
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;
	
	// Transform to world space.
	float3 posL = vin.PosL;
#if defined(SKINNED_MESH) && SKINNED_MESH
	const float4x4 skinningMatrix = BuildSkinningMatrixFromWeights(vin.BoneIndices, vin.BoneWeights);
	posL = SkinPositionL(vin.PosL, skinningMatrix);
#endif
	float4 posW = mul(float4(posL, 1.0f), g_WorldTransform);

	// Transform to homogeneous clip space.
	vout.PosH = mul(posW, g_ViewProj);
	
	// Output vertex attributes for interpolation across triangle.
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), g_TexTransform);
	vout.TexC = texC;
	
	return vout;
}

//这仅用于阿尔法剪切几何体，以便阴影正确显示。
//不需要采样纹理的几何体可以使用NULL像素着色器进行深度传递。
void PS(VertexOut pin) 
{

}


