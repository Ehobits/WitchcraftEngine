#include "ShadowFrameBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ShadowFrameBuilderDetail
{
	struct CameraFrustumSlice
	{
		std::array<DirectX::XMFLOAT3, 8> CornersWS{};
		float Radius = 1.0f;
	};

	struct LightSpaceReceiverBounds
	{
		float MinX = 0.0f;
		float MaxX = 0.0f;
		float MinY = 0.0f;
		float MaxY = 0.0f;
		float MinZ = 0.0f;
		float MaxZ = 0.0f;
	};

	UINT ClampDirectionalCascadeConfigIndex(UINT cascadeIndex)
	{
		return (std::min)(cascadeIndex, 2u);
	}

	float ComputeWorldUnitsPerTexel(float orthographicExtent, float shadowMapSize)
	{
		return (std::max)((2.0f * orthographicExtent) / (std::max)(shadowMapSize, 1.0f), 1e-5f);
	}

	float ComputeTexelPadding(float worldUnitsPerTexel, float paddingTexels)
	{
		return worldUnitsPerTexel * (std::max)(paddingTexels, 0.0f);
	}

	float SnapLightSpaceCoordinate(float coordinate, float snapWorldUnits)
	{
		const float gridSize = (std::max)(snapWorldUnits, 1e-5f);
		return std::floor((coordinate / gridSize) + 0.5f) * gridSize;
	}

	CameraFrustumSlice BuildCameraFrustumSlice(
		const DirectX::XMMATRIX& invView,
		float nearZ,
		float farZ,
		float tanHalfFovX,
		float tanHalfFovY)
	{
		using namespace DirectX;

		nearZ = (std::max)(nearZ, 0.001f);
		farZ = (std::max)(farZ, nearZ + 0.001f);

		const float nearHalfWidth = nearZ * tanHalfFovX;
		const float nearHalfHeight = nearZ * tanHalfFovY;
		const float farHalfWidth = farZ * tanHalfFovX;
		const float farHalfHeight = farZ * tanHalfFovY;

		const std::array<XMVECTOR, 8> cornersVS =
		{
			XMVectorSet(-nearHalfWidth, -nearHalfHeight, nearZ, 1.0f),
			XMVectorSet(-nearHalfWidth, +nearHalfHeight, nearZ, 1.0f),
			XMVectorSet(+nearHalfWidth, +nearHalfHeight, nearZ, 1.0f),
			XMVectorSet(+nearHalfWidth, -nearHalfHeight, nearZ, 1.0f),
			XMVectorSet(-farHalfWidth, -farHalfHeight, farZ, 1.0f),
			XMVectorSet(-farHalfWidth, +farHalfHeight, farZ, 1.0f),
			XMVectorSet(+farHalfWidth, +farHalfHeight, farZ, 1.0f),
			XMVectorSet(+farHalfWidth, -farHalfHeight, farZ, 1.0f)
		};

		CameraFrustumSlice slice;
		XMVECTOR centerWS = XMVectorZero();
		for (UINT cornerIndex = 0; cornerIndex < static_cast<UINT>(slice.CornersWS.size()); ++cornerIndex)
		{
			const XMVECTOR cornerWS = XMVector3TransformCoord(cornersVS[cornerIndex], invView);
			XMStoreFloat3(&slice.CornersWS[cornerIndex], cornerWS);
			centerWS += cornerWS;
		}
		centerWS /= static_cast<float>(slice.CornersWS.size());

		float radius = 0.0f;
		for (const XMFLOAT3& cornerWS : slice.CornersWS)
			radius = (std::max)(radius, XMVectorGetX(XMVector3Length(XMLoadFloat3(&cornerWS) - centerWS)));

		// 半径量化能避免相机轻微旋转时投影尺寸持续发生小数级变化。
		slice.Radius = (std::max)(1.0f, std::ceil(radius * 8.0f) / 8.0f);
		return slice;
	}

	LightSpaceReceiverBounds BuildLightSpaceReceiverBounds(
		const CameraFrustumSlice& slice,
		const DirectX::XMMATRIX& lightView)
	{
		using namespace DirectX;

		LightSpaceReceiverBounds bounds;
		bool initialized = false;
		for (const XMFLOAT3& cornerWS : slice.CornersWS)
		{
			XMFLOAT3 cornerLS{};
			XMStoreFloat3(&cornerLS, XMVector3TransformCoord(XMLoadFloat3(&cornerWS), lightView));

			if (!initialized)
			{
				bounds.MinX = bounds.MaxX = cornerLS.x;
				bounds.MinY = bounds.MaxY = cornerLS.y;
				bounds.MinZ = bounds.MaxZ = cornerLS.z;
				initialized = true;
				continue;
			}

			bounds.MinX = (std::min)(bounds.MinX, cornerLS.x);
			bounds.MaxX = (std::max)(bounds.MaxX, cornerLS.x);
			bounds.MinY = (std::min)(bounds.MinY, cornerLS.y);
			bounds.MaxY = (std::max)(bounds.MaxY, cornerLS.y);
			bounds.MinZ = (std::min)(bounds.MinZ, cornerLS.z);
			bounds.MaxZ = (std::max)(bounds.MaxZ, cornerLS.z);
		}

		return bounds;
	}

	float ComputeReceiverCoverageExtent(
		const LightSpaceReceiverBounds& receiverBoundsLS,
		float centerX,
		float centerY,
		float padding)
	{
		const float requiredHalfX = (std::max)(
			std::abs(receiverBoundsLS.MinX - centerX),
			std::abs(receiverBoundsLS.MaxX - centerX));
		const float requiredHalfY = (std::max)(
			std::abs(receiverBoundsLS.MinY - centerY),
			std::abs(receiverBoundsLS.MaxY - centerY));
		return (std::max)(requiredHalfX, requiredHalfY) + (std::max)(padding, 0.0f);
	}

	DirectX::XMFLOAT3 BuildCameraDepthCenterWS(
		const DirectX::XMMATRIX& invView,
		float nearZ,
		float farZ)
	{
		using namespace DirectX;

		nearZ = (std::max)(nearZ, 0.001f);
		farZ = (std::max)(farZ, nearZ + 0.001f);

		XMFLOAT3 centerWS{};
		const float centerDepth = (nearZ + farZ) * 0.5f;
		XMStoreFloat3(&centerWS, XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, centerDepth, 1.0f), invView));
		return centerWS;
	}

	bool IsDirectionalCascadeStateCompatible(
		const ShadowFrameBuilder::StableDirectionalCascadeState& state,
		UINT lightIndex,
		DirectX::XMVECTOR lightDir,
		float radius,
		float projectionWorldUnitsPerTexel)
	{
		constexpr float RadiusResetTexels = 0.5f;

		const float radiusResetDistance = projectionWorldUnitsPerTexel * RadiusResetTexels;
		const DirectX::XMVECTOR previousLightDir = DirectX::XMLoadFloat3(&state.LightDirection);
		const float lightDirectionDot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(previousLightDir, lightDir));
		const bool projectionScaleStillCompatible =
			std::abs(state.Radius - radius) <= radiusResetDistance &&
			std::abs(state.WorldUnitsPerTexel - projectionWorldUnitsPerTexel) <= projectionWorldUnitsPerTexel * 0.25f;
		const bool lightStillCompatible =
			state.LightIndex == lightIndex &&
			lightDirectionDot >= 0.9999f;

		return state.Valid && lightStillCompatible && projectionScaleStillCompatible;
	}

	void ApplyCascadeRawCenterSmoothing(
		UINT lightIndex,
		UINT cascadeIndex,
		DirectX::XMVECTOR lightDir,
		float radius,
		float projectionWorldUnitsPerTexel,
		float smoothingAlpha,
		float maxCenterLagWorldUnits,
		ShadowFrameBuilder::StableDirectionalCascadeStates* stableDirectionalCascadeStates,
		float* rawCenterX,
		float* rawCenterY)
	{
		if (stableDirectionalCascadeStates == nullptr ||
			rawCenterX == nullptr ||
			rawCenterY == nullptr ||
			cascadeIndex >= stableDirectionalCascadeStates->size())
		{
			return;
		}

		ShadowFrameBuilder::StableDirectionalCascadeState& state = (*stableDirectionalCascadeStates)[cascadeIndex];
		const bool stateCompatible =
			IsDirectionalCascadeStateCompatible(
				state,
				lightIndex,
				lightDir,
				radius,
				projectionWorldUnitsPerTexel);

		if (!state.RawCenterValid || !stateCompatible)
		{
			state.RawCenterValid = true;
			state.RawCenterX = *rawCenterX;
			state.RawCenterY = *rawCenterY;
			return;
		}

		const float alpha = std::clamp(smoothingAlpha, 0.01f, 1.0f);
		state.RawCenterX += (*rawCenterX - state.RawCenterX) * alpha;
		state.RawCenterY += (*rawCenterY - state.RawCenterY) * alpha;

		const float maxLag = (std::max)(maxCenterLagWorldUnits, 0.0f);
		if (maxLag > 0.0f)
		{
			state.RawCenterX = std::clamp(state.RawCenterX, *rawCenterX - maxLag, *rawCenterX + maxLag);
			state.RawCenterY = std::clamp(state.RawCenterY, *rawCenterY - maxLag, *rawCenterY + maxLag);
		}

		*rawCenterX = state.RawCenterX;
		*rawCenterY = state.RawCenterY;
	}

	void ApplyCascadeCenterHysteresis(
		UINT lightIndex,
		UINT cascadeIndex,
		DirectX::XMVECTOR lightDir,
		float radius,
		float projectionWorldUnitsPerTexel,
		float centerSnapWorldUnits,
		float centerHysteresisTexels,
		ShadowFrameBuilder::StableDirectionalCascadeStates* stableDirectionalCascadeStates,
		float* centerX,
		float* centerY)
	{
		if (stableDirectionalCascadeStates == nullptr ||
			centerX == nullptr ||
			centerY == nullptr ||
			cascadeIndex >= stableDirectionalCascadeStates->size())
		{
			return;
		}

		ShadowFrameBuilder::StableDirectionalCascadeState& state = (*stableDirectionalCascadeStates)[cascadeIndex];
		const float hysteresisDistance =
			(std::max)(centerSnapWorldUnits, 1e-5f) * (std::max)(centerHysteresisTexels, 0.0f);
		const bool stateCompatible =
			IsDirectionalCascadeStateCompatible(
				state,
				lightIndex,
				lightDir,
				radius,
				projectionWorldUnitsPerTexel);

		if (stateCompatible &&
			std::abs(*centerX - state.CenterX) < hysteresisDistance &&
			std::abs(*centerY - state.CenterY) < hysteresisDistance)
		{
			*centerX = state.CenterX;
			*centerY = state.CenterY;
			return;
		}

		state.Valid = true;
		state.LightIndex = lightIndex;
		state.CenterX = *centerX;
		state.CenterY = *centerY;
		state.Radius = radius;
		state.WorldUnitsPerTexel = projectionWorldUnitsPerTexel;
		DirectX::XMStoreFloat3(&state.LightDirection, lightDir);
	}

	void StoreCascadeCenterState(
		UINT lightIndex,
		UINT cascadeIndex,
		DirectX::XMVECTOR lightDir,
		float radius,
		float projectionWorldUnitsPerTexel,
		ShadowFrameBuilder::StableDirectionalCascadeStates* stableDirectionalCascadeStates,
		float centerX,
		float centerY)
	{
		if (stableDirectionalCascadeStates == nullptr ||
			cascadeIndex >= stableDirectionalCascadeStates->size())
		{
			return;
		}

		ShadowFrameBuilder::StableDirectionalCascadeState& state = (*stableDirectionalCascadeStates)[cascadeIndex];
		state.Valid = true;
		state.LightIndex = lightIndex;
		state.CenterX = centerX;
		state.CenterY = centerY;
		state.Radius = radius;
		state.WorldUnitsPerTexel = projectionWorldUnitsPerTexel;
		DirectX::XMStoreFloat3(&state.LightDirection, lightDir);
	}

	float ResolveDirectionalCascadeProjectionRadius(
		const D3DWindowShadowConfig& shadowConfig,
		UINT cascadeIndex,
		float frustumSliceRadius)
	{
		frustumSliceRadius = (std::max)(frustumSliceRadius, 1.0f);
		if (!shadowConfig.DirectionalCascadeUseFixedRadius)
			return frustumSliceRadius;

		const float configuredRadius =
			shadowConfig.DirectionalCascadeFixedRadii[ClampDirectionalCascadeConfigIndex(cascadeIndex)];
		if (configuredRadius <= 0.0f)
			return frustumSliceRadius;

		// 固定半径只是稳定目标，不是硬裁剪范围。
		// 这里仍以实际 frustum slice 半径兜底，避免异常 FOV/宽高比裁掉 receiver。
		return (std::max)(configuredRadius, frustumSliceRadius);
	}
}

