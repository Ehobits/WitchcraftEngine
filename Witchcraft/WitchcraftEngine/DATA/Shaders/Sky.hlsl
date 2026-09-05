// Include common HLSL code.
#include "Core.hlsl"
#include "SkySampling.hlsli"

struct VertexIn
{
	float3 PosL    : POSITION;
	float4 Color   : COLOR;
	float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float4 Color : COLOR;
	float3 PosL : POSITION;
	float2 TexC : TEXCOORD;
};
 
VertexOut VS(VertexIn vin)
{
	VertexOut vout;

	// Use local vertex position as map lookup vector.
	vout.PosL = vin.PosL;
	
	// Transform to world space.
	float4 posW = mul(float4(vin.PosL, 1.0f), g_WorldTransform);

	// Always center sky about camera.
	posW.xyz += g_CameraPosW;

	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
	vout.PosH = mul(posW, g_ViewProj).xyww;
	
	vout.Color = vin.Color;
	vout.TexC = vin.TexC;
	
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{	
	// Sphere.obj 没有有效的 UV，且当前天空贴图是经纬展开的 2D HDRI，
	// 因此这里直接由观察方向反推经纬 UV，而不是依赖模型自带 TexC。
	float3 dir = normalize(pin.PosL);
	dir = normalize(mul(float4(dir, 0.0f), g_TexTransform).xyz);
	float2 skyUv = DirectionToEquirectSkyUv(dir);

	float4 skyAlbedoColor = g_SkyTextureArray.Sample(g_SamLinearWrap, skyUv);
	
	// HDR tonemapping
	//skyAlbedoColor = skyAlbedoColor / (skyAlbedoColor + float4(1.0f, 1.0f, 1.0f,1.0f));
	// gamma correction
	//skyAlbedoColor = pow(skyAlbedoColor, (1.0f / 2.2f));
	return float4(skyAlbedoColor.rgb, 1.0f);
}

