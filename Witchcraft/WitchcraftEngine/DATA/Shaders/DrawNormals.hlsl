//***************************************************************************************
// DrawNormals.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

// 绘制全局法线贴图
// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
	#define NUM_DIR_LIGHTS 0
#endif

#ifndef NUM_POINT_LIGHTS
	#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
	#define NUM_SPOT_LIGHTS 0
#endif

// Include common HLSL code.
#include "Core.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
	float3 TangentU : TANGENT;
	float3 BitangentU : BINORMAL;
};

struct VertexOut
{
	float4 PosH     : SV_POSITION;
	float3 NormalW  : NORMAL;
	float3 TangentW : TANGENT;
	float3 BitangentW : BINORMAL;
	float2 TexC     : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

	// Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
	vout.NormalW = mul(vin.NormalL, (float3x3) g_WorldTransform);
	vout.TangentW = mul(vin.TangentU, (float3x3) g_WorldTransform);
	vout.BitangentW = mul(vin.BitangentU, (float3x3) g_WorldTransform);

	// Transform to homogeneous clip space.
	float4 posW = mul(float4(vin.PosL, 1.0f), g_WorldTransform);
	vout.PosH = mul(posW, g_ViewProj);
	
	// Output vertex attributes for interpolation across triangle.
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), g_TexTransform);
	vout.TexC = texC;
	
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// Interpolating normal can unnormalize it, so renormalize it.
	pin.NormalW = normalize(pin.NormalW);
	float3 N = pin.NormalW;

	if (g_UseNormalTexture)
	{
		float3 TN = g_TextureArray[1].Sample(g_SamAnisotropicWrap, pin.TexC).rgb;
		N = NormalSampleToWorldSpace(TN, pin.NormalW, pin.TangentW, pin.BitangentW);
	}


	// NOTE: We use interpolated vertex normal for SSAO.

	// Write normal in view space coordinates
	float3 normalV = mul(N, (float3x3) g_View);
	return float4(normalV, 1.0f);
}