std::array<float, 3> ShadowFrameBuilder::BuildDirectionalCascadeSplits(float nearZ, float farZ, float lambda, UINT cascadeCount)
{
	std::array<float, 3> splits = { farZ, farZ, farZ };
	nearZ = (std::max)(nearZ, 0.001f);
	farZ = (std::max)(farZ, nearZ + 0.001f);
	lambda = std::clamp(lambda, 0.0f, 1.0f);
	cascadeCount = std::clamp(cascadeCount, 1u, 3u);

	const float clipRange = farZ - nearZ;
	for (UINT cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
	{
		const float p = static_cast<float>(cascadeIndex + 1u) / static_cast<float>(cascadeCount);
		const float linearSplit = nearZ + clipRange * p;
		const float logarithmicSplit = nearZ * std::pow(farZ / nearZ, p);
		splits[cascadeIndex] = linearSplit * (1.0f - lambda) + logarithmicSplit * lambda;
	}

	splits[cascadeCount - 1u] = farZ;
	for (UINT cascadeIndex = cascadeCount; cascadeIndex < static_cast<UINT>(splits.size()); ++cascadeIndex)
		splits[cascadeIndex] = farZ;
	return splits;
}

ShadowFrameBuildResult ShadowFrameBuilder::BuildShadowFrame(
	const D3DWindowShadowConfig& shadowConfig,
	const Camera& camera,
	const std::vector<Light>& lightsCache,
	const std::vector<int>& lightShadowMapIndices,
	UINT shadowMapCount,
	ShadowFrameBuilder::StableDirectionalCascadeStates* stableDirectionalCascadeStates)
{
	using namespace DirectX;

	ShadowFrameBuildResult result;

	// 先沿用当前系统的保守场景包围估计。
	// 目标仍然是“先稳定、先别裁掉”，而不是追求最紧拟合。
	BoundingSphere sceneBounds;
	sceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	sceneBounds.Radius = sqrtf(100.0f * 100.0f + 64.0f * 64.0f);

	const UINT directionalCascadeCount = std::clamp(shadowConfig.DirectionalCascadeCount, 1u, 3u);
	const float directionalCascadeBlendRatio = std::clamp(shadowConfig.DirectionalCascadeBlendRatio, 0.0f, 1.0f);
	const float cameraNearZ = (std::max)(camera.GetNearZ(), 0.001f);
	const float cameraFarZ = (std::max)(camera.GetFarZ(), cameraNearZ + 0.001f);
	const float directionalShadowFarZ = (std::min)(
		cameraFarZ,
		(std::max)(cameraNearZ + 1.0f, shadowConfig.DirectionalShadowMaxDistance));
	const std::array<float, 3> cascadeSplits = BuildDirectionalCascadeSplits(
		cameraNearZ,
		directionalShadowFarZ,
		shadowConfig.DirectionalCascadeLambda,
		directionalCascadeCount);

	result.DirectionalShadowCascadeSplits = XMFLOAT4(
		cascadeSplits[0],
		cascadeSplits[1],
		cascadeSplits[2],
		cascadeSplits[2]);
	result.DirectionalShadowCascadeSettings = XMFLOAT4(
		static_cast<float>(directionalCascadeCount),
		directionalCascadeBlendRatio,
		(std::max)(shadowConfig.DirectionalCascadeReceiverBiasBase, 0.0f),
		(std::max)(shadowConfig.DirectionalCascadeReceiverBiasCascadeScale, 0.0f));
	result.DirectionalShadowCascadeWorldTexelSize = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	result.DirectionalShadowCascadeDepthScale = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

	const UINT activeShadowMapCount = (std::min)(shadowMapCount, shadowConfig.MaxShadowMapCount);
	result.ShadowTransforms.assign(activeShadowMapCount, MathHelps::Identity);
	result.ShadowRenderEntries.clear();
	if (lightsCache.empty() || activeShadowMapCount == 0u)
		return result;

	// 把裁剪空间 [-1,1] 映射到纹理空间 [0,1]，供 shader 直接采样阴影贴图。
	const XMMATRIX textureTransform(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	const int directionalLightType = static_cast<int>(std::lround(shadowConfig.DirectionalLightType));
	const int pointLightType = static_cast<int>(std::lround(shadowConfig.PointLightType));
	const int spotLightType = static_cast<int>(std::lround(shadowConfig.SpotLightType));

	const float cameraAspect = (std::max)(camera.GetViewportScale(), 0.001f);
	const float cameraFovY = (std::max)(camera.GetFovY(), 0.001f);
	const float tanHalfFovY = std::tan(0.5f * cameraFovY);
	const float tanHalfFovX = tanHalfFovY * cameraAspect;

	for (UINT lightIndex = 0; lightIndex < lightsCache.size(); ++lightIndex)
	{
		const Light& light = lightsCache[lightIndex];
		if (!light.CastShadow)
			continue;
		if (lightIndex >= lightShadowMapIndices.size())
			continue;

		const int shadowBaseIndex = lightShadowMapIndices[lightIndex];
		if (shadowBaseIndex < 0 || static_cast<UINT>(shadowBaseIndex) >= result.ShadowTransforms.size())
			continue;

		const int resolvedLightType = static_cast<int>(std::lround(light.Type));
		if (resolvedLightType != directionalLightType &&
			resolvedLightType != pointLightType &&
			resolvedLightType != spotLightType)
			continue;

		XMVECTOR lightDir = XMLoadFloat3(&light.Direction);
		if (XMVector3Equal(lightDir, XMVectorZero()))
			lightDir = XMLoadFloat3(&shadowConfig.FallbackDirection);
		lightDir = XMVector3Normalize(lightDir);

		XMVECTOR lightUp = XMLoadFloat3(&light.Up);
		if (XMVector3Equal(lightUp, XMVectorZero()))
			lightUp = XMLoadFloat3(&shadowConfig.FallbackUp);
		lightUp = XMVector3Normalize(lightUp);

		XMVECTOR lightPos = XMLoadFloat3(&light.Position);

		if (resolvedLightType == directionalLightType)
		{
			const UINT shadowBaseSlot = static_cast<UINT>(shadowBaseIndex);
			if (shadowBaseSlot >= result.ShadowTransforms.size())
				continue;

			const UINT maxCascadeSlots = static_cast<UINT>(result.ShadowTransforms.size() - shadowBaseSlot);
			const UINT activeCascadeCount = (std::min)(directionalCascadeCount, maxCascadeSlots);
			if (activeCascadeCount == 0u)
				continue;

			// 方向光使用 stabilized CSM：
			// 每一级联仍按相机深度 split 覆盖一段视锥，但投影尺度使用固定档位，
			// 中心只跟随该 split 的视线深度中点，再吸附到 light-space 全局 texel 网格。
			// 注意不要重新回到“每帧从 frustum corners 拟合投影中心/半径”的逻辑，
			// 否则相机旋转或进深移动时会重新引入投影尺度/中心的小数级抖动。
			XMVECTOR cascadeLightUp = lightUp;
			if (std::abs(XMVectorGetX(XMVector3Dot(lightDir, cascadeLightUp))) > 0.99f)
			{
				XMVECTOR fallbackUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
				if (std::abs(XMVectorGetX(XMVector3Dot(lightDir, fallbackUp))) > 0.99f)
					fallbackUp = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
				cascadeLightUp = fallbackUp;
			}

			// 先估算一个能覆盖主视锥的大致半径，再据此决定方向光离场景多远。
			const float farCoverageDepth = directionalShadowFarZ;
			const float farCoverageHalfWidth = farCoverageDepth * tanHalfFovX;
			const float farCoverageHalfHeight = farCoverageDepth * tanHalfFovY;
			float maxShadowCoverageRadius = std::sqrt(
				farCoverageHalfWidth * farCoverageHalfWidth +
				farCoverageHalfHeight * farCoverageHalfHeight +
				farCoverageDepth * farCoverageDepth);
			maxShadowCoverageRadius = (std::max)(maxShadowCoverageRadius, 1.0f);
			maxShadowCoverageRadius = std::ceil(maxShadowCoverageRadius * 16.0f) / 16.0f;

			const float directionalLightDistance = maxShadowCoverageRadius + sceneBounds.Radius + 64.0f;

			// referenceLightView 只提供方向光的全局 light-space 轴，用于稳定 snap。
			// 真正渲染每个 cascade 时会用 snapped cascade center 重新构建局部 lightView，
			// 避免相机/物体远离世界原点后，大坐标 off-center projection 造成阴影裁切或精度问题。
			const XMVECTOR referenceLightAnchorWS = XMVectorZero();
			const XMVECTOR referenceLightPos = referenceLightAnchorWS - lightDir * directionalLightDistance;
			const XMMATRIX referenceLightView = XMMatrixLookAtLH(referenceLightPos, referenceLightAnchorWS, cascadeLightUp);
			XMVECTOR referenceLightViewDeterminant = XMMatrixDeterminant(referenceLightView);
			const XMMATRIX invReferenceLightView = XMMatrixInverse(&referenceLightViewDeterminant, referenceLightView);

			const XMMATRIX cameraView = camera.GetView();
			XMVECTOR cameraViewDeterminant = XMMatrixDeterminant(cameraView);
			const XMMATRIX invCameraView = XMMatrixInverse(&cameraViewDeterminant, cameraView);

			for (UINT cascadeIndex = 0; cascadeIndex < activeCascadeCount; ++cascadeIndex)
			{
				const float cascadeNearDepth =
					(cascadeIndex == 0u)
						? cameraNearZ
						: cascadeSplits[ShadowFrameBuilderDetail::ClampDirectionalCascadeConfigIndex(cascadeIndex - 1u)];
				const UINT cascadeConfigIndex = ShadowFrameBuilderDetail::ClampDirectionalCascadeConfigIndex(cascadeIndex);
				const float cascadeFarDepth = cascadeSplits[cascadeConfigIndex];
				const ShadowFrameBuilderDetail::CameraFrustumSlice cascadeSlice =
					ShadowFrameBuilderDetail::BuildCameraFrustumSlice(
						invCameraView,
						cascadeNearDepth,
						cascadeFarDepth,
						tanHalfFovX,
						tanHalfFovY);

				const float cascadeReferenceShadowMapSize = (std::max)(
					1.0f,
					static_cast<float>(shadowConfig.DirectionalCascadeShadowMapSizes[cascadeConfigIndex]));

				const float stableProjectionRadiusFloor =
					ShadowFrameBuilderDetail::ResolveDirectionalCascadeProjectionRadius(
						shadowConfig,
						cascadeIndex,
						cascadeSlice.Radius);

				const XMFLOAT3 cascadeCenterWS =
					ShadowFrameBuilderDetail::BuildCameraDepthCenterWS(
						invCameraView,
						cascadeNearDepth,
						cascadeFarDepth);

				float xyExtent = stableProjectionRadiusFloor;
				float worldUnitsPerTexel =
					ShadowFrameBuilderDetail::ComputeWorldUnitsPerTexel(xyExtent, cascadeReferenceShadowMapSize);
				const float orthographicPadding =
					ShadowFrameBuilderDetail::ComputeTexelPadding(
						worldUnitsPerTexel,
						shadowConfig.DirectionalCascadeOrthographicPaddingTexels);
				const float smoothingLagPadding =
					ShadowFrameBuilderDetail::ComputeTexelPadding(
						worldUnitsPerTexel,
						shadowConfig.DirectionalCascadeCenterSmoothingMaxLagTexels[cascadeConfigIndex]);
				const float depthPadding =
					(std::max)(
						ShadowFrameBuilderDetail::ComputeTexelPadding(
							worldUnitsPerTexel,
							shadowConfig.DirectionalCascadeDepthPaddingTexels),
						0.25f);
				xyExtent += orthographicPadding + smoothingLagPadding;

				float projectionWorldUnitsPerTexel =
					ShadowFrameBuilderDetail::ComputeWorldUnitsPerTexel(xyExtent, cascadeReferenceShadowMapSize);
				float centerSnapWorldUnits =
					ShadowFrameBuilderDetail::ComputeTexelPadding(
						projectionWorldUnitsPerTexel,
						(std::max)(shadowConfig.DirectionalCascadeCenterSnapTexelScale, 0.125f));

				XMFLOAT3 shadowCenterLS{};
				XMStoreFloat3(
					&shadowCenterLS,
					XMVector3TransformCoord(XMLoadFloat3(&cascadeCenterWS), referenceLightView));

				float rawCenterX = shadowCenterLS.x;
				float rawCenterY = shadowCenterLS.y;

				const ShadowFrameBuilderDetail::LightSpaceReceiverBounds receiverBoundsLS =
					ShadowFrameBuilderDetail::BuildLightSpaceReceiverBounds(cascadeSlice, referenceLightView);

				// 把每级联的 light-space 中心吸附到可配置的 texel 子网格。
				// 0.5 texel 默认值会降低整 texel snap 的可见跳变，但仍避免完全连续移动导致的 shimmering。
				float centerX = ShadowFrameBuilderDetail::SnapLightSpaceCoordinate(rawCenterX, centerSnapWorldUnits);
				float centerY = ShadowFrameBuilderDetail::SnapLightSpaceCoordinate(rawCenterY, centerSnapWorldUnits);
				const float preHysteresisCoverageExtent =
					ShadowFrameBuilderDetail::ComputeReceiverCoverageExtent(
						receiverBoundsLS,
						centerX,
						centerY,
						orthographicPadding);
				xyExtent = (std::max)(xyExtent, preHysteresisCoverageExtent);

				projectionWorldUnitsPerTexel =
					ShadowFrameBuilderDetail::ComputeWorldUnitsPerTexel(xyExtent, cascadeReferenceShadowMapSize);
				centerSnapWorldUnits =
					ShadowFrameBuilderDetail::ComputeTexelPadding(
						projectionWorldUnitsPerTexel,
						(std::max)(shadowConfig.DirectionalCascadeCenterSnapTexelScale, 0.125f));

				ShadowFrameBuilderDetail::ApplyCascadeRawCenterSmoothing(
					lightIndex,
					cascadeIndex,
					lightDir,
					xyExtent,
					projectionWorldUnitsPerTexel,
					shadowConfig.DirectionalCascadeCenterSmoothingAlphas[cascadeConfigIndex],
					smoothingLagPadding,
					stableDirectionalCascadeStates,
					&rawCenterX,
					&rawCenterY);

				centerX = ShadowFrameBuilderDetail::SnapLightSpaceCoordinate(rawCenterX, centerSnapWorldUnits);
				centerY = ShadowFrameBuilderDetail::SnapLightSpaceCoordinate(rawCenterY, centerSnapWorldUnits);
				ShadowFrameBuilderDetail::ApplyCascadeCenterHysteresis(
					lightIndex,
					cascadeIndex,
					lightDir,
					xyExtent,
					projectionWorldUnitsPerTexel,
					centerSnapWorldUnits,
					shadowConfig.DirectionalCascadeCenterHysteresisTexels,
					stableDirectionalCascadeStates,
					&centerX,
					&centerY);

				const float finalCoveragePadding =
					ShadowFrameBuilderDetail::ComputeTexelPadding(
						projectionWorldUnitsPerTexel,
						shadowConfig.DirectionalCascadeOrthographicPaddingTexels);
				const float postHysteresisCoverageExtent =
					ShadowFrameBuilderDetail::ComputeReceiverCoverageExtent(
						receiverBoundsLS,
						centerX,
						centerY,
						finalCoveragePadding);
				xyExtent = (std::max)(xyExtent, postHysteresisCoverageExtent);
				projectionWorldUnitsPerTexel =
					ShadowFrameBuilderDetail::ComputeWorldUnitsPerTexel(xyExtent, cascadeReferenceShadowMapSize);

				const XMVECTOR snappedCenterLS = XMVectorSet(centerX, centerY, shadowCenterLS.z, 1.0f);
				const XMVECTOR snappedCascadeCenterWS = XMVector3TransformCoord(snappedCenterLS, invReferenceLightView);
				const XMVECTOR cascadeLightPos = snappedCascadeCenterWS - lightDir * directionalLightDistance;
				const XMMATRIX lightView = XMMatrixLookAtLH(cascadeLightPos, snappedCascadeCenterWS, cascadeLightUp);

				XMFLOAT3 localShadowCenterLS{};
				XMStoreFloat3(&localShadowCenterLS, XMVector3TransformCoord(snappedCascadeCenterWS, lightView));

				const ShadowFrameBuilderDetail::LightSpaceReceiverBounds finalReceiverBoundsLS =
					ShadowFrameBuilderDetail::BuildLightSpaceReceiverBounds(cascadeSlice, lightView);
				const float finalProjectionPadding =
					ShadowFrameBuilderDetail::ComputeTexelPadding(
						projectionWorldUnitsPerTexel,
						shadowConfig.DirectionalCascadeOrthographicPaddingTexels);
				const float finalReceiverCoverageExtent =
					ShadowFrameBuilderDetail::ComputeReceiverCoverageExtent(
						finalReceiverBoundsLS,
						localShadowCenterLS.x,
						localShadowCenterLS.y,
						finalProjectionPadding);
				xyExtent = (std::max)(xyExtent, finalReceiverCoverageExtent);
				projectionWorldUnitsPerTexel =
					ShadowFrameBuilderDetail::ComputeWorldUnitsPerTexel(xyExtent, cascadeReferenceShadowMapSize);
				const float finalProjectionPaddingAfterResize =
					ShadowFrameBuilderDetail::ComputeTexelPadding(
						projectionWorldUnitsPerTexel,
						shadowConfig.DirectionalCascadeOrthographicPaddingTexels);
				xyExtent = (std::max)(
					xyExtent,
					ShadowFrameBuilderDetail::ComputeReceiverCoverageExtent(
						finalReceiverBoundsLS,
						localShadowCenterLS.x,
						localShadowCenterLS.y,
						finalProjectionPaddingAfterResize));
				projectionWorldUnitsPerTexel =
					ShadowFrameBuilderDetail::ComputeWorldUnitsPerTexel(xyExtent, cascadeReferenceShadowMapSize);
				ShadowFrameBuilderDetail::StoreCascadeCenterState(
					lightIndex,
					cascadeIndex,
					lightDir,
					xyExtent,
					projectionWorldUnitsPerTexel,
					stableDirectionalCascadeStates,
					centerX,
					centerY);

				const float l = -xyExtent;
				const float b = -xyExtent;
				const float r = xyExtent;
				const float t = xyExtent;

				// Receiver 的 XY 覆盖由最终 lightView 下的 8 个 frustum corner 保证。
				// Z 范围也不能继续只围绕 cascade center 粗估，否则相机远离场景中心或光照方向斜切时，
				// receiver/caster 可能落到正交投影深度范围外，shader 会因为 shadowPos.z 越界直接返回全亮。
				// 方向光阴影真正需要保守的是“receiver 朝光源方向之前的 caster 深度”：
				// - near 侧给 caster 留出一段深度，让能投到当前 receiver 上的物体被画进 shadow map；
				// - far 侧只需覆盖 receiver 自身和少量安全余量，避免无意义扩大深度范围损失精度。
				const float receiverDepthPadding =
					(std::max)(depthPadding, projectionWorldUnitsPerTexel * 4.0f);
				const float casterDepthPadding =
					(std::max)(
						directionalShadowFarZ + xyExtent,
						depthPadding * 2.0f);
				const float n = finalReceiverBoundsLS.MinZ - casterDepthPadding;
				float f = finalReceiverBoundsLS.MaxZ + receiverDepthPadding;
				if (f <= n + 0.001f)
					f = n + 0.001f;

				// 这组 bounds 只用于 shadow pass 的 caster culling，不代表 receiver 的最终采样投影。
				// 它必须比 l/r/b/t 更保守一点：大型物体或 bounds 粗糙的物体只要有可能投影到 receiver 区域，
				// 就不应在 CPU 侧被提前剔除。
				const float casterCullXYPadding =
					(std::max)(
						finalProjectionPaddingAfterResize,
						projectionWorldUnitsPerTexel * 16.0f);

				// 这两组值专门给 shader 用来统一不同级联的滤波尺度和 receiver bias。
				float* cascadeWorldTexelSizes = &result.DirectionalShadowCascadeWorldTexelSize.x;
				float* cascadeDepthScales = &result.DirectionalShadowCascadeDepthScale.x;
				cascadeWorldTexelSizes[cascadeConfigIndex] = projectionWorldUnitsPerTexel;
				cascadeDepthScales[cascadeConfigIndex] = 1.0f / (std::max)(f - n, 1e-5f);

				const XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

				const UINT shadowSlotIndex = shadowBaseSlot + cascadeIndex;

				ShadowRenderEntry entry;
				entry.LightIndex = lightIndex;
				entry.ShadowSlotIndex = shadowSlotIndex;
				entry.PassCBIndex = 1u + static_cast<UINT>(result.ShadowRenderEntries.size());
				entry.ViewIndex = cascadeIndex;
				entry.ViewKind = ShadowViewKind::DirectionalCascade;
				entry.ProjectionKind = ShadowProjectionKind::Orthographic;
				XMStoreFloat4x4(&entry.View, lightView);
				XMStoreFloat4x4(&entry.Proj, lightProj);
				XMStoreFloat4x4(&entry.Transform, lightView * lightProj * textureTransform);
				XMStoreFloat3(&entry.EyePosition, cascadeLightPos);
				entry.NearPlane = n;
				entry.FarPlane = f;
				entry.ShadowMapSize = static_cast<UINT>(cascadeReferenceShadowMapSize);
				entry.HasLightSpaceCullBounds = true;
				entry.LightSpaceCullMin = XMFLOAT3(l - casterCullXYPadding, b - casterCullXYPadding, n);
				entry.LightSpaceCullMax = XMFLOAT3(r + casterCullXYPadding, t + casterCullXYPadding, f);

				result.ShadowTransforms[shadowSlotIndex] = entry.Transform;
				result.ShadowRenderEntries.push_back(entry);
			}
		}
		else if (resolvedLightType == pointLightType)
		{
			// 点光源使用 TextureCube：单个槽位包含 6 面
			// 渲染时仍需 6 个 entry（每面一个），但采样时使用 cubemap 方式
			const XMVECTOR faceDirections[6] =
			{
				XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
				XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f),
				XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
				XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f),
				XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
				XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f)
			};
			const XMVECTOR faceUps[6] =
			{
				XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
				XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
				XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f),
				XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
				XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
				XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
			};

			const float nearZ = 0.1f;
			const float farZ = 100.0f;
			const XMMATRIX lightProj = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, nearZ, farZ);

			const UINT shadowSlotIndex = static_cast<UINT>(shadowBaseIndex);
			if (shadowSlotIndex >= result.ShadowTransforms.size())
				continue;

			// 记录 cubemap 参数（用于 shader 采样）
			ShadowFrameBuildResult::PointLightCubeParams cubeParams;
			cubeParams.LightIndex = lightIndex;
			cubeParams.ShadowSlotIndex = shadowSlotIndex;
			XMStoreFloat3(&cubeParams.LightPosition, lightPos);
			cubeParams.NearPlane = nearZ;
			cubeParams.FarPlane = farZ;
			result.PointLightCubes.push_back(cubeParams);

			// 生成 6 个渲染 entry（每面一个）
			for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex)
			{
				const XMMATRIX lightView = XMMatrixLookAtLH(lightPos, lightPos + faceDirections[faceIndex], faceUps[faceIndex]);

				ShadowRenderEntry entry;
				entry.LightIndex = lightIndex;
				entry.ShadowSlotIndex = shadowSlotIndex;
				entry.PassCBIndex = 1u + static_cast<UINT>(result.ShadowRenderEntries.size());
				entry.ViewIndex = faceIndex;
				entry.ViewKind = ShadowViewKind::PointFace;
				entry.ProjectionKind = ShadowProjectionKind::Perspective;
				XMStoreFloat4x4(&entry.View, lightView);
				XMStoreFloat4x4(&entry.Proj, lightProj);
				XMStoreFloat4x4(&entry.Transform, lightView * lightProj * textureTransform);
				XMStoreFloat3(&entry.EyePosition, lightPos);
				entry.NearPlane = nearZ;
				entry.FarPlane = farZ;
				entry.ShadowMapSize = shadowConfig.ShadowMapSize;

				result.ShadowRenderEntries.push_back(entry);
			}
		}
		else
		{
			// 聚光灯使用独立的 range / cone 参数。
			// CPU 阴影投影视锥必须和 shader 里的聚光照明锥保持一致，
			// 否则就会出现“照亮了，但阴影投不到正确范围”的现象。
			XMVECTOR lightForward = lightDir;
			if (std::abs(XMVectorGetX(XMVector3Dot(lightForward, lightUp))) > 0.99f)
			{
				XMVECTOR fallbackUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
				if (std::abs(XMVectorGetX(XMVector3Dot(lightForward, fallbackUp))) > 0.99f)
					fallbackUp = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
				lightUp = fallbackUp;
			}

			const float nearZ = 0.1f;
			const float farZ = (std::max)(std::clamp(light.SpotRange, 0.1f, 500.0f), nearZ + 1.0f);
			const float outerHalfAngleDegrees = std::clamp(light.SpotOuterAngleDegrees, 5.0f, 85.0f);
			const float fovY = XMConvertToRadians(outerHalfAngleDegrees * 2.0f);
			const XMVECTOR targetPos = lightPos + lightForward * farZ;
			const XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);
			const XMMATRIX lightProj = XMMatrixPerspectiveFovLH(fovY, 1.0f, nearZ, farZ);

			ShadowRenderEntry entry;
			entry.LightIndex = lightIndex;
			entry.ShadowSlotIndex = static_cast<UINT>(shadowBaseIndex);
			entry.PassCBIndex = 1u + static_cast<UINT>(result.ShadowRenderEntries.size());
			entry.ViewIndex = 0;
			entry.ViewKind = ShadowViewKind::Spot;
			entry.ProjectionKind = ShadowProjectionKind::Perspective;
			XMStoreFloat4x4(&entry.View, lightView);
			XMStoreFloat4x4(&entry.Proj, lightProj);
			XMStoreFloat4x4(&entry.Transform, lightView * lightProj * textureTransform);
			XMStoreFloat3(&entry.EyePosition, lightPos);
			entry.NearPlane = nearZ;
			entry.FarPlane = farZ;
			entry.ShadowMapSize = shadowConfig.ShadowMapSize;

			result.ShadowTransforms[shadowBaseIndex] = entry.Transform;
			result.ShadowRenderEntries.push_back(entry);
		}
	}

	return result;
}

