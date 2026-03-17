#include "Core.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

	// 转换到世界空间
	float4 posW = mul(float4(vin.PosL, 1.0f), g_WorldTransform);
	// Transform to homogeneous clip space.
	vout.PosH = mul(posW, g_ViewProj);

	// Already in homogeneous clip space.
	//vout.PosH = float4(vin.PosL, 1.0f);
	
	vout.TexC = vin.TexC;
	
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	return float4(g_AOMap.Sample(g_SamLinearWrap, pin.TexC).rgb, 0.0f);
}


