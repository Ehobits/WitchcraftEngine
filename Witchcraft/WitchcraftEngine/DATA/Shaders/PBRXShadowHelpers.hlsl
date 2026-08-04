#include "Core.hlsl"
// PBRX 阴影辅助函数（从 PBRX.hlsl 拆分）
// 说明：
// - 方向光 CSM 由 CPU 侧 frustum-slice split 生成，这里使用 view-space depth 选择级联；
// - 默认方向光过滤为 9-tap PCF，近级联清晰度主要依赖更高的 world-texel 密度；
// - 级联边界仍保留窄带平滑混合，避免硬切线；
// - 不使用强修补策略（不做 seamFix，不做大范围亮向抬升）。
// 注意：DirectionalShadowMask.hlsl 维护了一份屏幕空间版本的 CSM 采样逻辑。
// 调整 bias/filter/seam blend 时应同步检查两边，除非确认主 PBR 不再使用该 mask 路径。

static const uint PBRX_MAX_DIRECTIONAL_SHADOW_MAP_COUNT = G_MAX_DIRECTIONAL_SHADOW_MAP_COUNT;
static const uint PBRX_MAX_SPOT_SHADOW_MAP_COUNT = G_MAX_SPOT_SHADOW_MAP_COUNT;

float GetDirectionalShadowCascadeSplit(uint index);
float GetDirectionalShadowCascadeWorldTexelSize(uint index);
float GetDirectionalShadowCascadeDepthScale(uint index);

// Shadow map 投影范围外按“全亮”处理。
// 这和 C++ 侧的 border-white sampler 语义一致，可避免 cascade 覆盖边缘被采成黑色阴影。
float SampleShadowCmpLitOutside(Texture2D shadowMap, float2 shadowUv, float depth)
{
	if (shadowUv.x < 0.0f || shadowUv.x > 1.0f ||
		shadowUv.y < 0.0f || shadowUv.y > 1.0f ||
		depth < 0.0f || depth > 1.0f)
	{
		return 1.0f;
	}

	return shadowMap.SampleCmpLevelZero(g_SamShadow, shadowUv, depth).r;
}

uint GetDirectionalShadowCascadeCount()
{
	return (uint) clamp(g_DirectionalShadowCascadeSettings.x, 1.0f, 4.0f);
}

float GetDirectionalCascadeNormalizedPosition(uint cascadeIndex)
{
	return saturate((float)cascadeIndex / max((float)GetDirectionalShadowCascadeCount() - 1.0f, 1.0f));
}

float GetDirectionalCascadeMiddleWeight(uint cascadeIndex)
{
	const float cascadePosition = GetDirectionalCascadeNormalizedPosition(cascadeIndex);
	return 1.0f - abs(cascadePosition * 2.0f - 1.0f);
}

float CalcShadowFactor(Texture2D shadowMap, float4 shadowPos, float depthBias)
{
	if (shadowPos.w <= 0.0f)
		return 1.0f;

	shadowPos.xyz /= shadowPos.w;

	float depth = shadowPos.z - max(depthBias, 0.0f);

	if (shadowPos.x < 0.0f || shadowPos.x > 1.0f ||
		shadowPos.y < 0.0f || shadowPos.y > 1.0f ||
		depth < 0.0f || depth > 1.0f)
	{
		return 1.0f;
	}

	uint width, height, numMips;
	shadowMap.GetDimensions(0, width, height, numMips);

	const float softness = saturate(max(g_ShadowSettings.y, 0.0f) * 0.25f);
	const float radiusInTexels = lerp(0.60f, 2.20f, pow(softness, 1.20f));
	float dx = (1.0f / (float) width) * radiusInTexels;

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
		percentLit += SampleShadowCmpLitOutside(
			shadowMap,
			shadowPos.xy + offsets[i],
			depth);
	}

	percentLit /= 9.0f;

	const float shadowOpacity = saturate(g_ShadowSettings.x);
	return lerp(1.0f - shadowOpacity, 1.0f, percentLit);
}

