#include "PBRXCore.hlsl"

struct MaterialData
{
	float Metallic; //金属度
	float Roughness; //粗糙度
	float3 FresnelR0; //菲涅耳R0
	float3 Luminescence; //自发光
	float Opacity; //不透明度
	float4x4 MatTransform;//材质变换
	
	uint SkyMapIndex;
	uint DiffuseMapIndex;
	uint NormalMapIndex;
	uint SpecularMapIndex;
	uint MetallicMapIndex;
	uint RoughnessMapIndex;
	uint FresnelR0MapIndex;
	uint DisplacementMapIndex;
	uint BumpMapIndex;
	uint AmbientOcclusionMapIndex;
	uint CavityMapIndex;
	uint SheenMapIndex;
	uint EmissiveMapIndex;
	float2 SkyTexC;
	
	//uint     MatPad1;
	//uint     MatPad2;
};

//MaterialData
StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);

//opaque
Texture2D gTextureMaps[12] : register(t1, space1);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamPointBorder : register(s2);
SamplerState gsamLinearWrap : register(s3);
SamplerState gsamLinearClamp : register(s4);
SamplerState gsamAnisotropicWrap : register(s5);
SamplerState gsamAnisotropicClamp : register(s6);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b1)
{
	float4x4 gWorld;
	float4x4 gTexTransform;
	uint gMaterialIndex;
};

// Constant data that varies per material.
cbuffer cbPass : register(b2)
{
	float4x4 gView;
	float4x4 gInvView;
	float4x4 gProj;
	float4x4 gInvProj;
	float4x4 gViewProj;
	float4x4 gInvViewProj;
	float3 gEyePosW;
	//float cbPerObjectPad1;
	float2 gRenderTargetSize;
	float2 gInvRenderTargetSize;
	float gNearZ;
	float gFarZ;
};

cbuffer cbLight : register(b3)
{
	float4 gAmbientLight;

	// Indices [0, NUM_DIR_LIGHTS) are directional lights;
	// indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
	// indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
	// are spot lights for a maximum of MaxLights per object.
	LightData gLights[MaxLights];
};

cbuffer cbMaterial : register(b4)
{
	float gMetallic;
	float gRoughness;
	float3 gFresnelR0;
	float3 gLuminescence;
	float gOpacity;
	float4x4 gMatTransform;
	
	uint gSkyMapIndex;
	uint gDiffuseMapIndex;
	uint gNormalMapIndex;
	uint gSpecularMapIndex;
	uint gMetallicMapIndex;
	uint gRoughnessMapIndex;
	uint gFresnelR0MapIndex;
	uint gDisplacementMapIndex;
	uint gBumpMapIndex;
	uint gAmbientOcclusionMapIndex;
	uint gCavityMapIndex;
	uint gSheenMapIndex;
	uint gEmissiveMapIndex;
	float2 gSkyTexC;
}

//---------------------------------------------------------------------------------------
// Transforms a normal map sample to world space.
//---------------------------------------------------------------------------------------
float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW, float3 BitangentW)
{
	// Uncompress each component from [0,1] to [-1,1].
	float3 normalT = 2.0f * normalMapSample - 1.0f;

	// Build orthonormal basis.
	float3 N = unitNormalW;
	float3 T = normalize(tangentW - dot(tangentW, N) * N);
	float3 B = BitangentW; //cross(N, T);

	float3x3 TBN = float3x3(T, B, N);

	// Transform from tangent space to world space.
	float3 bumpedNormalW = mul(normalT, TBN);

	return bumpedNormalW;
}

