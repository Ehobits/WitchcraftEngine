#include "Core.hlsl"

struct VertexIn
{
	float3 PosL : POSITION;
	float4 Color : COLOR;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float4 Color : COLOR;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;
	float4 posW = mul(float4(vin.PosL, 1.0f), g_WorldTransform);
	vout.PosH = mul(posW, g_ViewProj);
	vout.Color = vin.Color;
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	return float4(saturate(pin.Color.rgb), saturate(pin.Color.a));
}
