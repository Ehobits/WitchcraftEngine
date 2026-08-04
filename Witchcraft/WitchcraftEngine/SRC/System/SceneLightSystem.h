#pragma once

#include "D3DWindow/D3D12_framework.h"

class D3DWindow;
class SceneEntityBase;
class WitchcraECS;

class SceneLightSystem
{
public:
	void SyncSceneLights(WitchcraECS* ecs, D3DWindow* dx);

private:
	void AppendEntityLightRecursive(const WitchcraECS& ecs, SceneEntityBase* entity, D3DWindow* dx, DirectX::XMFLOAT4& ambientColor) const;
	static float ResolveLightType(unsigned int lightKind, float fallbackType);
	static void AccumulateAmbientLight(const DirectX::XMFLOAT3& lightColor, float lightPower, DirectX::XMFLOAT4& ambientColor);
	static DirectX::XMFLOAT3 ResolveLightDirectionFromMatrix(const DirectX::XMFLOAT4X4& worldMatrix);
	static DirectX::XMFLOAT3 ResolveLightUpFromMatrix(const DirectX::XMFLOAT4X4& worldMatrix);
	static const DirectX::XMFLOAT3& GetLightForwardVector();
	static const DirectX::XMFLOAT3& GetLightUpVector();

private:
	static constexpr float kDirectionalShaderLightType = 0.0f;
	static constexpr float kPointShaderLightType = 1.0f;
	static constexpr float kSpotShaderLightType = 2.0f;
	const DirectX::XMFLOAT4 m_defaultAmbientColor = { 0.0f, 0.0f, 0.0f, 0.0f };
};
