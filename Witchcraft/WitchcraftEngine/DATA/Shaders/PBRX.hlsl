#include "Core.hlsl"

#define PI 3.14159265359
#define TwoPI 6.28318530718

struct VertexIn
{
	float3 PosL			: POSITION;
	float4 Color		: COLOR;
	float3 NormalL		: NORMAL;
	float2 TexC			: TEXCOORD;
	float3 TangentU		: TANGENT;
	float3 BitangentU	: BINORMAL;
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

float CalcShadowFactor(float4 shadowPos)
{
	// Complete projection by doing division by w.
	shadowPos.xyz /= shadowPos.w;

	// Depth in NDC space.
	float depth = shadowPos.z;

	uint width, height, numMips;
	g_ShadowMap.GetDimensions(0, width, height, numMips);

	// Texel size.
	float dx = 1.0f / (float) width;

	float percentLit = 0.0f;
	const float2 offsets[9] =
	{
		float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
		float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
		float2(-dx, +dx), float2(0.0f, +dx), float2(dx, +dx)
	};

	[unroll]
	for (uint i = 0; i < 9; ++i)
	{
		percentLit += g_ShadowMap.SampleCmpLevelZero(g_SamShadow,
			shadowPos.xy + offsets[i], depth).r;
	}
	
	return percentLit / 9.0f;
}

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;
	
	// 转换到世界空间
	vout.PosW = mul(float4(vin.PosL, 1.0f), g_WorldTransform);

	// 假设缩放不均匀；否则使用反向转置
	vout.NormalW = mul(vin.NormalL, (float3x3) g_WorldTransform);

	// 将切线和双切线转换为世界空间
	vout.TangentW = mul(vin.TangentU, (float3x3) g_WorldTransform);
	vout.BitangentW = mul(vin.BitangentU, (float3x3) g_WorldTransform);

	// 转换为均匀裁切空间。
	vout.PosH = mul(vout.PosW, g_ViewProj);

	vout.Color = vin.Color;
	
	vout.TexT = vin.TexC;
	// 输出用于跨三角形插值的顶点属性。
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), g_TexTransform);
	vout.TexC = texC;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// 插值正态可以使其非正态化，因此可以对其进行重整正态化。
	pin.NormalW = normalize(pin.NormalW);
	
	// 从被照亮的点到眼睛的矢量。
	float3 V = normalize(g_CameraPosW - pin.PosW.xyz);

	float2 TexC = pin.TexC;

	// 漫反射图
	float4 diffuseAlbedo = g_TextureArray[0].Sample(g_SamAnisotropicWrap, TexC);
	diffuseAlbedo.rgb += (g_DiffuseAlbedo.rgb * (1.0f - g_DiffuseAlbedo.a));
	clamp(diffuseAlbedo, float4(0.0f, 0.0f, 0.0f, 0.0f), float4(1.0f, 1.0f, 1.0f, 1.0f));

	// 金属度
	float metallic = g_TextureArray[3].Sample(g_SamAnisotropicWrap, TexC).r;
	metallic += g_Metallic;
	clamp(metallic, 0.0f, 1.0f);

	float3 F0 = g_TextureArray[2].Sample(g_SamAnisotropicWrap, TexC).rgb;
	F0 += g_FresnelR0;
	clamp(F0, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));

	// 粗糙度
	float roughness = g_TextureArray[4].Sample(g_SamAnisotropicWrap, TexC).r;
	roughness += g_Roughness;
	clamp(roughness, 0.0f, 1.0f);

	float3 N = pin.NormalW;
	if (g_UseNormalTexture)
	{
		float3 TN = g_TextureArray[1].Sample(g_SamAnisotropicWrap, TexC).rgb;
		N = NormalSampleToWorldSpace(TN, pin.NormalW, pin.TangentW, pin.BitangentW);
	}

	// Light terms.

	Material mat =
	{
		diffuseAlbedo,
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
	// 将它们组合在一起，以获得IBL镜面部分
	float4 specular_ab = prefilteredColor * (1.0f - roughness) * metallic;


	float shadowFactor = 0.0f;
	float3 directLight = float3(0.0f, 0.0f, 0.0f);
	for (uint i = 0; i < g_LightConst; i++)
	{
		shadowFactor = CalcShadowFactor(mul(pin.PosW, g_ShadowTransform[i]));
		directLight += ComputeLighting(g_Lights[i], mat, pin.PosW.xyz,
			N, V) * shadowFactor;
	}
	
	// 色彩要乘上环境光强度
	diffuseAlbedo.rgb *= g_AmbientColor.rgb * (1.0f - g_AmbientColor.a);
	// 乘上环境光强度
	specular_ab.rgb *= g_AmbientColor.rgb * (1.0f - g_AmbientColor.a);
	
	// 生成投影纹理坐标，将AO贴图投影到场景中。
	float4 aoPosH = mul(pin.PosW, g_ViewProjTex);
	aoPosH/= aoPosH.w;
	// 采样SSAO贴图。
	float ambientAccess = g_AOMap.Sample(g_SamAnisotropicClamp, aoPosH.xy, 0.0f).r;

	diffuseAlbedo *= ambientAccess;
	
	// HDR tonemapping
	//specular_ab.rgb = specular_ab.rgb / (specular_ab.rgb + float3(1.2f, 1.2f, 1.2f));
	// gamma correction
	//specular_ab = pow(specular_ab, (1.0f / 2.0f));
	float4 litColor = diffuseAlbedo + float4(directLight, 1.0f) + specular_ab;

	// 从漫反射材质中获取透明度。
	litColor.a = diffuseAlbedo.a;

	return litColor;
}
