#pragma once

#include "D3DHelpers.h"
#include "Camera.h"
#include "Light.h"

#include <array>
#include <vector>

// 阴影系统运行时配置。
// 这里保留“D3DWindow 当前正在使用的那一套参数定义”，
// 但把纯配置结构挪到 helper 里，避免主窗口类继续膨胀。
struct D3DWindowShadowConfig
{
	const float DirectionalLightType = 0.0f;
	const float PointLightType = 1.0f;
	const float SpotLightType = 2.0f;
	const float DefaultOpacity = 0.65f;
	const float DefaultSoftness = 1.5f;
	const float MinOpacity = 0.0f;
	const float MaxOpacity = 1.0f;
	const float MinSoftness = 0.0f;
	const float MaxSoftness = 4.0f;
	const UINT ShadowMapSize = 4096;
	const UINT MaxShadowMapCount = 256;
	const UINT DirectionalCascadeCount = 3;
	const std::array<UINT, 4> DirectionalCascadeShadowMapSizes = { 8192u, 2048u, 1024u, 1024u };
	const float DirectionalCascadeLambda = 0.65f;
	const float DirectionalShadowMaxDistance = 100000.0f;
	const float DirectionalCascadeBlendRatio = 0.45f;
	const float DirectionalCascadeReceiverBiasBase = 0.00028f;
	const float DirectionalCascadeReceiverBiasCascadeScale = 0.00f;
	const DirectX::XMFLOAT3 FallbackDirection = { 0.57735f, -0.57735f, 0.57735f };
	const DirectX::XMFLOAT3 FallbackUp = { 0.0f, 1.0f, 0.0f };
};

enum class ShadowProjectionKind : UINT8
{
	None = 0,
	Orthographic,
	Perspective
};

enum class ShadowViewKind : UINT8
{
	None = 0,
	DirectionalCascade,
	Spot,
	PointFace
};

// 一次阴影视图渲染任务描述。
// 它描述“这一帧需要为哪盏灯、哪个阴影视图、渲染到哪个资源里”，
// 而不是继续沿用旧的扁平 shadow slot 语义。
struct ShadowRenderEntry
{
	UINT LightIndex = 0;
	UINT ShadowSlotIndex = 0;       // 工作阴影池逻辑槽位：Directional/Spot 为 2D 槽位，Point 为 cube 槽位基址
	UINT PassCBIndex = 0;           // 对应 FrameResource::PassCB 的独立槽位
	UINT ViewIndex = 0;             // 方向光=级联索引，点光=面索引，聚光=0
	ShadowViewKind ViewKind = ShadowViewKind::None;
	ShadowProjectionKind ProjectionKind = ShadowProjectionKind::None;
	DirectX::XMFLOAT4X4 View = MathHelps::Identity;
	DirectX::XMFLOAT4X4 Proj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 Transform = MathHelps::Identity;
	DirectX::XMFLOAT3 EyePosition = { 0.0f, 0.0f, 0.0f };
	float NearPlane = 0.1f;
	float FarPlane = 100.0f;
	UINT ShadowMapSize = 0;
};

// 一帧阴影构建结果。
// 前四个字段会直接回填到 MainPassCB，后两组数组则驱动阴影 pass 录制。
struct ShadowFrameBuildResult
{
	DirectX::XMFLOAT4 DirectionalShadowCascadeSplits = { 8.0f, 24.0f, 72.0f, 256.0f };
	DirectX::XMFLOAT4 DirectionalShadowCascadeSettings = { 4.0f, 0.08f, 0.0f, 0.0f };
	DirectX::XMFLOAT4 DirectionalShadowCascadeWorldTexelSize = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT4 DirectionalShadowCascadeDepthScale = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::vector<DirectX::XMFLOAT4X4> ShadowTransforms;
	std::vector<ShadowRenderEntry> ShadowRenderEntries;
	
	// 点光源 cubemap 参数
	// 每个 cubemap 需要：光源位置、far plane、起始槽位
	struct PointLightCubeParams
	{
		UINT LightIndex = 0;
		UINT ShadowSlotIndex = 0;
		DirectX::XMFLOAT3 LightPosition = { 0.0f, 0.0f, 0.0f };
		float NearPlane = 0.1f;
		float FarPlane = 100.0f;
	};
	std::vector<PointLightCubeParams> PointLightCubes;
};

namespace ShadowFrameBuilder
{
	// 生成方向光级联的分割深度。
	// 这里沿用现有的线性/对数混合策略，只把它挪出 D3DWindow 主文件。
	std::array<float, 4> BuildDirectionalCascadeSplits(float nearZ, float farZ, float lambda, UINT cascadeCount);

	// 根据相机、灯光缓存和当前 shadow slot 分配，生成本帧阴影数据。
	// 这里只负责“算出应该画什么、传什么”，不负责真正创建/提交资源。
	ShadowFrameBuildResult BuildShadowFrame(
		const D3DWindowShadowConfig& shadowConfig,
		const Camera& camera,
		const std::vector<Light>& lightsCache,
		const std::vector<int>& lightShadowMapIndices,
		UINT shadowMapCount);

	// 为单张 shadow map 生成对应的 PassConstants。
	// 主 pass 已经确定好的阴影/AO/灯光参数会在这里原样继承，保证 shader 侧布局一致。
	PassConstants BuildShadowPassConstants(
		const ShadowRenderEntry& entry,
		ID3D12Resource* shadowResource,
		UINT defaultShadowMapSize,
		const PassConstants& mainPassCB);
}
