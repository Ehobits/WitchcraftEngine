#include "LightingUtil.hlsl"

cbuffer cbPerObject : register(b0)
{
	float4x4 g_WorldTransform;
	float4x4 g_TexTransform;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD0;
	nointerpolation uint LightIndex : TEXCOORD1;
};

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

// 当前体积光 pass 采用“每灯全屏积分”的简单模型。
// 方向光走固定深度段；
// 点光使用收紧后的球形体积；
// 聚光在球形 broad phase 之后，再用有限锥体做二次裁剪，避免屏幕上出现“大圆盘”感。
static const float kVolumeRadiusBase = 3.5f;
static const float kVolumeFalloff = 2.0f;
static const float kVolumeMediumDensity = 0.08f;
static const float kVolumeSpotFocus = 8.0f;

// 复用主根签名 slot7(t269)：这里绑定 AO 子系统提供的主深度 SRV。
Texture2D<float> g_OpaqueDepthMap : register(t269);
SamplerState g_SamLinearClamp : register(s3);

VertexOut VS(uint vertexID : SV_VertexID)
{
	VertexOut vout;
	float2 texC;
	if (vertexID == 0) texC = float2(0.0f, 1.0f);
	else if (vertexID == 1) texC = float2(0.0f, 0.0f);
	else if (vertexID == 2) texC = float2(1.0f, 0.0f);
	else if (vertexID == 3) texC = float2(0.0f, 1.0f);
	else if (vertexID == 4) texC = float2(1.0f, 0.0f);
	else texC = float2(1.0f, 1.0f);
	vout.TexC = texC;
	vout.PosH = float4(texC.x * 2.0f - 1.0f, 1.0f - texC.y * 2.0f, 0.0f, 1.0f);
	// 体积光逐灯 draw 的 light index 通过 ObjectCB.TexTransform._11 显式传入，
	// 不依赖 SV_InstanceID，避免 StartInstanceLocation 与 shader 侧索引语义不一致。
	vout.LightIndex = (uint)max(g_TexTransform._11 + 0.5f, 0.0f);
	return vout;
}

bool RaySphereIntersect(float3 rayOriginW, float3 rayDirW, float3 sphereCenterW, float sphereRadius, out float tNear, out float tFar)
{
	float3 oc = rayOriginW - sphereCenterW;
	float b = dot(oc, rayDirW);
	float c = dot(oc, oc) - sphereRadius * sphereRadius;
	float h = b * b - c;
	if (h < 0.0f)
	{
		tNear = 0.0f;
		tFar = 0.0f;
		return false;
	}

	float sqrtH = sqrt(h);
	tNear = -b - sqrtH;
	tFar = -b + sqrtH;
	return tFar > 0.0f;
}

float3 ReconstructWorldPos(float2 ndcXY, float depth01)
{
	float4 posH = float4(ndcXY, depth01, 1.0f);
	float4 posW = mul(posH, g_InvViewProj);
	return posW.xyz / max(posW.w, 1e-6f);
}

float3 SafeNormalize(float3 v)
{
	float lenSq = dot(v, v);
	if (lenSq > 1e-8f)
		return v * rsqrt(lenSq);
	return float3(0.0f, -1.0f, 0.0f);
}

