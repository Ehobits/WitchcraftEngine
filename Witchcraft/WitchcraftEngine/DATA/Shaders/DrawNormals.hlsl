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
#include "SkinningHelpers.hlsli"

struct VertexIn
{
	float3 PosL : POSITION;
	float3 NormalL : NORMAL;
	float2 TexC : TEXCOORD;
	float3 TangentU : TANGENT;
	float3 BitangentU : BINORMAL;
#if defined(SKINNED_MESH) && SKINNED_MESH
	uint4 BoneIndices : BLENDINDICES;
	float4 BoneWeights : BLENDWEIGHT;
#endif
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float3 NormalW : NORMAL;
	float3 TangentW : TANGENT;
	float3 BitangentW : BINORMAL;
	float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

#if defined(SKINNED_MESH) && SKINNED_MESH
	const float4x4 skinningMatrix = BuildSkinningMatrixFromWeights(vin.BoneIndices, vin.BoneWeights);
	const float3 positionL = SkinPositionL(vin.PosL, skinningMatrix);
	const float3 normalL = SkinDirectionL(vin.NormalL, skinningMatrix);
	const float3 tangentL = SkinDirectionL(vin.TangentU, skinningMatrix);
	const float3 bitangentL = SkinDirectionL(vin.BitangentU, skinningMatrix);
#else
	const float3 positionL = vin.PosL;
	const float3 normalL = vin.NormalL;
	const float3 tangentL = vin.TangentU;
	const float3 bitangentL = vin.BitangentU;
#endif

	// 假设尺度均匀；否则需要使用世界矩阵的逆转置。
	vout.NormalW = mul(normalL, (float3x3)g_WorldTransform);
	vout.TangentW = mul(tangentL, (float3x3)g_WorldTransform);
	vout.BitangentW = mul(bitangentL, (float3x3)g_WorldTransform);

	// 变换到裁剪空间。
	float4 posW = mul(float4(positionL, 1.0f), g_WorldTransform);
	vout.PosH = mul(posW, g_ViewProj);

	// 输出纹理坐标。
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), g_TexTransform);
	vout.TexC = texC.xy;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// 重新归一化插值后的法线。
	pin.NormalW = normalize(pin.NormalW);
	float3 N = pin.NormalW;

	// 透明材质在 AO 法线预通道中同样按最终 alpha 裁剪，
	// 避免整张四边形都写法线导致 AO 外轮廓错误。
	float diffuseTextureAlpha = 1.0f;
	if (g_UseDiffuseTexture != 0)
	{
		diffuseTextureAlpha = saturate(g_TextureArray[0].Sample(g_SamAnisotropicWrap, pin.TexC).a);
	}
	const float finalAlpha = saturate(g_Opacity * diffuseTextureAlpha);
	clip(finalAlpha - 0.001f);

	if (g_UseNormalTexture)
	{
		uint normalTexWidth = 1;
		uint normalTexHeight = 1;
		uint normalMipLevels = 1;
		g_TextureArray[1].GetDimensions(0, normalTexWidth, normalTexHeight, normalMipLevels);

		const float2 texelSize = 1.0f / max(float2((float)normalTexWidth, (float)normalTexHeight), float2(1.0f, 1.0f));
		const float2 uv = pin.TexC;
		float3 TN = 0.0f;
		TN += g_TextureArray[1].Sample(g_SamAnisotropicWrap, uv).rgb * 0.40f;
		TN += g_TextureArray[1].Sample(g_SamAnisotropicWrap, uv + float2(texelSize.x, 0.0f)).rgb * 0.15f;
		TN += g_TextureArray[1].Sample(g_SamAnisotropicWrap, uv + float2(-texelSize.x, 0.0f)).rgb * 0.15f;
		TN += g_TextureArray[1].Sample(g_SamAnisotropicWrap, uv + float2(0.0f, texelSize.y)).rgb * 0.15f;
		TN += g_TextureArray[1].Sample(g_SamAnisotropicWrap, uv + float2(0.0f, -texelSize.y)).rgb * 0.15f;
		N = NormalSampleToWorldSpace(TN, pin.NormalW, pin.TangentW, pin.BitangentW);
	}

	float3 normalV = mul(N, (float3x3)g_View);
	return float4(normalV, 1.0f);
}