// 方向光级联阴影默认使用 9-tap PCF。
// 近级联的清晰度优先依赖更高 world-texel 密度，而不是继续堆大核滤波。
float CalcDirectionalShadowPercentLit(
	Texture2D shadowMap,
	float2 shadowUv,
	float depth,
	float nearHardBlend,
	float filterTexelSize,
	float radiusMultiplier)
{
	const float texelSize = max(filterTexelSize, 1e-6f);
	const float softness = saturate(max(g_ShadowSettings.y, 0.0f) * 0.02f);

	const float hardening = nearHardBlend * (1.0f - softness);
	const float radiusScale = lerp(0.90f, 0.52f, hardening);
	const float r1 = texelSize * lerp(0.50f, 1.35f, pow(softness, 1.05f)) * radiusScale * max(radiusMultiplier, 0.01f);

	const float centerLit = SampleShadowCmpLitOutside(shadowMap, shadowUv, depth);
	float percentLit = 0.0f;
	percentLit += centerLit * 0.28f;

	percentLit += SampleShadowCmpLitOutside(shadowMap, shadowUv + float2(+r1, 0.0f), depth) * 0.12f;
	percentLit += SampleShadowCmpLitOutside(shadowMap, shadowUv + float2(-r1, 0.0f), depth) * 0.12f;
	percentLit += SampleShadowCmpLitOutside(shadowMap, shadowUv + float2(0.0f, +r1), depth) * 0.12f;
	percentLit += SampleShadowCmpLitOutside(shadowMap, shadowUv + float2(0.0f, -r1), depth) * 0.12f;
	percentLit += SampleShadowCmpLitOutside(shadowMap, shadowUv + float2(+r1, +r1), depth) * 0.06f;
	percentLit += SampleShadowCmpLitOutside(shadowMap, shadowUv + float2(+r1, -r1), depth) * 0.06f;
	percentLit += SampleShadowCmpLitOutside(shadowMap, shadowUv + float2(-r1, +r1), depth) * 0.06f;
	percentLit += SampleShadowCmpLitOutside(shadowMap, shadowUv + float2(-r1, -r1), depth) * 0.06f;

	const float centerRecover = lerp(0.42f, 0.12f, softness) * nearHardBlend;
	return lerp(percentLit, centerLit, centerRecover);
}

float CalcDirectionalShadowFactor(
	Texture2D shadowMap,
	float4 shadowPos,
	float depthBias,
	float nearHardBlend,
	float filterTexelSize,
	float wideBlurBlend,
	float wideBlurRadiusScale)
{
	if (shadowPos.w <= 0.0f)
		return 1.0f;

	shadowPos.xyz /= shadowPos.w;
	float depth = shadowPos.z - max(depthBias, 0.0f);

	if (shadowPos.x < 0.0f || shadowPos.x > 1.0f ||
		shadowPos.y < 0.0f || shadowPos.y > 1.0f ||
		depth < 0.0f || depth > 1.0f)
	{
		return 1.0f;
	}

	float percentLit = CalcDirectionalShadowPercentLit(
		shadowMap,
		shadowPos.xy,
		depth,
		nearHardBlend,
		filterTexelSize,
		1.0f);

	const float blurBlend = saturate(wideBlurBlend);
	if (blurBlend > 0.0f)
	{
		const float widePercentLit = CalcDirectionalShadowPercentLit(
			shadowMap,
			shadowPos.xy,
			depth,
			nearHardBlend,
			filterTexelSize,
			wideBlurRadiusScale);
		percentLit = lerp(percentLit, widePercentLit, blurBlend);
	}

	const float shadowOpacity = saturate(g_ShadowSettings.x);
	return lerp(1.0f - shadowOpacity, 1.0f, percentLit);
}

