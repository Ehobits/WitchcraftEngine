//***************************************************************************************
// LightingUtil.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Contains API for shader lighting.
//***************************************************************************************

static const float PI = 3.14159265359f;
static const float TwoPI = 6.28318530718f;
static const float INV_PI = 1.0f / PI;
static const float INV_TWO_PI = 1.0f / TwoPI;

#define DIRCTON_LIT 0.0f
#define CONCENT_LIT 1.0f
#define SPORT_LIT	2.0f

struct Light
{
	float3 Color;
	float Type;
	float3 Position;
	float __pad002;
	float3 Direction;
	float Power;
};

struct Material
{
	float4 DiffuseAlbedo;
	float Metallic;
	float3 FresnelR0;
	float Roughness;
	float3 Transmission;
	float3 Emissive;
	float __pad000;
	float __pad001;
	float ClearCoatThickness;
	float ClearCoatRoughness;
	float Anisotropy;
	float AnisotropyRotation;
};

float CalcAttenuation(float d)
{
	// Quadratic falloff
	float dSat = max(d, 0.01f);
	return 1 / (dSat*dSat);
}

// Schlick给出了菲涅耳反射率的近似值
float3 FresnelSchlick(float3 H, float3 V, float3 F0)
{
	float cosTheta = saturate(dot(H, V));
	return F0 + (1.0f-F0) * pow(1.0f - cosTheta, 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
	float rp = roughness;
	roughness = max(roughness, 0.025f);
	float aSqr = roughness * roughness;
	float NdotH = max(dot(N,H), 0.0f);
	float NdotHSqr = NdotH*NdotH;

	float nom = aSqr;
	float denom = (NdotHSqr * (aSqr - 1.0f) + 1.0f) - 0.025f;
	denom = PI * denom * denom;

	return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0f);
	float k = (r*r) / 8.0f;
	
	float nom = NdotV;
	float denom = NdotV * (1.0f - k) + k;	

	return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0f);
	float NdotL = max(dot(N, L), 0.0f);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

float3 BRDFCookTorrance(Material mat, float3 radiance, float3 N, float3 V, float3 L, float3 H)
{
	float roughness = mat.Roughness;
	float3 F0 = mat.FresnelR0;

	float NDF = DistributionGGX(N, H, roughness);
	float G   = GeometrySmith(N, V, L, roughness);
	float3 F  = FresnelSchlick(H, V, F0); 

	float3 nom = NDF * G * F;
	float denom = 4.0f * max(dot(N,V), 0.0f) * max(dot(N,L) , 0.0f) + 0.0001f;
	float3 specular = nom / denom;

	float3 kS = F;
	float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
	kD *= 1.0f - mat.Metallic;

	float NdotL = dot(N,L);
	NdotL = max(NdotL, 0.0f);
	return (kD * mat.DiffuseAlbedo.rgb / PI + specular) * radiance * NdotL;
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for directional lights.
//---------------------------------------------------------------------------------------
float3 ComputeDirectionalLight(Light light, Material mat, float3 N, float3 V)
{
	// Calculate per-light radiance
	// The vector from the surface to the light.
	float3 L = -light.Direction;
	// half vector
	float3 H = normalize(V + L);
	float3 radiance = light.Color * light.Power;

	return BRDFCookTorrance(mat, radiance, N, V, L, H);	
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for point lights.
//---------------------------------------------------------------------------------------
float3 ComputePointLight(Light light, Material mat, float3 pos, float3 N, float3 V)
{
	// Calculate per-light radiance
	// The vector from the surface to the light.
	float3 L = light.Position - pos;
	float d = length(L);
	// Range test.
	if(d > 100.0f) // Implicit falloff of 100.0f for all lights
		return 0.0f;
	// Normalize the light vector.
	L /= d;
	// half vector
	float3 H = normalize(V + L);
	// Attenuate light by distance.
	float attenuation = CalcAttenuation(d);
	float3 radiance = light.Color * light.Power * attenuation;

	return BRDFCookTorrance(mat, radiance, N, V, L, H);	
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for spot lights.
//---------------------------------------------------------------------------------------
float3 ComputeSpotLight(Light light, Material mat, float3 pos, float3 N, float3 V)
{
	// Calculate per-light radiance
	// The vector from the surface to the light.
	float3 L = light.Position - pos;
	float d = length(L);
	// Range test.
	if(d > 100.0f) // Implicit falloff of 100.0f for all lights
		return 0.0f;
	// Normalize the light vector.
	L /= d;
	// half vector
	float3 H = normalize(V + L);
	// 按距离衰减光线。
	float attenuation = CalcAttenuation(d);
	// 按角度衰减光线
	attenuation *= pow(max(dot(-L, light.Direction), 0.0f), 1.4f);
	float3 radiance = light.Color * light.Power * attenuation;

	return BRDFCookTorrance(mat, radiance, N, V, L, H);	
}


float3 ComputeLighting(Light gLights, Material mat,
					   float3 pos, float3 N, float3 V)
{
	float3 result = 0.0f;

	if (gLights.Type == DIRCTON_LIT)
		result = ComputeDirectionalLight(gLights, mat, N, V);
	if (gLights.Type == CONCENT_LIT)
		result = ComputePointLight(gLights, mat, pos, N, V);
	if (gLights.Type == SPORT_LIT)
		result = ComputeSpotLight(gLights, mat, pos, N, V);
	
	return result;
}

// Transforms a normal map sample to world space
float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 N, float3 T, float3 B)
{
	// 将每个分量从[0,1]区间解压到[-1,1]区间。
	float3 normalT = 2.0f * normalMapSample - 1.0f;

	float3 n = normalize(N);
	float3 t = T - dot(T, n) * n;
	if (dot(t, t) < 1e-6f)
	{
		float3 fallbackAxis = abs(n.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
		t = cross(fallbackAxis, n);
	}
	t = normalize(t);

	float handedness = dot(cross(n, t), B) < 0.0f ? -1.0f : 1.0f;
	float3 b = handedness * normalize(cross(n, t));
	float3x3 TBN = float3x3(t, b, n);

	// Transform from tangent space to world space.
	float3 bumpedNormalW = normalize(mul(normalT, TBN));

	return bumpedNormalW;
}
