// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
	#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
	#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
	#define NUM_SPOT_LIGHTS 0
#endif

#define MaxLights 16

struct LightData
{
	float3 Strength;
	float FalloffStart; // point/spot light only
	float3 Direction; // directional/spot light only
	float FalloffEnd; // point/spot light only
	float3 Position; // point light only
	float SpotPower; // spot light only
};

struct Material
{
	float4 DiffuseAlbedo;
	float Metallic; //金属度
	float Roughness; //粗糙度
	float3 FresnelR0; //菲涅耳R0
	float3 Luminescence; //自发光
	float Shininess; //光泽度
};

float CalcAttenuation(float d)
{
	// Quadratic falloff
	float dSat = max(d, 0.01f);
	return 1 / (dSat * dSat);
}

// Schlick gives an approximation to Fresnel reflectance (see pg. 233 "Real-Time Rendering 3rd Ed.").
// R0 = ( (n-1)/(n+1) )^2, where n is the index of refraction.
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
	float cosIncidentAngle = saturate(dot(normal, lightVec));

	float f0 = 1.0f - cosIncidentAngle;
	float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);

	return reflectPercent;
}

// Schlick gives an approximation to Fresnel reflectance
float3 FresnelSchlick(float3 halfVector, float3 toEye, float3 FR0)
{
	float cosTheta = saturate(dot(halfVector, toEye));
	return FR0 + (1.0f - FR0) * pow(1.0f - cosTheta, 5.0);
}

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
	roughness = max(roughness, 0.05f);
	float a = roughness * roughness;
	float aSqr = a * a;
	float NdotH = max(dot(normal, halfVector), 0.0f);
	float NdotHSqr = NdotH * NdotH;

	float nom = aSqr;
	float denom = (NdotHSqr * (aSqr - 1.0f) + 1.0f);
	denom = 3.14159265359 * denom * denom;

	return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0f);
	float k = (r * r) / 8.0f;
	
	float nom = NdotV;
	float denom = NdotV * (1.0f - k) + k;

	return nom / denom;
}

float GeometrySmith(float3 normal, float3 toEye, float3 lightVec, float roughness)
{
	float NdotV = max(dot(normal, toEye), 0.0f);
	float NdotL = max(dot(normal, lightVec), 0.0f);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

float3 BRDFCookTorrance(Material mat, float3 radiance, float3 normal, float3 toEye, float3 lightVec, float3 halfVector)
{
	float roughness = mat.Roughness;
	float3 FR0 = mat.FresnelR0;

	float NDF = DistributionGGX(normal, halfVector, roughness);
	float G = GeometrySmith(normal, toEye, lightVec, roughness);
	float3 F = FresnelSchlick(halfVector, toEye, FR0);

	float3 nom = NDF * G * F;
	float denom = 4.0f * max(dot(normal, toEye), 0.0f) * max(dot(normal, lightVec), 0.0f) + 0.001f;
	float3 specular = nom / denom;

	float3 kS = F;
	float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
	kD *= 1.0f - mat.Metallic;

	float NdotL = max(dot(normal, lightVec), 0.0f);
	return (kD * mat.DiffuseAlbedo.rgb / 3.14159265359 + specular) * radiance * NdotL;
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for directional lights.
//---------------------------------------------------------------------------------------
float3 ComputeDirectionalLight(LightData light, Material mat, float3 normal, float3 toEye)
{
	// 计算每个光源的辐射率
	// 从表面到光源的矢量。
	float3 lightVec = -light.Direction;
	// half vector
	float3 halfVector = normalize(toEye + lightVec);
	float3 radiance = light.Strength;

	return BRDFCookTorrance(mat, radiance, normal, toEye, lightVec, halfVector);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for point lights.
//---------------------------------------------------------------------------------------
float3 ComputePointLight(LightData light, Material mat, float3 pos, float3 normal, float3 toEye)
{
	// The vector from the surface to the light.
	float3 lightVec = light.Position - pos;

	// The distance from surface to light.
	float d = length(lightVec);

	// Range test.
	if (d > light.FalloffEnd)
		return 0.0f;

	// Normalize the light vector.
	lightVec /= d;

	// half vector
	float3 halfVector = normalize(toEye + lightVec);

	// Attenuate light by distance.
	float attenuation = CalcAttenuation(d);
	float3 radiance = light.Strength * attenuation;

	return BRDFCookTorrance(mat, radiance, normal, toEye, lightVec, halfVector);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for spot lights.
//---------------------------------------------------------------------------------------
float3 ComputeSpotLight(LightData light, Material mat, float3 pos, float3 normal, float3 toEye)
{
	// The vector from the surface to the light.
	float3 lightVec = light.Position - pos;

	// The distance from surface to light.
	float d = length(lightVec);

	// Range test.
	if (d > light.FalloffEnd)
		return 0.0f;

	// Normalize the light vector.
	lightVec /= d;

	// half vector
	float3 halfVector = normalize(toEye + lightVec);

	// Attenuate light by distance.
	float attenuation = CalcAttenuation(d);

	// Attenuate light by angle
	attenuation *= pow(max(dot(-lightVec, light.Direction), 0.0f), light.SpotPower);
	float3 radiance = light.Strength * attenuation;

	return BRDFCookTorrance(mat, radiance, normal, toEye, lightVec, halfVector);
}

float4 ComputeLighting(LightData gLights[MaxLights], Material mat,
					   float3 pos, float3 normal, float3 toEye,
					   float3 shadowFactor)
{
	float3 result = 0.0f;

	int i = 0;

#if (NUM_DIR_LIGHTS > 0)
	for(i = 0; i < NUM_DIR_LIGHTS; ++i)
	{
		result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
	}
#endif

#if (NUM_POINT_LIGHTS > 0)
	for(i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; ++i)
	{
		result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
	}
#endif

#if (NUM_SPOT_LIGHTS > 0)
	for(i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
	{
		result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
	}
#endif 

	return float4(result, 0.0f);
}