// 点光源/聚光灯阴影的 receiver bias
// 透视投影的深度非线性分布需要特殊处理
float ComputePointLightReceiverBias(
	float3 normalW,
	float3 lightToPixelDir,
	float distanceToLight,
	float nearZ,
	float farZ)
{
	// 透视投影中，depth 非线性分布
	// bias 需要按距离缩放才能在 world space 一致
	const float ndotl = saturate(dot(normalize(normalW), -lightToPixelDir));
	const float slopeFactor = 1.0f - ndotl;

	// 激进版本：更高 bias 压住面边界接缝
	const float baseBias = 0.0028f;

	// 透视投影的深度压缩因子
	// 距离越远，同样的世界空间误差对应的 depth 值变化越小
	const float normalizedDistance = saturate(distanceToLight / max(farZ, 1.0f));
	const float distanceScale = 1.0f + normalizedDistance * 6.0f;

	// 坡度因子：掠射角时 bias 需要更大
	const float slopeScale = 1.0f + slopeFactor * slopeFactor * 5.0f;

	return baseBias * distanceScale * slopeScale;
}

float ComputeSpotLightReceiverBias(
	float3 normalW,
	float3 lightToPixelDir,
	float distanceToLight,
	float nearZ,
	float farZ)
{
	const float3 N = normalize(normalW);
	const float3 L = normalize(-lightToPixelDir);
	const float ndotl = saturate(dot(N, L));
	const float slopeFactor = 1.0f - ndotl;

	// Spot 使用 2D shadow map，不需要 point-cube 那种为了压接缝而放大的激进 bias。
	const float baseBias = 0.00018f;
	const float normalizedDistance = saturate(distanceToLight / max(farZ, 1.0f));
	const float distanceScale = 1.0f + normalizedDistance * 1.2f;
	const float slopeScale = 1.0f + slopeFactor * slopeFactor * 1.8f;
	return baseBias * distanceScale * slopeScale;
}

float ComputeDirectionalReceiverBias(float3 normalW, float3 lightDirW, uint cascadeIndex, float lodDistance, float maxShadowDistance)
{
	const float baseBias = max(g_DirectionalShadowCascadeSettings.z, 0.0f);
	const float3 N = normalize(normalW);
	const float3 L = normalize(-lightDirW);
	const float ndotl = saturate(dot(N, L));
	const float slopeFactor = 1.0f - ndotl;
	const float referenceDepthScale = max(GetDirectionalShadowCascadeDepthScale(0u), 1e-6f);
	const float cascadeDepthScale = max(GetDirectionalShadowCascadeDepthScale(min(cascadeIndex, 3u)), 1e-6f);
	const float worldStableBiasScale = max(cascadeDepthScale / referenceDepthScale, 1.26f);
	const float referenceWorldTexelSize = max(GetDirectionalShadowCascadeWorldTexelSize(0u), 1e-5f);
	const float cascadeWorldTexelSize = max(GetDirectionalShadowCascadeWorldTexelSize(min(cascadeIndex, 3u)), referenceWorldTexelSize);
	const float texelRatio = max(cascadeWorldTexelSize / referenceWorldTexelSize, 1.0f);
	const float coarseCascadeBiasScale = lerp(1.0f, 1.58f, saturate(log2(texelRatio) * 0.24f));
	const float middleCascadeBlend = GetDirectionalCascadeMiddleWeight(cascadeIndex);
	const float middleCascadeBiasScale = lerp(1.0f, 1.16f, middleCascadeBlend);

	// 各级联的 depth 映射范围不同，这里先把 bias 统一到接近同一世界尺度，
	// 再对低分辨率级联做温和的 texel-aware 放大，压住粗 texel 上更明显的暗斑。
	return baseBias * max(worldStableBiasScale, coarseCascadeBiasScale) * middleCascadeBiasScale * (1.14f + slopeFactor * 1.75f);
}

