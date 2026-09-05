#include "ShadowFrameBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>

std::array<float, 4> ShadowFrameBuilder::BuildDirectionalCascadeSplits(float nearZ, float farZ, float lambda, UINT cascadeCount)
{
	std::array<float, 4> splits = { farZ, farZ, farZ, farZ };
	nearZ = (std::max)(nearZ, 0.001f);
	farZ = (std::max)(farZ, nearZ + 0.001f);
	lambda = std::clamp(lambda, 0.0f, 1.0f);
	cascadeCount = std::clamp(cascadeCount, 1u, 4u);

	const float clipRange = farZ - nearZ;
	for (UINT cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
	{
		const float p = static_cast<float>(cascadeIndex + 1u) / static_cast<float>(cascadeCount);
		const float linearSplit = nearZ + clipRange * p;
		const float logarithmicSplit = nearZ * std::pow(farZ / nearZ, p);
		splits[cascadeIndex] = linearSplit * (1.0f - lambda) + logarithmicSplit * lambda;
	}

	splits[cascadeCount - 1u] = farZ;
	for (UINT cascadeIndex = cascadeCount; cascadeIndex < 4u; ++cascadeIndex)
		splits[cascadeIndex] = farZ;
	return splits;
}

ShadowFrameBuildResult ShadowFrameBuilder::BuildShadowFrame(
	const D3DWindowShadowConfig& shadowConfig,
	const Camera& camera,
	const std::vector<Light>& lightsCache,
	const std::vector<int>& lightShadowMapIndices,
	UINT shadowMapCount)
{
	using namespace DirectX;

	ShadowFrameBuildResult result;

	// 先沿用当前系统的保守场景包围估计。
	// 目标仍然是“先稳定、先别裁掉”，而不是追求最紧拟合。
	BoundingSphere sceneBounds;
	sceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	sceneBounds.Radius = sqrtf(100.0f * 100.0f + 64.0f * 64.0f);

	const UINT directionalCascadeCount = std::clamp(shadowConfig.DirectionalCascadeCount, 1u, 4u);
	const float directionalCascadeBlendRatio = std::clamp(shadowConfig.DirectionalCascadeBlendRatio, 0.0f, 1.0f);
	const float cameraNearZ = (std::max)(camera.GetNearZ(), 0.001f);
	const float cameraFarZ = (std::max)(camera.GetFarZ(), cameraNearZ + 0.001f);
	const float directionalShadowFarZ = (std::min)(
		cameraFarZ,
		(std::max)(cameraNearZ + 1.0f, shadowConfig.DirectionalShadowMaxDistance));
	const std::array<float, 4> cascadeSplits = BuildDirectionalCascadeSplits(
		cameraNearZ,
		directionalShadowFarZ,
		shadowConfig.DirectionalCascadeLambda,
		directionalCascadeCount);

	result.DirectionalShadowCascadeSplits = XMFLOAT4(
		cascadeSplits[0],
		cascadeSplits[1],
		cascadeSplits[2],
		cascadeSplits[3]);
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
	const XMVECTOR sceneCenter = XMLoadFloat3(&sceneBounds.Center);

	const float cameraAspect = (std::max)(camera.GetViewportScale(), 0.001f);
	const float cameraFovY = (std::max)(camera.GetFovY(), 0.001f);
	const float tanHalfFovY = std::tan(0.5f * cameraFovY);
	const float tanHalfFovX = tanHalfFovY * cameraAspect;
	const DirectX::SimpleMath::Vector3 cameraPosition3 = camera.GetCamPosition();
	const XMFLOAT3 cameraPosition3f(cameraPosition3.x, cameraPosition3.y, cameraPosition3.z);
	const XMVECTOR cameraPosition = XMLoadFloat3(&cameraPosition3f);

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

			// 方向光所有级联共用同一个稳定 light-view，
			// 然后只改变各级联的正交投影尺度。
			// 这和经典“每级联独立拟合 frustum”的 CSM 不同，但更利于稳定。
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
			const XMVECTOR sharedShadowCenterWS = cameraPosition;
			const XMVECTOR stableLightAnchorWS = sceneCenter;
			const XMVECTOR cascadeLightPos = stableLightAnchorWS - lightDir * directionalLightDistance;
			const XMMATRIX lightView = XMMatrixLookAtLH(cascadeLightPos, stableLightAnchorWS, cascadeLightUp);

			XMFLOAT3 shadowCenterLS{};
			XMStoreFloat3(&shadowCenterLS, XMVector3TransformCoord(sharedShadowCenterWS, lightView));

			for (UINT cascadeIndex = 0; cascadeIndex < activeCascadeCount; ++cascadeIndex)
			{
				// 每一级联本质上是“同中心、不同尺度”的阴影副本。
				// 近级联覆盖更小，换取更高的有效分辨率；远级联逐步放大来兜底。
				const float cascadeCoverageDepth =
					(std::max)(cameraNearZ + 0.001f, cascadeSplits[(std::min)(cascadeIndex, 3u)]);
				const float cascadeHalfWidth = cascadeCoverageDepth * tanHalfFovX;
				const float cascadeHalfHeight = cascadeCoverageDepth * tanHalfFovY;
				float shadowCoverageRadius = std::sqrt(
					cascadeHalfWidth * cascadeHalfWidth +
					cascadeHalfHeight * cascadeHalfHeight +
					cascadeCoverageDepth * cascadeCoverageDepth);
				shadowCoverageRadius = (std::max)(shadowCoverageRadius, 1.0f);
				shadowCoverageRadius = std::ceil(shadowCoverageRadius * 16.0f) / 16.0f;

				const float cascadeReferenceShadowMapSize = (std::max)(
					1.0f,
					static_cast<float>(shadowConfig.DirectionalCascadeShadowMapSizes[(std::min)(cascadeIndex, 3u)]));
				float xyExtent = shadowCoverageRadius;

				// worldUnitsPerTexel 是未 padding 前的粗略世界 texel 尺度，
				// projectionWorldUnitsPerTexel 则是最终投影真正落地后的世界 texel 尺度。
				// shader 侧统一滤波半径和 bias 时应以后者为准。
				float worldUnitsPerTexel = (2.0f * xyExtent) / cascadeReferenceShadowMapSize;
				worldUnitsPerTexel = (std::max)(worldUnitsPerTexel, 1e-5f);
				const float orthographicPadding = worldUnitsPerTexel * 4.0f;
				const float depthPadding = worldUnitsPerTexel * 24.0f;
				xyExtent += orthographicPadding;

				const float projectionWorldUnitsPerTexel =
					(std::max)((2.0f * xyExtent) / cascadeReferenceShadowMapSize, 1e-5f);

				// 把共享阴影中心吸附到 texel 网格，减少相机移动时的阴影游走。
				float centerX = shadowCenterLS.x;
				float centerY = shadowCenterLS.y;
				const float centerZ = shadowCenterLS.z;
				centerX = std::floor((centerX / projectionWorldUnitsPerTexel) + 0.8f) * projectionWorldUnitsPerTexel;
				centerY = std::floor((centerY / projectionWorldUnitsPerTexel) + 0.8f) * projectionWorldUnitsPerTexel;

				const float l = centerX - xyExtent;
				const float b = centerY - xyExtent;
				const float stableDepthExtent = shadowCoverageRadius + depthPadding * 2.0f;
				const float n = centerZ - stableDepthExtent;
				const float r = centerX + xyExtent;
				const float t = centerY + xyExtent;
				float f = centerZ + stableDepthExtent;
				if (f <= n + 0.001f)
					f = n + 0.001f;

				// 这两组值专门给 shader 用来统一不同级联的滤波尺度和 receiver bias。
				float* cascadeWorldTexelSizes = &result.DirectionalShadowCascadeWorldTexelSize.x;
				float* cascadeDepthScales = &result.DirectionalShadowCascadeDepthScale.x;
				cascadeWorldTexelSizes[(std::min)(cascadeIndex, 3u)] = projectionWorldUnitsPerTexel;
				cascadeDepthScales[(std::min)(cascadeIndex, 3u)] = 1.0f / (std::max)(f - n, 1e-5f);

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