float4 PS(VertexOut pin) : SV_Target
{
	if (pin.LightIndex >= g_LightConst || pin.LightIndex >= 256u)
		return float4(0.0f, 0.0f, 0.0f, 0.0f);

	Light light = g_Lights[pin.LightIndex];
	const bool isDirectional = (light.Type == DIRCTON_LIT);
	const bool isPoint = (light.Type == CONCENT_LIT);
	const bool isSpot = (light.Type == SPORT_LIT);
	if (light.VolumetricEnable <= 0.5f || (!isDirectional && !isPoint && !isSpot))
		return float4(0.0f, 0.0f, 0.0f, 0.0f);

	const float attenuationDistance = max(light.VolumetricAttenuationDistance, 0.1f);
	const float baseRadius = max(kVolumeRadiusBase, 0.05f);
	const float powerFactor = sqrt(max(light.Power, 0.0f));
	float volumeRadius = attenuationDistance;
	float3 volumeCenterW = light.Position;
	if (isPoint)
	{
		// 点光体积半径不要直接吃满衰减距离，否则很容易在场景表面形成明显大圆盘。
		volumeRadius = max(baseRadius * (1.0f + powerFactor * 0.25f), attenuationDistance * 0.35f);
	}
	else if (isSpot)
	{
		const float3 spotDirectionForBounds = SafeNormalize(light.Direction);
		// 聚光 broad phase 先包住主要锥体，但不要无限贴近整段衰减距离。
		volumeCenterW = light.Position + spotDirectionForBounds * (attenuationDistance * 0.35f);
		volumeRadius = max(baseRadius, attenuationDistance * 0.45f);
	}

	const float2 uv = pin.TexC;
	const float2 ndcXY = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
	const float3 rayOriginW = g_CameraPosW;
	const float3 farPosW = ReconstructWorldPos(ndcXY, 1.0f);
	const float3 rayDirW = normalize(farPosW - rayOriginW);

	// 用主场景深度裁掉空气积分段，避免体积光穿过前景表面继续累积。
	float tScene = 0.0f;
	bool hasSceneDepth = false;
	const float sceneDepth = g_OpaqueDepthMap.SampleLevel(g_SamLinearClamp, uv, 0.0f);
	if (sceneDepth < 0.99999f)
	{
		const float3 scenePosW = ReconstructWorldPos(ndcXY, sceneDepth);
		tScene = dot(scenePosW - rayOriginW, rayDirW);
		hasSceneDepth = (tScene > 0.0f);
	}

	float tStart = 0.0f;
	float tEnd = 0.0f;
	if (isDirectional)
	{
		tEnd = hasSceneDepth ? min(tScene, attenuationDistance) : attenuationDistance;
	}
	else
	{
		float tNear = 0.0f;
		float tFar = 0.0f;
		if (!RaySphereIntersect(rayOriginW, rayDirW, volumeCenterW, volumeRadius, tNear, tFar))
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		tStart = max(tNear, 0.0f);
		tEnd = tFar;
		if (hasSceneDepth)
			tEnd = min(tEnd, tScene);
	}

	if (tEnd <= tStart + 1e-4f)
		return float4(0.0f, 0.0f, 0.0f, 0.0f);

	const int kStepCount = isDirectional ? 24 : 32;
	const float stepLength = (tEnd - tStart) / (float)kStepCount;
	const float falloff = max(kVolumeFalloff, 0.05f);
	const float spotFocus = max(kVolumeSpotFocus, 1.0f);
	const float mediumDensity = max(kVolumeMediumDensity, 0.001f);
	const float3 directionalIncoming = SafeNormalize(-light.Direction);
	const float3 spotDirection = SafeNormalize(light.Direction);
	const float spotOuterCos = saturate(light.SpotOuterCos);
	const float spotInnerCos = saturate(max(light.SpotInnerCos, spotOuterCos + 1e-4f));
	const float spotOuterSin = sqrt(saturate(1.0f - spotOuterCos * spotOuterCos));
	const float spotOuterTan = spotOuterSin / max(spotOuterCos, 1e-3f);

	float accumulation = 0.0f;
	float transmittance = 1.0f;
	const float jitter = frac(sin(dot(pin.PosH.xy, float2(12.9898f, 78.233f))) * 43758.5453f);
	[loop]
	for (int i = 0; i < kStepCount; ++i)
	{
		const float t = tStart + ((float)i + jitter) * stepLength;
		const float3 samplePosW = rayOriginW + rayDirW * t;

		float radialAttenuation = 1.0f;
		float angularAttenuation = 1.0f;
		float3 incomingLightDir = directionalIncoming;

		if (!isDirectional)
		{
			const float3 offset = samplePosW - volumeCenterW;
			const float distToLight = length(offset);
			const float safeDist = max(distToLight, 1e-4f);
			const float normalizedDistance = saturate(distToLight / volumeRadius);
			radialAttenuation = pow(saturate(1.0f - normalizedDistance), 3.0f);
			radialAttenuation *= exp(-distToLight / max(attenuationDistance * 0.6f, 0.1f));

			if (isSpot)
			{
				const float3 coneOffset = samplePosW - light.Position;
				const float axialDistance = dot(coneOffset, spotDirection);
				if (axialDistance <= 0.0f || axialDistance >= attenuationDistance)
					continue;

				const float3 radialOffset = coneOffset - spotDirection * axialDistance;
				const float radialDistance = length(radialOffset);
				const float coneRadius = max(axialDistance * spotOuterTan, 1e-3f);
				if (radialDistance >= coneRadius)
					continue;

				const float coneShell = saturate(1.0f - radialDistance / coneRadius);
				const float coneDepthFade = saturate(1.0f - axialDistance / attenuationDistance);
				radialAttenuation *= coneShell * coneShell * (0.35f + 0.65f * coneDepthFade);

				const float3 lightToSample = normalize(coneOffset);
				const float coneCos = saturate(dot(lightToSample, spotDirection));
				const float coneAttenuation = saturate((coneCos - spotOuterCos) / max(spotInnerCos - spotOuterCos, 1e-4f));
				angularAttenuation = pow(coneAttenuation, spotFocus);
				if (angularAttenuation <= 1e-4f)
					continue;
			}
			else if (isPoint)
			{
				// 点光进一步压缩远离光心的空气积分，减少表面的“大圈”感。
				radialAttenuation *= radialAttenuation;
			}

			incomingLightDir = (volumeCenterW - samplePosW) / safeDist;
		}

		const float3 viewDir = normalize(g_CameraPosW - samplePosW);
		const float phase = 0.25f + 0.75f * pow(saturate(dot(incomingLightDir, viewDir)), 2.0f);
		float localDensity = mediumDensity * radialAttenuation * angularAttenuation;
		if (isDirectional)
			localDensity *= 0.7f;

		const float scatter = localDensity * phase;
		accumulation += transmittance * scatter * stepLength;
		transmittance *= exp(-localDensity * stepLength * 1.25f);
	}

	float distanceFade = 1.0f;
	if (isDirectional)
		distanceFade = exp(-((tStart + tEnd) * 0.5f) / attenuationDistance);

	const float intensity = max(light.VolumetricIntensity, 0.0f) * max(light.Power, 0.0f);
	const float3 volumetricColor = light.Color * intensity * accumulation * distanceFade;
	return float4(max(volumetricColor, 0.0f), 1.0f);
}