float ComputePerspectiveDepthFromViewZ(float viewZ, float nearZ, float farZ)
{
	viewZ = max(viewZ, nearZ + 1e-5f);
	nearZ = max(nearZ, 1e-5f);
	farZ = max(farZ, nearZ + 1e-5f);

	// 对应 XMMatrixPerspectiveFovLH 的 D3D 深度映射：
	// depth = far/(far-near) - far*near/((far-near)*viewZ)
	const float a = farZ / (farZ - nearZ);
	const float b = (farZ * nearZ) / (farZ - nearZ);
	return saturate(a - b / viewZ);
}

float ComputePointLightCubeCompareDepth(float3 lightToPixel, float nearZ, float farZ)
{
	// 当前点光阴影是“把 6 个 90° 透视投影面渲染进 TextureCube”。
	// 所以比较值不能用线性 distance/farPlane，必须使用“被选中 cube 面”的透视投影深度。
	const float3 absLightToPixel = abs(lightToPixel);
	const float faceViewZ = max(absLightToPixel.x, max(absLightToPixel.y, absLightToPixel.z));
	return ComputePerspectiveDepthFromViewZ(faceViewZ, nearZ, farZ);
}

uint SelectPointLightShadowFace(float3 lightToPixel)
{
	float3 absDir = abs(lightToPixel);

	if (absDir.x >= absDir.y && absDir.x >= absDir.z)
		return lightToPixel.x >= 0.0f ? 0u : 1u;
	if (absDir.y >= absDir.x && absDir.y >= absDir.z)
		return lightToPixel.y >= 0.0f ? 2u : 3u;
	return lightToPixel.z >= 0.0f ? 4u : 5u;
}

float GetDirectionalShadowCascadeSplit(uint index)
{
	if (index == 0u)
		return g_DirectionalShadowCascadeSplits.x;
	if (index == 1u)
		return g_DirectionalShadowCascadeSplits.y;
	if (index == 2u)
		return g_DirectionalShadowCascadeSplits.z;
	return g_DirectionalShadowCascadeSplits.w;
}

float GetDirectionalShadowCascadeWorldTexelSize(uint index)
{
	if (index == 0u)
		return g_DirectionalShadowCascadeWorldTexelSize.x;
	if (index == 1u)
		return g_DirectionalShadowCascadeWorldTexelSize.y;
	if (index == 2u)
		return g_DirectionalShadowCascadeWorldTexelSize.z;
	return g_DirectionalShadowCascadeWorldTexelSize.w;
}

float GetDirectionalShadowCascadeDepthScale(uint index)
{
	if (index == 0u)
		return g_DirectionalShadowCascadeDepthScale.x;
	if (index == 1u)
		return g_DirectionalShadowCascadeDepthScale.y;
	if (index == 2u)
		return g_DirectionalShadowCascadeDepthScale.z;
	return g_DirectionalShadowCascadeDepthScale.w;
}

// 标准 CSM 使用相机 view-space 深度选择级联。
// CPU 侧每个 cascade 也是按同一组 view-space split 构建 frustum slice。
float GetDirectionalShadowLodDistance(float3 posW)
{
	return max(mul(float4(posW, 1.0f), g_View).z, 0.0f);
}

float ComputeDirectionalShadowFilterTexelSize(uint cascadeIndex, uint shadowIndex, float desiredWorldTexelSize)
{
	uint shadowWidth, shadowHeight, shadowNumMips;
	g_DirectionalShadowMap[shadowIndex].GetDimensions(0, shadowWidth, shadowHeight, shadowNumMips);

	const float cascadeWorldTexelSize =
		max(GetDirectionalShadowCascadeWorldTexelSize(min(cascadeIndex, 3u)), 1e-5f);
	const float farCascadeBlend = GetDirectionalCascadeNormalizedPosition(cascadeIndex);
	const float middleCascadeBlend = GetDirectionalCascadeMiddleWeight(cascadeIndex);
	const float minWorldRadiusScale = lerp(1.05f, 2.15f, farCascadeBlend) + middleCascadeBlend * 0.32f;
	const float clampedDesiredWorldTexelSize =
		max(desiredWorldTexelSize, cascadeWorldTexelSize * minWorldRadiusScale);
	const float worldRadiusScale = min(clampedDesiredWorldTexelSize / cascadeWorldTexelSize, 2.75f);

	// desiredWorldTexelSize 表示“希望在世界空间里有多宽的滤波尺度”，
	// 这里把它换算成当前 shadow map 上的 texel 偏移量。
	return (1.0f / max((float) shadowWidth, 1.0f)) * worldRadiusScale;
}