PassConstants ShadowFrameBuilder::BuildShadowPassConstants(
	const ShadowRenderEntry& entry,
	ID3D12Resource* shadowResource,
	UINT defaultShadowMapSize,
	const PassConstants& mainPassCB)
{
	using namespace DirectX;

	PassConstants shadowPassCB = {};
	// 优先读取真实 shadow 资源尺寸。
	// 这样多分辨率级联在写 pass 常量时能保持和实际贴图一致。
	float shadowMapWidth = static_cast<float>((std::max)(defaultShadowMapSize, 1u));
	float shadowMapHeight = shadowMapWidth;
	if (shadowResource != nullptr)
	{
		const D3D12_RESOURCE_DESC shadowDesc = shadowResource->GetDesc();
		shadowMapWidth = static_cast<float>((std::max)(1ull, shadowDesc.Width));
		shadowMapHeight = static_cast<float>((std::max)(1u, static_cast<UINT>(shadowDesc.Height)));
	}

	const XMMATRIX view = XMLoadFloat4x4(&entry.View);
	const XMMATRIX proj = XMLoadFloat4x4(&entry.Proj);
	const XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMVECTOR determinantView = XMMatrixDeterminant(view);
	XMVECTOR determinantProj = XMMatrixDeterminant(proj);
	XMVECTOR determinantViewProj = XMMatrixDeterminant(viewProj);
	const XMMATRIX invView = XMMatrixInverse(&determinantView, view);
	const XMMATRIX invProj = XMMatrixInverse(&determinantProj, proj);
	const XMMATRIX invViewProj = XMMatrixInverse(&determinantViewProj, viewProj);

	XMStoreFloat4x4(&shadowPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&shadowPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&shadowPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&shadowPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&shadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&shadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	shadowPassCB.EyePosW = entry.EyePosition;
	shadowPassCB.RenderTargetSize = XMFLOAT2(shadowMapWidth, shadowMapHeight);
	shadowPassCB.ShadowSettings = mainPassCB.ShadowSettings;
	shadowPassCB.AOSettings = mainPassCB.AOSettings;
	shadowPassCB.LightConst = mainPassCB.LightConst;
	shadowPassCB.DirectionalShadowCascadeSplits = mainPassCB.DirectionalShadowCascadeSplits;
	shadowPassCB.DirectionalShadowCascadeSettings = mainPassCB.DirectionalShadowCascadeSettings;
	shadowPassCB.DirectionalShadowCascadeWorldTexelSize = mainPassCB.DirectionalShadowCascadeWorldTexelSize;
	shadowPassCB.DirectionalShadowCascadeDepthScale = mainPassCB.DirectionalShadowCascadeDepthScale;

	return shadowPassCB;
}
