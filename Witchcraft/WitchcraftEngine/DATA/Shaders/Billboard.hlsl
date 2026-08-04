#include "Core.hlsl"

#ifndef TRANSPARENT_PASS
#define TRANSPARENT_PASS 0
#endif

#ifndef TRANSPARENT_OIT_PASS
#define TRANSPARENT_OIT_PASS 0
#endif

struct VertexIn
{
	float3 PosL : POSITION;
	float4 Color : COLOR;
	float3 NormalL : NORMAL;
	float2 TexC : TEXCOORD;
	float3 TangentU : TANGENT;
	float3 BitangentU : BINORMAL;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD0;
	float4 Color : COLOR0;
	float4 PosW : POSITION0;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

	float3 center = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), g_WorldTransform).xyz + g_BillboardOffset.xyz;
	float3 toCamera = normalize(g_CameraPosW - center);
	float3 worldUp = float3(0.0f, 1.0f, 0.0f);
	float3 right = float3(1.0f, 0.0f, 0.0f);
	float3 up = float3(0.0f, 1.0f, 0.0f);

	const uint facingMode = (uint)(g_BillboardParams1.x + 0.5f);
	if (facingMode == 1u)
	{
		float3 flatForward = normalize(float3(toCamera.x, 0.0f, toCamera.z));
		if (length(flatForward) < 1e-4f)
			flatForward = float3(0.0f, 0.0f, 1.0f);
		right = normalize(cross(worldUp, flatForward));
		up = worldUp;
	}
	else
	{
		right = normalize(cross(worldUp, toCamera));
		if (length(right) < 1e-4f)
			right = float3(1.0f, 0.0f, 0.0f);
		up = normalize(cross(toCamera, right));
	}

	float halfWidth = max(g_BillboardParams0.x, 0.001f) * 0.5f;
	float halfHeight = max(g_BillboardParams0.y, 0.001f) * 0.5f;
	const uint billboardMode = (uint)(g_BillboardParams0.w + 0.5f);
	if (billboardMode == 1u)
	{
		float viewDepth = abs(mul(float4(center, 1.0f), g_View).z);
		float ndcHalfHeight = max(g_BillboardParams0.z, 1.0f) / max(g_RenderTargetSize.y, 1.0f);
		halfHeight = viewDepth * ndcHalfHeight / max(g_Proj[1][1], 1e-4f);
		halfWidth = halfHeight * (max(g_BillboardParams0.x, 0.001f) / max(g_BillboardParams0.y, 0.001f));
	}

	float2 localCorner = vin.PosL.xy;
	float3 posW = center + right * (localCorner.x * 2.0f * halfWidth) + up * (localCorner.y * 2.0f * halfHeight);

	vout.PosW = float4(posW, 1.0f);
	vout.PosH = mul(vout.PosW, g_ViewProj);
	vout.TexC = mul(float4(vin.TexC, 0.0f, 1.0f), g_TexTransform).xy;
	vout.Color = vin.Color * g_BillboardColor;
	return vout;
}

#if TRANSPARENT_OIT_PASS == 1
struct TransparentOitOutput
{
	float4 Accum : SV_Target0;
	float4 Reveal : SV_Target1;
};

float ComputeOitWeight(float alpha, float depth01)
{
	float clampedAlpha = saturate(alpha);
	float clampedDepth = saturate(depth01);
	float depthWeight = pow(saturate(1.0f - clampedDepth), 3.0f);
	float alphaWeight = 0.05f + clampedAlpha * 0.95f;
	return max(1e-2f, depthWeight * (200.0f * alphaWeight));
}

TransparentOitOutput PS(VertexOut pin)
#else
float4 PS(VertexOut pin) : SV_Target
#endif
{
	float4 baseColor = g_DiffuseAlbedo * pin.Color;
	if (g_UseDiffuseTexture != 0)
		baseColor *= g_TextureArray[0].Sample(g_SamAnisotropicWrap, pin.TexC);

	float alpha = saturate(baseColor.a * g_Opacity);
	clip(alpha - 0.001f);

#if TRANSPARENT_OIT_PASS == 1
	float depth01 = pin.PosH.z / max(pin.PosH.w, 1e-4f);
	float weight = ComputeOitWeight(alpha, depth01);
	TransparentOitOutput output;
	output.Accum = float4(baseColor.rgb, alpha) * weight;
	output.Reveal = float4(alpha, alpha, alpha, alpha);
	return output;
#else
	baseColor.a = alpha;
	return baseColor;
#endif
}
