#include "SsaoCommon.hlsli"

Texture2D gRandomVecMap : register(t2);

static const int gSampleCount = 14;

float3 Hash33(float3 p3)
{
	p3 = frac(p3 * 0.1031f);
	p3 += dot(p3, p3.yxz + 33.33f);
	return frac((p3.xxy + p3.yxx) * p3.zyx);
}

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float3 PosV : POSITION;
	float2 TexC : TEXCOORD0;
};

VertexOut VS(uint vid : SV_VertexID)
{
	VertexOut vout;

	vout.TexC = BuildFullscreenQuadTexCoord(vid);
	vout.PosH = BuildFullscreenQuadPositionH(vout.TexC);

	float4 ph = mul(vout.PosH, gInvProj);
	vout.PosV = ph.xyz / ph.w;

	return vout;
}

float OcclusionFunction(float distZ)
{
	float occlusion = 0.0f;
	if (distZ > gSurfaceEpsilon)
	{
		const float fadeLength = max(gOcclusionFadeEnd - gOcclusionFadeStart, 1e-4f);
		occlusion = saturate((gOcclusionFadeEnd - distZ) / fadeLength);
	}

	return occlusion;
}

float4 PS(VertexOut pin) : SV_Target
{
	float3 n = normalize(gNormalMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz);
	const float depthNdc = gDepthMap.SampleLevel(gsamDepthMap, pin.TexC, 0.0f).r;
	if (depthNdc >= 0.999999f)
		return 1.0f;

	const float3 p = ReconstructViewPosition(pin.TexC, depthNdc);
	const float3 screenRand = 2.0f * gRandomVecMap.SampleLevel(gsamLinearWrap, 6.0f * pin.TexC, 0.0f).rgb - 1.0f;
	// 先回退到纯屏幕空间随机向量。
	// 之前混入世界空间连续 hash 虽然提升了“世界稳定性”，
	// 但在大面积平面上会形成持续可见的细碎噪点。
	// 这里优先保证 AO 观感干净，再考虑后续是否做更温和的稳定化策略。
	const float3 randVec = normalize(screenRand);

	float occlusionSum = 0.0f;
	float validSampleCount = 0.0f;

	[unroll]
	for (int i = 0; i < gSampleCount; ++i)
	{
		float3 offset = reflect(gOffsetVectors[i].xyz, randVec);
		float flip = sign(dot(offset, n));
		float3 q = p + flip * gOcclusionRadius * offset;

		const float2 sampleTex = ProjectViewPosToTexCoord(q);
		if (sampleTex.x < 0.0f || sampleTex.x > 1.0f || sampleTex.y < 0.0f || sampleTex.y > 1.0f)
			continue;

		const float rzNdc = gDepthMap.SampleLevel(gsamDepthMap, sampleTex, 0.0f).r;
		if (rzNdc >= 0.999999f)
			continue;

		const float3 r = ReconstructViewPosition(sampleTex, rzNdc);
		const float3 hitVector = r - p;
		const float hitLengthSq = dot(hitVector, hitVector);
		if (hitLengthSq <= 1e-6f)
			continue;

		float distZ = p.z - r.z;
		float dp = max(dot(n, hitVector * rsqrt(hitLengthSq)), 0.0f);
		occlusionSum += dp * OcclusionFunction(distZ);
		validSampleCount += 1.0f;
	}

	if (validSampleCount > 0.0f)
		occlusionSum /= validSampleCount;

	const float access = saturate(1.0f - occlusionSum * 1.35f);
	return saturate(pow(access, 2.2f));
}
