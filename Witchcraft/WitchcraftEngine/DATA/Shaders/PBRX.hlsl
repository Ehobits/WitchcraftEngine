#include "PBRXShadowHelpers.hlsl"
#include "SkinningHelpers.hlsli"

#ifndef TRANSPARENT_PASS
#define TRANSPARENT_PASS 0
#endif

#ifndef TRANSPARENT_OIT_PASS
#define TRANSPARENT_OIT_PASS 0
#endif

#ifndef TRANSPARENT_OPAQUE_CUTOFF_PASS
#define TRANSPARENT_OPAQUE_CUTOFF_PASS 0
#endif

struct VertexIn
{
	float3 PosL			: POSITION;
	float4 Color		: COLOR;
	float3 NormalL		: NORMAL;
	float2 TexC			: TEXCOORD;
	float3 TangentU		: TANGENT;
	float3 BitangentU	: BINORMAL;
#if defined(SKINNED_MESH) && SKINNED_MESH
	uint4 BoneIndices	: BLENDINDICES;
	float4 BoneWeights	: BLENDWEIGHT;
#endif
};

struct VertexOut
{
	float4 PosH			: SV_POSITION;
	float4 Color		: COLOR;
	float4 PosW			: POSITION;
	float3 NormalW		: NORMAL;
	float2 TexT			: TEXCOORD;
	float2 TexC			: TEXCOORD;
	float3 TangentW		: TANGENT;
	float3 BitangentW	: BINORMAL;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

#if defined(SKINNED_MESH) && SKINNED_MESH
	const float4x4 skinningMatrix = BuildSkinningMatrixFromWeights(vin.BoneIndices, vin.BoneWeights);
	const float4 skinnedPosL = float4(SkinPositionL(vin.PosL, skinningMatrix), 1.0f);
	const float3 skinnedNormalL = SkinDirectionL(vin.NormalL, skinningMatrix);
	const float3 skinnedTangentL = SkinDirectionL(vin.TangentU, skinningMatrix);
	const float3 skinnedBitangentL = SkinDirectionL(vin.BitangentU, skinningMatrix);
#else
	const float4 skinnedPosL = float4(vin.PosL, 1.0f);
	const float3 skinnedNormalL = vin.NormalL;
	const float3 skinnedTangentL = vin.TangentU;
	const float3 skinnedBitangentL = vin.BitangentU;
#endif
	
	// 转换到世界空间
	vout.PosW = mul(skinnedPosL, g_WorldTransform);

	// 假设缩放不均匀；否则使用反向转置
	vout.NormalW = mul(skinnedNormalL, (float3x3) g_WorldTransform);

	// 将切线和双切线转换为世界空间
	vout.TangentW = mul(skinnedTangentL, (float3x3) g_WorldTransform);
	vout.BitangentW = mul(skinnedBitangentL, (float3x3) g_WorldTransform);

	// 转换为均匀裁切空间。
	vout.PosH = mul(vout.PosW, g_ViewProj);

	vout.Color = vin.Color;
	
	vout.TexT = vin.TexC;
	// 输出用于跨三角形插值的顶点属性。
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), g_TexTransform);
	vout.TexC = texC;

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
	const float clampedAlpha = saturate(alpha);
	const float clampedDepth = saturate(depth01);

	// 前景片元更高权重，弱化远处/背后片元泄露。
	float depthWeight = pow(saturate(1.0f - clampedDepth), 3.0f);
	float alphaWeight = 0.05f + clampedAlpha * 0.95f;
	float weight = max(1e-2f, depthWeight * (200.0f * alphaWeight));

	// alpha 接近 1 的片元提升权重，尽量逼近“看起来不透明”的效果。
	if (clampedAlpha >= 0.999f)
		weight *= 64.0f;

	return weight;
}

