#include "LightComponent.h"

#include "ECS/WitchcraECS.h"
#include <algorithm>

void LightComponent::BindEntity(WitchcraECS* ecs, SceneEntityBase* ownerEntity)
{
	mEcs = ecs;
	mOwnerEntity = ownerEntity;
}

bool LightComponent::TryGetSnapshot(EntityLightComponentData* outSnapshot) const
{
	if (outSnapshot == nullptr)
		return false;

	if (mEcs == nullptr || mOwnerEntity == nullptr || !mEcs->HasEntity(mOwnerEntity))
		return false;

	return mEcs->GetEntityLightSnapshot(mOwnerEntity, outSnapshot);
}

bool LightComponent::TrySetSnapshot(const EntityLightComponentData& snapshot)
{
	if (mEcs == nullptr || mOwnerEntity == nullptr || !mEcs->HasEntity(mOwnerEntity))
		return false;

	return mEcs->SetEntityLightSnapshot(mOwnerEntity, snapshot);
}

void LightComponent::SetKind(LightKind kind)
{
	EntityLightComponentData snapshot;
	if (!TryGetSnapshot(&snapshot))
		return;

	const LightKind previousKind = static_cast<LightKind>(snapshot.kind);
	// 禁止把普通灯切换为环境光，环境光由环境系统创建并维护。
	if (kind == LightKind::Ambient && previousKind != LightKind::Ambient)
		return;

	snapshot.kind = static_cast<std::uint32_t>(kind);
	if (kind != LightKind::Directional && kind != LightKind::Spot && kind != LightKind::Point)
		snapshot.castShadow = false;
	TrySetSnapshot(snapshot);
}

LightKind LightComponent::GetKind() const
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return static_cast<LightKind>(snapshot.kind);
}

void LightComponent::SetType(float type)
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	snapshot.type = type;
	TrySetSnapshot(snapshot);
}

float LightComponent::GetType() const
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return snapshot.type;
}

void LightComponent::SetColor(const DirectX::XMFLOAT3& color)
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	snapshot.color = color;
	TrySetSnapshot(snapshot);
}

DirectX::XMFLOAT3 LightComponent::GetColor() const
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return snapshot.color;
}

void LightComponent::SetPower(float power)
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	snapshot.power = power;
	TrySetSnapshot(snapshot);
}

float LightComponent::GetPower() const
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return snapshot.power;
}

void LightComponent::SetCastShadow(bool castShadow)
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	const LightKind kind = static_cast<LightKind>(snapshot.kind);
	snapshot.castShadow = (kind == LightKind::Directional || kind == LightKind::Spot || kind == LightKind::Point) && castShadow;
	TrySetSnapshot(snapshot);
}

bool LightComponent::GetCastShadow() const
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	const LightKind kind = static_cast<LightKind>(snapshot.kind);
	return (kind == LightKind::Directional || kind == LightKind::Spot || kind == LightKind::Point) && snapshot.castShadow;
}

void LightComponent::SetEnableVolumetric(bool enableVolumetric)
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	const LightKind kind = static_cast<LightKind>(snapshot.kind);
	snapshot.enableVolumetric =
		(kind == LightKind::Directional || kind == LightKind::Spot || kind == LightKind::Point) &&
		enableVolumetric;
	TrySetSnapshot(snapshot);
}

bool LightComponent::GetEnableVolumetric() const
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	const LightKind kind = static_cast<LightKind>(snapshot.kind);
	return (kind == LightKind::Directional || kind == LightKind::Spot || kind == LightKind::Point) &&
		snapshot.enableVolumetric;
}

void LightComponent::SetVolumetricIntensity(float intensity)
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	const LightKind kind = static_cast<LightKind>(snapshot.kind);
	if (kind != LightKind::Directional && kind != LightKind::Spot && kind != LightKind::Point)
		return;

	snapshot.volumetricIntensity = std::clamp(intensity, 0.0f, 8.0f);
	TrySetSnapshot(snapshot);
}

float LightComponent::GetVolumetricIntensity() const
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	const LightKind kind = static_cast<LightKind>(snapshot.kind);
	if (kind != LightKind::Directional && kind != LightKind::Spot && kind != LightKind::Point)
		return 0.0f;

	return std::clamp(snapshot.volumetricIntensity, 0.0f, 8.0f);
}

void LightComponent::SetVolumetricAttenuationDistance(float distance)
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	const LightKind kind = static_cast<LightKind>(snapshot.kind);
	if (kind != LightKind::Directional && kind != LightKind::Spot && kind != LightKind::Point)
		return;

	snapshot.volumetricAttenuationDistance = std::clamp(distance, 0.1f, 500.0f);
	TrySetSnapshot(snapshot);
}

float LightComponent::GetVolumetricAttenuationDistance() const
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	const LightKind kind = static_cast<LightKind>(snapshot.kind);
	if (kind != LightKind::Directional && kind != LightKind::Spot && kind != LightKind::Point)
		return 0.0f;

	return std::clamp(snapshot.volumetricAttenuationDistance, 0.1f, 500.0f);
}
