#pragma once

#include "../D3DHelpers.h"
#include "../Camera.h"
#include "../Light.h"

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

	// Directional CSM 参数约定：
	// - CascadeShadowMapSizes / FixedRadii 决定近中远三级的清晰度与覆盖范围；
	// - Lambda / MaxDistance 只决定 view-space split，不直接拿来拟合投影盒；
	// - CenterSnap / Smoothing / Hysteresis 只稳定 light-space XY 中心；
	// - OrthographicPadding / DepthPadding 是 receiver/caster 安全边界，不应当被当成滤波半径。
	const UINT DirectionalCascadeCount = 3;
	const std::array<UINT, 3> DirectionalCascadeShadowMapSizes = { 3272u, 2826u, 1536u };
	const float DirectionalCascadeLambda = 0.86f;
	const float DirectionalShadowMaxDistance = 120.0f;
	const float DirectionalCascadeBlendRatio = 0.10f;
	const float DirectionalCascadeReceiverBiasBase = 0.00028f;
	const float DirectionalCascadeReceiverBiasCascadeScale = 0.00f;
	const bool DirectionalCascadeUseFixedRadius = true;
	const std::array<float, 3> DirectionalCascadeFixedRadii = { 16.0f, 48.0f, 160.0f };
	const float DirectionalCascadeCenterSnapTexelScale = 0.5f;
	const std::array<float, 3> DirectionalCascadeCenterSmoothingAlphas = { 0.45f, 0.25f, 0.18f };
	const std::array<float, 3> DirectionalCascadeCenterSmoothingMaxLagTexels = { 16.0f, 24.0f, 32.0f };
	const float DirectionalCascadeCenterHysteresisTexels = 1.0f;
	const float DirectionalCascadeOrthographicPaddingTexels = 6.0f;
	const float DirectionalCascadeDepthPaddingTexels = 64.0f;
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

	// light-space bounds 是为“未来的保守 caster culling”保留的诊断/输入数据。
	// 当前方向光 CSM 不能直接用 receiver 投影盒裁 caster：
	// 位于 receiver XY 外的物体仍可能沿光照方向投影进当前 cascade。
	// 若后续重新启用方向光 caster culling，必须使用 receiver 沿光照方向外扩/挤出的保守体。
	bool HasLightSpaceCullBounds = false;
	DirectX::XMFLOAT3 LightSpaceCullMin = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 LightSpaceCullMax = { 0.0f, 0.0f, 0.0f };
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
	// 方向光 CSM 的跨帧稳定状态。
	// 只保存 light-space XY 中心、raw center 平滑值和投影尺度；
	// 不保存 view/proj 矩阵，避免把阴影构建结果变成隐式缓存。
	// 调用方应在窗口/场景/阴影配置发生大幅变化时让状态自然失效或重新初始化。
	struct StableDirectionalCascadeState
	{
		bool Valid = false;
		bool RawCenterValid = false;
		UINT LightIndex = 0;
		float RawCenterX = 0.0f;
		float RawCenterY = 0.0f;
		float CenterX = 0.0f;
		float CenterY = 0.0f;
		float Radius = 1.0f;
		float WorldUnitsPerTexel = 1.0f;
		DirectX::XMFLOAT3 LightDirection = { 0.0f, -1.0f, 0.0f };
	};

	using StableDirectionalCascadeStates = std::array<StableDirectionalCascadeState, 3>;

	// 生成方向光级联的分割深度。
	// 这里沿用现有的线性/对数混合策略，只把它挪出 D3DWindow 主文件。
	std::array<float, 3> BuildDirectionalCascadeSplits(float nearZ, float farZ, float lambda, UINT cascadeCount);

	// 根据相机、灯光缓存和当前 shadow slot 分配，生成本帧阴影数据。
	// 这里只负责“算出应该画什么、传什么”，不负责真正创建/提交资源。
	ShadowFrameBuildResult BuildShadowFrame(
		const D3DWindowShadowConfig& shadowConfig,
		const Camera& camera,
		const std::vector<Light>& lightsCache,
		const std::vector<int>& lightShadowMapIndices,
		UINT shadowMapCount,
		StableDirectionalCascadeStates* stableDirectionalCascadeStates = nullptr);

	// 为单张 shadow map 生成对应的 PassConstants。
	// 主 pass 已经确定好的阴影/AO/灯光参数会在这里原样继承，保证 shader 侧布局一致。
	PassConstants BuildShadowPassConstants(
		const ShadowRenderEntry& entry,
		ID3D12Resource* shadowResource,
		UINT defaultShadowMapSize,
		const PassConstants& mainPassCB);
}
