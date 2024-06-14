// Include common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
	float4 Color : COLOR;
	float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
	float3 TangentU : TANGENT;
	float3 BitangentU : BINORMAL;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float4 Color : COLOR;
	//float4 ShadowPosH : POSITION0;
   // float4 SsaoPosH   : POSITION1;
	float3 PosW    : POSITION2;
	float3 NormalW : NORMAL;
	float2 TexC : TEXCOORD;
	float3 TangentW : TANGENT;
	float3 BitangentW : BINORMAL;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut) 0.0f;
	
	// Fetch the material data.
	MaterialData matData = gMaterialData[gMaterialIndex];

	// Transform to world space.
	float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
	vout.PosW = posW.xyz;

	vout.Color = vin.Color;

	// Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix.
	vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);

	vout.TangentW = mul(vin.TangentU, (float3x3) gWorld);
	vout.BitangentW = mul(vin.BitangentU, (float3x3) gWorld);

	// Transform to homogeneous clip space.
	vout.PosH = mul(posW, gViewProj);
	
	// Output vertex attributes for interpolation across triangle.
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;
	
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// Interpolating normal can unnormalize it, so renormalize it.
	pin.NormalW = normalize(pin.NormalW);

	// 找到对应的材质数据.
	MaterialData matData = gMaterialData[gMaterialIndex];
		
	// -------------------------------------------------------------------------
	float4 diffuseAlbedo = gTextureMaps[matData.DiffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
	diffuseAlbedo.rgb += pin.Color.rgb * (1.0f - pin.Color.a);
	
	// 法线
	float4 normalMapSample = gTextureMaps[matData.NormalMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
	// Uncomment to turn off normal mapping.
	float3 bumpedNormalW = pin.NormalW;
	bumpedNormalW = NormalSampleToWorldSpace(normalMapSample.rgb, pin.NormalW, pin.TangentW, pin.BitangentW);

	// 金属度
	float metallic = matData.Metallic;
	if (matData.MetallicMapIndex != 0)
		metallic = gTextureMaps[matData.MetallicMapIndex].Sample(gsamAnisotropicWrap, pin.TexC).r;
	
	// FR0
	float3 fresnelR0 = matData.FresnelR0;
	fresnelR0 += lerp(fresnelR0, diffuseAlbedo.rgb, metallic).rgb;
	if (matData.FresnelR0MapIndex != 0)
		fresnelR0 = gTextureMaps[matData.FresnelR0MapIndex].Sample(gsamAnisotropicWrap, pin.TexC).rgb;
	
	// 粗糙度
	float roughness = matData.Roughness;
	if (matData.RoughnessMapIndex != 0)
		roughness = gTextureMaps[matData.RoughnessMapIndex].Sample(gsamAnisotropicWrap, pin.TexC).r;

	// 不透明度
	float fragOpacity = gOpacity;
	// -------------------------------------------------------------------------

	float3 luminescence = { 0.0f, 0.0f, 0.0f };
	
	// 动态查找数组中的纹理。
	//diffuseAlbedo *= gTextureMaps[matData.DiffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);

	// Vector from point being lit to eye. 
	float3 toEyeW = normalize(gEyePosW - pin.PosW);

	// Light terms.
	float4 ambient = gAmbientLight * diffuseAlbedo;
	// 光泽度s
	const float shininess = (1.0f - roughness) * normalMapSample.a;
	
	Material mat =
	{
		diffuseAlbedo,
		metallic,
		roughness,
		fresnelR0,
		luminescence,
		shininess
	};
	
	float3 shadowFactor = 1.0f;
	float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
		bumpedNormalW, toEyeW, shadowFactor);

	float4 litColor = ambient + directLight + float4(luminescence, 0.0f);

	// 添加镜面反射（高光）。
	float3 r = reflect(-toEyeW, bumpedNormalW);
	float4 reflectionColor = gTextureMaps[matData.SkyMapIndex].Sample(gsamLinearWrap, matData.SkyTexC);
	// 菲涅尔系数
	float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpedNormalW, r);
	// 最终混合方式
	litColor.rgb += litColor.rgb * shininess * fresnelFactor * reflectionColor.rgb;
	
	// Common convention to take alpha from diffuse albedo.
	litColor.a = diffuseAlbedo.a;
	
 //   float3 lightVec = normalize(pin.PosW - gEyePosW);
 //   float lightFactor = 0.0f;
 //   lightFactor += litColor;
 //   float diffuse = clamp(dot(-lightVec, pin.NormalW), 0.0f, 1.0f);
 //   float3 cameraDir = normalize(gEyePosW - pin.PosH.xyz);
 //   float3 specularDir = lightVec - 2 * dot(lightVec, pin.NormalW) * pin.NormalW;
	//float specular = clamp(dot(specularDir, cameraDir), 0.0f, 1.0f);
	//specular = pow(specular, 8);
	//litColor = clamp(litColor * (lightFactor * (0.8 * diffuse + 5.0f * specular)), 0.0f, 1.0f);
	return litColor;
}


