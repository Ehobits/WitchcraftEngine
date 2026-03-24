#include "SceneLightSystem.h"

#include "D3DWindow/D3DWindow.h"
#include "D3DWindow/Light.h"
#include "ECS/WitchcraECS.h"
#include "ECS/COMPONENT/LightComponent.h"

#include <algorithm>
#include <DirectXMath.h>

const DirectX::XMFLOAT3& SceneLightSystem::GetLightForwardVector()
{
	static const DirectX::XMFLOAT3 kLightForwardVector = { 0.0f, 0.0f, 1.0f };
	return kLightForwardVector;
}

const DirectX::XMFLOAT3& SceneLightSystem::GetLightUpVector()
{
	static const DirectX::XMFLOAT3 kLightUpVector = { 0.0f, 1.0f, 0.0f };
	return kLightUpVector;
}

float SceneLightSystem::ResolveRuntimeLightType(unsigned int lightKind, float fallbackType)
{
	switch (static_cast<LightKind>(lightKind))
	{
	case LightKind::Directional:
		return kDirectionalShaderLightType;
	case LightKind::Point:
		return kPointShaderLightType;
	case LightKind::Spot:
		return kSpotShaderLightType;
	case LightKind::Ambient:
	default:
		return fallbackType;
	}
}

void SceneLightSystem::AccumulateAmbientLight(const DirectX::XMFLOAT3& lightColor, float lightPower, DirectX::XMFLOAT4& ambientColor)
{
	ambientColor.x = std::clamp(ambientColor.x + lightColor.x * lightPower, 0.0f, 1.0f);
	ambientColor.y = std::clamp(ambientColor.y + lightColor.y * lightPower, 0.0f, 1.0f);
	ambientColor.z = std::clamp(ambientColor.z + lightColor.z * lightPower, 0.0f, 1.0f);
}

DirectX::XMFLOAT3 SceneLightSystem::ResolveLightDirectionFromMatrix(const DirectX::XMFLOAT4X4& worldMatrix)
{
	using namespace DirectX;

	const XMMATRIX rotationMatrix = XMLoadFloat4x4(&worldMatrix);
	const XMFLOAT3 lightForwardVector = GetLightForwardVector();
	XMVECTOR forward = XMLoadFloat3(&lightForwardVector);
	forward = XMVector3Normalize(XMVector3TransformNormal(forward, rotationMatrix));
	if (XMVector3Equal(forward, XMVectorZero()))
		return GetLightForwardVector();

	XMFLOAT3 resolvedDirection{};
	XMStoreFloat3(&resolvedDirection, forward);
	return resolvedDirection;
}

DirectX::XMFLOAT3 SceneLightSystem::ResolveLightUpFromMatrix(const DirectX::XMFLOAT4X4& worldMatrix)
{
	using namespace DirectX;

	const XMMATRIX rotationMatrix = XMLoadFloat4x4(&worldMatrix);
	const XMFLOAT3 lightUpVector = GetLightUpVector();
	XMVECTOR up = XMLoadFloat3(&lightUpVector);
	up = XMVector3Normalize(XMVector3TransformNormal(up, rotationMatrix));
	if (XMVector3Equal(up, XMVectorZero()))
		return GetLightUpVector();

	XMFLOAT3 resolvedUp{};
	XMStoreFloat3(&resolvedUp, up);
	return resolvedUp;
}

void SceneLightSystem::AppendEntityLightRecursive(const WitchcraECS& ecs, SceneEntityBase* entity, D3DWindow* dx, DirectX::XMFLOAT4& ambientColor) const
{
	if (entity == nullptr || dx == nullptr)
		return;

	if (ecs.IsEntityVisible(entity))
	{
		EntityLightComponentData lightData;
		if (ecs.GetEntityLightSnapshot(entity, &lightData))
		{
			const LightKind lightKind = static_cast<LightKind>(lightData.kind);
			if (lightKind == LightKind::Ambient)
			{
				AccumulateAmbientLight(lightData.color, lightData.power, ambientColor);
			}
			else
			{
				Transform worldTransform{};
				DirectX::XMFLOAT4X4 worldMatrix{};
				if (ecs.GetEntityWorldTransform(entity, &worldTransform) &&
					ecs.GetEntityWorldMatrix(entity, &worldMatrix))
				{
					const std::wstring runtimeLightName = ecs.GetEntityName(entity) + L"_" + std::to_wstring(static_cast<unsigned long long>(entity->entity));
					Light runtimeLight(runtimeLightName);
					runtimeLight.Type = ResolveRuntimeLightType(lightData.kind, lightData.type);
					runtimeLight.Color = lightData.color;
					runtimeLight.Direction = ResolveLightDirectionFromMatrix(worldMatrix);
					runtimeLight.Up = ResolveLightUpFromMatrix(worldMatrix);
					runtimeLight.Position = worldTransform.position;
					runtimeLight.Power = lightData.power;
					runtimeLight.CastShadow = lightData.castShadow;
					dx->AddLight(&runtimeLight);
				}
			}
		}
	}

	for (SceneEntityBase* childEntity : ecs.GetSceneChildren(entity))
		AppendEntityLightRecursive(ecs, childEntity, dx, ambientColor);
}

void SceneLightSystem::SyncSceneLights(WitchcraECS* ecs, D3DWindow* dx)
{
	if (ecs == nullptr || dx == nullptr)
		return;

	DirectX::XMFLOAT4 ambientColor = m_defaultAmbientColor;
	dx->ClearLights();

	for (SceneEntityBase* rootEntity : ecs->GetSceneRootEntities())
		AppendEntityLightRecursive(*ecs, rootEntity, dx, ambientColor);

	dx->SetAmbientColor(ambientColor);
}