float ComputeDirectionalCascadeWideBlurBlend(uint cascadeIndex)
{
	const float middleCascadeBlend = GetDirectionalCascadeMiddleWeight(cascadeIndex);

	// 只给中间级联混入宽核结果，隔绝局部亮/暗斑块。
	// 第 0 级联保持锐利，最低分辨率级联也不继续额外变糊。
	return middleCascadeBlend * 0.34f;
}

float ComputeDirectionalCascadeWideBlurRadiusScale(uint cascadeIndex)
{
	const float middleCascadeBlend = GetDirectionalCascadeMiddleWeight(cascadeIndex);
	return lerp(1.0f, 1.82f, middleCascadeBlend);
}

float SampleDirectionalCascadeShadowWithWorldTexelSize(
	uint cascadeIndex,
	uint shadowIndex,
	uint shadowTransformIndex,
	float4 posW,
	float directionalBias,
	float nearHardBlend,
	float desiredWorldTexelSize)
{
	if (shadowIndex >= PBRX_MAX_DIRECTIONAL_SHADOW_MAP_COUNT)
		return 1.0f;

	const float4 shadowPos = mul(posW, g_ShadowTransform[shadowTransformIndex]);
	// 交界区会走这个入口，让当前级联和相邻级联临时共享同一套世界滤波尺度，
	// 避免一边更锐一边更糊，把级联边界直接看出来。
	return CalcDirectionalShadowFactor(
		g_DirectionalShadowMap[shadowIndex],
		shadowPos,
		directionalBias,
		nearHardBlend,
		ComputeDirectionalShadowFilterTexelSize(
			cascadeIndex,
			shadowIndex,
			desiredWorldTexelSize),
		ComputeDirectionalCascadeWideBlurBlend(cascadeIndex),
		ComputeDirectionalCascadeWideBlurRadiusScale(cascadeIndex));
}

uint SelectDirectionalShadowCascade(float3 posW, out float outLodDistance)
{
	outLodDistance = GetDirectionalShadowLodDistance(posW);

	const uint cascadeCount = GetDirectionalShadowCascadeCount();
	uint cascadeIndex = 0u;

	[unroll]
	for (uint splitIndex = 0u; splitIndex < 3u; ++splitIndex)
	{
		if (splitIndex + 1u >= cascadeCount)
			break;

		if (outLodDistance > GetDirectionalShadowCascadeSplit(splitIndex))
			cascadeIndex = splitIndex + 1u;
	}

	return cascadeIndex;
}

// 示例方向级联阴影
float SampleDirectionalCascadeShadow(
	uint cascadeIndex,
	uint shadowIndex,
	uint shadowTransformIndex,
	float4 posW,
	float directionalBias,
	float nearHardBlend)
{
	const float referenceWorldTexelSize = max(GetDirectionalShadowCascadeWorldTexelSize(0u), 1e-5f);
	return SampleDirectionalCascadeShadowWithWorldTexelSize(
		cascadeIndex,
		shadowIndex,
		shadowTransformIndex,
		posW,
		directionalBias,
		nearHardBlend,
		referenceWorldTexelSize);
}