TransparentOitOutput PS(VertexOut pin, bool isFrontFace : SV_IsFrontFace)
#else
float4 PS(VertexOut pin, bool isFrontFace : SV_IsFrontFace) : SV_Target
#endif
{
	// 插值正态可以使其非正态化，因此可以对其进行重整正态化。
	pin.NormalW = normalize(pin.NormalW);
	
	// 从被照亮的点到眼睛的矢量。
	float3 V = normalize(g_CameraPosW - pin.PosW.xyz);

	const float2 TexC = pin.TexC;
	const float materialOpacity = saturate(g_Opacity);
	float diffuseTextureAlpha = 1.0f;
	float finalAlpha = materialOpacity;

	// Diffuse color:
	float4 surfaceDiffuseAlbedo = g_DiffuseAlbedo;
	// - 漫反射颜色 alpha 仅用于控制颜色叠加强度，不参与最终透明度。
	// - 最终透明度由独立 Opacity 与漫反射贴图 alpha 相乘得到。
	const float diffuseColorBlend = 1.0f - saturate(g_DiffuseAlbedo.a);
	if (g_UseDiffuseTexture != 0)
	{
		const float4 sampledDiffuseAlbedo = g_TextureArray[0].Sample(g_SamAnisotropicWrap, TexC);
		diffuseTextureAlpha = saturate(sampledDiffuseAlbedo.a);
		surfaceDiffuseAlbedo.rgb = sampledDiffuseAlbedo.rgb + saturate(g_DiffuseAlbedo.rgb) * diffuseColorBlend;
	}
	else
	{
		surfaceDiffuseAlbedo.rgb = saturate(g_DiffuseAlbedo.rgb);
	}
	// 最终透明度遵循现有材质规则：材质 opacity 与漫反射贴图 alpha 相乘。
	finalAlpha = saturate(materialOpacity * diffuseTextureAlpha);
	clip(finalAlpha - 0.001f);
#if TRANSPARENT_OPAQUE_CUTOFF_PASS == 1
	// 近不透明子通道：仅保留 alpha 接近 1 的片元。
	clip(finalAlpha - 0.999f);
#endif
#if TRANSPARENT_OIT_PASS == 1
	// OIT 子通道：只保留真正半透明片元，避免 alpha≈1 进入 OIT 导致发灰与竞争。
	clip(0.999f - finalAlpha);
#endif
	surfaceDiffuseAlbedo = saturate(surfaceDiffuseAlbedo);
	surfaceDiffuseAlbedo.a = 1.0f;

	// 金属度
	float metallic = g_Metallic;
	if (g_UseMetallicTexture != 0)
	{
		metallic += g_TextureArray[3].Sample(g_SamAnisotropicWrap, TexC).r;
	}
	metallic = clamp(metallic, 0.0f, 1.0f);

	float3 F0 = g_FresnelR0;
	if (g_UseSpecularTexture != 0)
	{
		F0 += g_TextureArray[2].Sample(g_SamAnisotropicWrap, TexC).rgb;
	}
	F0 = clamp(F0, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));

	// 粗糙度
	float roughness = g_Roughness;
	if (g_UseRoughnessTexture != 0)
	{
		roughness += g_TextureArray[4].Sample(g_SamAnisotropicWrap, TexC).r;
	}
	roughness = clamp(roughness, 0.0f, 1.0f);

	float3 N = pin.NormalW;
	if (g_UseNormalTexture)
	{
		float3 TN = g_TextureArray[1].Sample(g_SamAnisotropicWrap, TexC).rgb;
		N = NormalSampleToWorldSpace(TN, pin.NormalW, pin.TangentW, pin.BitangentW);
	}
	if (!isFrontFace)
		N = -N;

	// Light terms.

	Material mat =
	{
		surfaceDiffuseAlbedo,
		metallic,
		F0,
		roughness,
		g_Transmission,
		g_Emissive,
		1.0f,
		1.0f,
		g_ClearCoatThickness,
		g_ClearCoatRoughness,
		g_Anisotropy,
		g_AnisotropyRotation
	};
	
	float3 VN = normalize(V + N);
	
	float phi = atan2(VN.z, VN.x);
	float theta = acos(N.y);

	float2 r = float2(phi / TwoPI, theta / PI);

	// 预过滤颜色
	float4 prefilteredColor = g_SkyTextureArray.Sample(g_SamLinearWrap, r);
	float reflectionStrength = (1.0f - roughness) * metallic;
	if (g_ReflectionSource == 1u)
	{
		// ReflectionViewProjTex 是 CPU 侧为镜面接收面计算出的世界坐标到贴图坐标矩阵。
		// 它不是模型 UV；接收面的世界坐标仍参与映射，所以不会依赖模型自身 UV 是否规整。
		float4 reflectionPos = mul(pin.PosW, g_ReflectionViewProjTex);
		if (reflectionPos.w > 0.0f)
		{
			reflectionPos.xyz /= reflectionPos.w;
			if (reflectionPos.x >= 0.0f && reflectionPos.x <= 1.0f &&
				reflectionPos.y >= 0.0f && reflectionPos.y <= 1.0f &&
				reflectionPos.z >= 0.0f && reflectionPos.z <= 1.0f)
			{
				const float4 renderTextureReflection = g_ReflectionTexture.SampleLevel(
					g_SamLinearClamp,
					reflectionPos.xy,
					0.0f);
				prefilteredColor = lerp(renderTextureReflection, prefilteredColor, saturate(roughness));
				reflectionStrength = 1.0f - roughness;
			}
		}
	}
	// 将它们组合在一起，以获得 IBL/RenderTexture 镜面部分。
	float4 specular_ab = prefilteredColor * reflectionStrength;


	float shadowFactor = 0.0f;
	float3 directLight = float3(0.0f, 0.0f, 0.0f);
	const uint lightCount = min(g_LightConst, 256u);
	for (uint i = 0; i < lightCount; i++)
	{
		const int shadowBaseIndex = (int)g_Lights[i].ShadowTextureIndex;
		shadowFactor = ComputeLightShadowFactor(g_Lights[i], shadowBaseIndex, pin.PosW, pin.NormalW);
		if (g_ShadowMaskSettings.x > 0.5f &&
			(g_Lights[i].ShadowSamplingMode == SHADOW_MODE_DIRECTIONAL_CASCADE || g_Lights[i].Type == DIRCTON_LIT) &&
			shadowBaseIndex == 0)
		{
			const float2 invScreenSize = 1.0f / max(g_RenderTargetSize, float2(1.0f, 1.0f));
			const float2 shadowMaskUv = saturate((pin.PosH.xy + float2(0.5f, 0.5f)) * invScreenSize);
			shadowFactor = saturate(g_DirectionalShadowMask.SampleLevel(g_SamLinearClamp, shadowMaskUv, 0.0f).r);
		}
		directLight += ComputeLighting(g_Lights[i], mat, pin.PosW.xyz,
			N, V) * shadowFactor;
	}
	
	// 色彩要乘上环境光强度
	surfaceDiffuseAlbedo.rgb *= g_AmbientColor.rgb * (1.0f - g_AmbientColor.a);
	// 乘上环境光强度
	specular_ab.rgb *= g_AmbientColor.rgb * (1.0f - g_AmbientColor.a);
	
	// 采样SSAO贴图。
	float ambientAccess = 1.0f;
	if (isFrontFace && (g_AOSettings.x > 0.5f))
	{
		const float2 invScreenSize = 1.0f / max(g_RenderTargetSize, float2(1.0f, 1.0f));
		const float2 aoUv = saturate((pin.PosH.xy + float2(0.5f, 0.5f)) * invScreenSize);
		const float aoSample = saturate(g_AOMap.SampleLevel(g_SamLinearClamp, aoUv, 0.0f).r);
		const float aoContrast = pow(aoSample, 1.9f);
		ambientAccess = lerp(1.0f, aoContrast, saturate(g_AOSettings.y));
	}

	surfaceDiffuseAlbedo *= ambientAccess;
	specular_ab *= lerp(1.0f, ambientAccess, 0.40f);
	
	// HDR tonemapping
	//specular_ab.rgb = specular_ab.rgb / (specular_ab.rgb + float3(1.2f, 1.2f, 1.2f));
	// gamma correction
	//specular_ab = pow(specular_ab, (1.0f / 2.0f));
	float4 litColor = surfaceDiffuseAlbedo + float4(directLight, 1.0f) + specular_ab;

	// Final alpha = material opacity + diffuse texture alpha (if present), clamped to [0,1].
	litColor.a = saturate(finalAlpha);
#if TRANSPARENT_OPAQUE_CUTOFF_PASS == 1
	litColor.a = 1.0f;
#endif

#if TRANSPARENT_PASS == 1 && TRANSPARENT_OPAQUE_CUTOFF_PASS == 0
	// 透明路径把 AO 作用到整体彩色响应，保证半透明物体上 AO 可见。
	litColor.rgb *= ambientAccess;
	// 透明通道输出预乘颜色，沿用原有材质透明度规则计算出来的 alpha。
	litColor.rgb *= litColor.a;
#endif

#if TRANSPARENT_OIT_PASS == 1
	TransparentOitOutput output;
	const float oitWeight = ComputeOitWeight(litColor.a, pin.PosH.z);
	output.Accum = float4(litColor.rgb * oitWeight, litColor.a * oitWeight);
	output.Reveal = float4(litColor.a, litColor.a, litColor.a, litColor.a);
	return output;
#else
	return litColor;
#endif
}
