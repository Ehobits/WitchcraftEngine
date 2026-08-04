#include "LightingUtil.hlsl"

Texture2D g_SkyTextureArray : register(t0);
Texture2D g_TextureArray[12] : register(t1);
static const uint G_MAX_DIRECTIONAL_SHADOW_MAP_COUNT = 32u;
static const uint G_MAX_SPOT_SHADOW_MAP_COUNT = 224u;
Texture2D g_DirectionalShadowMap[G_MAX_DIRECTIONAL_SHADOW_MAP_COUNT] : register(t13);
Texture2D g_SpotShadowMap[G_MAX_SPOT_SHADOW_MAP_COUNT] : register(t45);
TextureCube g_PointLightShadowCube[64] : register(t269);  // 点光源阴影 cubemap 数组
Texture2D g_AOMap : register(t333);
Texture2D g_DirectionalShadowMask : register(t334);

SamplerState g_SamPointWrap : register(s0);
SamplerState g_SamPointClamp : register(s1);
SamplerState g_SamLinearWrap : register(s2);
SamplerState g_SamLinearClamp : register(s3);
SamplerState g_SamAnisotropicWrap : register(s4);
SamplerState g_SamAnisotropicClamp : register(s5);
SamplerComparisonState g_SamShadow : register(s6);
SamplerComparisonState g_SamShadowCube : register(s7);  // cubemap 阴影采样器

// 每个对象变化的常量数据
cbuffer cbPerObject : register(b0)
{
	float4x4 g_WorldTransform;
	float4x4 g_TexTransform;
};

// 每帧变化的常量数据
cbuffer cbPass : register(b1)
{
	float4x4 g_View;
	float4x4 g_InvView;
	float4x4 g_Proj;
	float4x4 g_InvProj;
	float4x4 g_ViewProj;
	float4x4 g_InvViewProj;
	float4x4 g_ViewProjTex;
	float3 g_CameraPosW;
	float __g_pass_PAD000;
	float2 g_RenderTargetSize;
	float4x4 g_ShadowTransform[256];
	float2 g_ShadowSettings;
	float2 g_AOSettings;
	float4 g_ShadowMaskSettings;
	float4 g_DirectionalShadowCascadeSplits;
	float4 g_DirectionalShadowCascadeSettings;
	float4 g_DirectionalShadowCascadeWorldTexelSize;
	float4 g_DirectionalShadowCascadeDepthScale;
	uint g_LightConst;
	float3 __g_pass_PAD001;
};

cbuffer cbLightPass : register(b2)
{
	float4 g_AmbientColor;
	
	Light g_Lights[256];
};

// Constant data that varies per material
cbuffer cbMaterial : register(b3)
{
	float4 g_DiffuseAlbedo;
	float3 g_FresnelR0;
	float g_Opacity;
	float3 g_Transmission;
	float __g_mat_pass_PAD001;
	float3 g_Emissive;
	float __g_mat_pass_PAD002;
	float g_Metallic;
	float g_Roughness;
	float g_ClearCoatThickness;
	float g_ClearCoatRoughness;
	float g_Anisotropy;
	float g_AnisotropyRotation;
	float2 __g_mat_pass_PAD003;
	uint g_UseDiffuseTexture;
	uint g_UseNormalTexture;
	uint g_UseMetallicTexture;
	uint g_UseRoughnessTexture;
	uint g_UseSpecularTexture;
	float4 __g_mat_pass_PAD005;
};

cbuffer cbSkinning : register(b4)
{
	float4x4 g_BoneMatrices[256];
};