float ComputeDirectionalLightShadowFactor(Light light, uint shadowBaseIndex, float4 posW, float3 normalW)
{
	float lodDistance = 0.0f;
	const uint cascadeCount = GetDirectionalShadowCascadeCount();
	const uint cascadeIndex = SelectDirectionalShadowCascade(posW.xyz, lodDistance);

	const float maxDirectionalShadowDistance = max(
		GetDirectionalShadowCascadeSplit(cascadeCount - 1u),
		0.0f);

	const uint shadowIndex = shadowBaseIndex + cascadeIndex;
	if (shadowIndex >= PBRX_MAX_DIRECTIONAL_SHADOW_MAP_COUNT)
		return 1.0f;

	const uint shadowTransformIndex = (uint)max(light.ShadowTransformIndex, 0.0f) + cascadeIndex;
	const float directionalBias = ComputeDirectionalReceiverBias(
		normalW,
		light.Direction,
		cascadeIndex,
		lodDistance,
		maxDirectionalShadowDistance);

	const float lodRatio = saturate(lodDistance / max(maxDirectionalShadowDistance, 0.001f));
	const float nearHardBlend = 1.0f - smoothstep(0.0f, 0.14f, lodRatio);

	// 当前级联基础结果
	// 常规区域先按当前级联自己的统一世界尺度采样。
	float shadowFactor = SampleDirectionalCascadeShadow(
		cascadeIndex,
		shadowIndex,
		shadowTransformIndex,
		posW,
		directionalBias,
		nearHardBlend);

	// 双向普通平滑混合 + 极弱亮向保护：
	// - 只在边界区稍微向更亮结果拉一点，压低残余接缝
	// 下面是级联交界区处理：
	// 1. 前后级联都各自重算 bias，避免直接复用当前级联 bias 造成黑点。
	// 2. 两边在交界区临时共享“渐进变粗”的世界滤波尺度，而不是硬切到更粗半径。
	// 3. 亮向保护基本关闭，避免把接缝重新抬成浅色带。
	// 稳定 CSM 下相邻级联有各自的 snapped center。
	// 混合带过窄会露出硬边，过宽又会把低分辨率级联的网格差异带回近处并表现成轻微抖动。
	// 这里使用配置值的中间比例，避免回到旧版 0.28 过窄，也避免 full ratio 过宽。
	const float blendRatio = saturate(g_DirectionalShadowCascadeSettings.y) * 0.42f;
	if (blendRatio <= 0.0f || cascadeCount <= 1u)
		return shadowFactor;

	// 边界区统一采用更保守、更稳定的硬边混合，减少局部黑颗粒。
	// 交界区把核的硬化程度压低一些，减轻边界附近的闪烁和脏点。
	const float seamNearHardBlend = min(nearHardBlend, 0.16f);

	// 亮向保护强度要很低，避免回到强修补产生的跳变问题。
	// 当前版本基本关闭亮向保护，优先避免形成额外的半透明亮带。
	const float protectStrength = 0.022f;

	if (cascadeIndex > 0u)
	{
		const float previousSplitDepth = GetDirectionalShadowCascadeSplit(cascadeIndex - 1u);
		const float previousPreviousSplitDepth =
			(cascadeIndex > 1u) ? GetDirectionalShadowCascadeSplit(cascadeIndex - 2u) : 0.0f;

		const float previousSplitRange = max(previousSplitDepth - previousPreviousSplitDepth, 0.001f);
		const float previousBlendBand = max(previousSplitRange * blendRatio, 1.0f);
		const float previousBlendStart = previousSplitDepth - previousBlendBand;
		const float previousBlendEnd = previousSplitDepth + previousBlendBand;

		if (lodDistance < previousBlendEnd)
		{
			const float previousDirectionalBias = ComputeDirectionalReceiverBias(
				normalW,
				light.Direction,
				cascadeIndex - 1u,
				lodDistance,
				maxDirectionalShadowDistance);
			const float previousSeamBias = max(directionalBias, previousDirectionalBias);
			const uint previousShadowIndex = shadowIndex - 1u;
			const float currentWorldTexelSize = GetDirectionalShadowCascadeWorldTexelSize(cascadeIndex);
			const float previousWorldTexelSize = GetDirectionalShadowCascadeWorldTexelSize(cascadeIndex - 1u);
			const float previousEdgeWeight =
				1.0f - smoothstep(0.0f, previousBlendBand, abs(lodDistance - previousSplitDepth));
			// 不再“一进混合区就直接切到 max(当前, 相邻)”。
			// 这里只会在越靠近分界线时，越多地往更粗的共同尺度靠拢，
			// 这样镜头移动时条纹不容易突然变粗。
			const float previousSeamWorldTexelSize = lerp(
				currentWorldTexelSize,
				max(currentWorldTexelSize, previousWorldTexelSize),
				previousEdgeWeight * 0.45f);
			const float seamCurrentShadowFactor = SampleDirectionalCascadeShadowWithWorldTexelSize(
				cascadeIndex,
				shadowIndex,
				shadowTransformIndex,
				posW,
				previousSeamBias,
				seamNearHardBlend,
				previousSeamWorldTexelSize);
			const float previousShadowFactor = SampleDirectionalCascadeShadowWithWorldTexelSize(
				cascadeIndex - 1u,
				previousShadowIndex,
				shadowTransformIndex - 1u,
				posW,
				previousSeamBias,
				seamNearHardBlend,
				previousSeamWorldTexelSize);

			const float previousBlendWeight = smoothstep(
				previousBlendStart,
				previousBlendEnd,
				lodDistance);

			// 先做普通平滑混合
			shadowFactor = lerp(previousShadowFactor, seamCurrentShadowFactor, previousBlendWeight);

			// 再做极弱亮向保护，只轻微拉向更亮结果，减少残余黑缝
			const float previousBrighter = max(shadowFactor, previousShadowFactor);
			shadowFactor = lerp(shadowFactor, previousBrighter, previousEdgeWeight * protectStrength);
		}
	}

	if (cascadeIndex + 1u < cascadeCount)
	{
		const float splitDepth = GetDirectionalShadowCascadeSplit(cascadeIndex);
		const float previousSplitDepth =
			(cascadeIndex > 0u) ? GetDirectionalShadowCascadeSplit(cascadeIndex - 1u) : 0.0f;

		const float splitRange = max(splitDepth - previousSplitDepth, 0.001f);
		const float blendBand = max(splitRange * blendRatio, 1.0f);
		const float blendStart = splitDepth - blendBand;
		const float blendEnd = splitDepth + blendBand;

		if (lodDistance > blendStart)
		{
			const float nextDirectionalBias = ComputeDirectionalReceiverBias(
				normalW,
				light.Direction,
				cascadeIndex + 1u,
				lodDistance,
				maxDirectionalShadowDistance);
			const float nextSeamBias = max(directionalBias, nextDirectionalBias);
			const uint nextShadowIndex = shadowIndex + 1u;
			const float currentWorldTexelSize = GetDirectionalShadowCascadeWorldTexelSize(cascadeIndex);
			const float nextWorldTexelSize = GetDirectionalShadowCascadeWorldTexelSize(cascadeIndex + 1u);
			const float nextEdgeWeight =
				1.0f - smoothstep(0.0f, blendBand, abs(lodDistance - splitDepth));
			// 后向交界区同理：当前级联和下一级联共享同一套渐进 seam 尺度。
			const float nextSeamWorldTexelSize = lerp(
				currentWorldTexelSize,
				max(currentWorldTexelSize, nextWorldTexelSize),
				nextEdgeWeight * 0.45f);
			const float seamCurrentShadowFactor = SampleDirectionalCascadeShadowWithWorldTexelSize(
				cascadeIndex,
				shadowIndex,
				shadowTransformIndex,
				posW,
				nextSeamBias,
				seamNearHardBlend,
				nextSeamWorldTexelSize);
			const float nextShadowFactor = SampleDirectionalCascadeShadowWithWorldTexelSize(
				cascadeIndex + 1u,
				nextShadowIndex,
				shadowTransformIndex + 1u,
				posW,
				nextSeamBias,
				seamNearHardBlend,
				nextSeamWorldTexelSize);

			const float blendWeight = smoothstep(blendStart, blendEnd, lodDistance);

			// 先做普通平滑混合
			shadowFactor = lerp(seamCurrentShadowFactor, nextShadowFactor, blendWeight);

			// 再做极弱亮向保护，只轻微拉向更亮结果，减少残余黑缝
			const float nextBrighter = max(shadowFactor, nextShadowFactor);
			shadowFactor = lerp(shadowFactor, nextBrighter, nextEdgeWeight * protectStrength);
		}
	}

	return shadowFactor;
}

