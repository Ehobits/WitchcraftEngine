#include "Core.hlsl"

struct VertexIn
{
	float3 PosL : POSITION;
	float4 Color : COLOR;
	float HandleId : TEXCOORD0;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float4 Color : COLOR;
	float HandleId : TEXCOORD0;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;
	float4 posW = mul(float4(vin.PosL, 1.0f), g_WorldTransform);
	vout.PosH = mul(posW, g_ViewProj);
	vout.Color = vin.Color;
	vout.HandleId = vin.HandleId;
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	const float hoverHandle = g_TexTransform[0][0];
	const float activeHandle = g_TexTransform[1][1];

	float3 color = pin.Color.rgb;
	if (abs(pin.HandleId - activeHandle) < 0.25f)
		color = lerp(color, float3(1.0f, 0.95f, 0.35f), 0.75f);
	else if (abs(pin.HandleId - hoverHandle) < 0.25f)
		color = lerp(color, float3(1.0f, 1.0f, 1.0f), 0.45f);

	return float4(saturate(color), saturate(pin.Color.a));
}
