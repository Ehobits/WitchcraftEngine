// Include common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float3 PosL : POSITION;
	float2 TexC : TEXCOORD;
};
 
VertexOut VS(VertexIn vin)
{
	VertexOut vout;

	// Fetch the material data.
	MaterialData matData = gMaterialData[gMaterialIndex];

	// Use local vertex position as map lookup vector.
	vout.PosL = vin.PosL;
	
	// Transform to world space.
	float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

	// Always center sky about camera.
	posW.xyz += gEyePosW;

	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
	vout.PosH = mul(posW, gViewProj).xyww;
	
	// 用于跨三角形插值的输出顶点属性。
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;

	matData.SkyTexC = vout.TexC;
	
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// Fetch the material data.
	MaterialData matData = gMaterialData[gMaterialIndex];
	
	float4 skyAlbedoColor = gTextureMaps[matData.DiffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
	
	return skyAlbedoColor;
 
}