float ComputeLightShadowFactor(Light light, int shadowBaseIndex, float4 posW, float3 normalW)
{
	if (shadowBaseIndex < 0)
		return 1.0f;

	const float shadowMode = light.ShadowSamplingMode;

	if (shadowMode == SHADOW_MODE_DIRECTIONAL_CASCADE || light.Type == DIRCTON_LIT)
	{
		return ComputeDirectionalLightShadowFactor(
			light,
			(uint) shadowBaseIndex,
			posW,
			normalW);
	}
	if (shadowMode == SHADOW_MODE_SPOT_MAP || light.Type == SPORT_LIT)
	{
		const uint shadowIndex = (uint) shadowBaseIndex;
		if (shadowIndex < PBRX_MAX_SPOT_SHADOW_MAP_COUNT)
		{
			const uint shadowTransformIndex = (uint) max(light.ShadowTransformIndex, 0.0f);
			const float4 shadowPos = mul(posW, g_ShadowTransform[shadowTransformIndex]);

			// 聚光灯 receiver bias
			const float3 lightToPixel = posW.xyz - light.Position;
			const float distanceToLight = length(lightToPixel);
			const float3 lightToPixelDir = lightToPixel / max(distanceToLight, 0.001f);
			const float spotNearPlane = max(light.ShadowNearPlane, 0.001f);
			const float spotFarPlane = max(light.ShadowFarPlane, spotNearPlane + 0.001f);
			const float spotBias = ComputeSpotLightReceiverBias(
				normalW, lightToPixelDir, distanceToLight, spotNearPlane, spotFarPlane) * max(light.ShadowBiasScale, 0.0f);

			return CalcShadowFactor(g_SpotShadowMap[shadowIndex], shadowPos, spotBias);
		}
		return 1.0f;
	}
	if (shadowMode == SHADOW_MODE_POINT_CUBE || light.Type == CONCENT_LIT)
	{
		// 点光源使用 TextureCube 采样。
		// 但当前阴影内容依然是“6 个透视面”写进去的，
		// 因此 compare depth 必须和面渲染时的透视深度保持同一数学空间。
		const float3 lightToPixel = posW.xyz - light.Position;
		const float distanceToLight = length(lightToPixel);
		if (distanceToLight <= 1e-5f)
			return 1.0f;

		const float3 sampleDir = lightToPixel / distanceToLight;
		const uint cubeIndex = (uint) shadowBaseIndex;
		if (cubeIndex >= 64u)
			return 1.0f;

		const float nearPlane = max(light.ShadowNearPlane, 0.001f);
		const float farPlane = max(light.ShadowFarPlane, nearPlane + 0.001f);
		const float compareDepth = ComputePointLightCubeCompareDepth(lightToPixel, nearPlane, farPlane);

		const float3 lightToPixelDir = sampleDir;
		const float pointBias = ComputePointLightReceiverBias(
			normalW,
			lightToPixelDir,
			distanceToLight,
			nearPlane,
			farPlane) * max(light.ShadowBiasScale, 0.0f);
		const float biasedDepth = saturate(compareDepth - pointBias);

		const float shadowFactor = g_PointLightShadowCube[cubeIndex].SampleCmpLevelZero(
			g_SamShadowCube,
			sampleDir,
			biasedDepth);

		const float shadowOpacity = saturate(g_ShadowSettings.x);
		return lerp(1.0f - shadowOpacity, 1.0f, shadowFactor);
	}

	return 1.0f;
}
