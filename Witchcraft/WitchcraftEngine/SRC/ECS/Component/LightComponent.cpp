#include "LightComponent.h"

#include "ECS/WitchcraECS.h"

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
	TryGetSnapshot(&snapshot);
	snapshot.kind = static_cast<std::uint32_t>(kind);
	if (kind == LightKind::Ambient)
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
	snapshot.castShadow = kind != LightKind::Ambient && castShadow;
	TrySetSnapshot(snapshot);
}

bool LightComponent::GetCastShadow() const
{
	EntityLightComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return static_cast<LightKind>(snapshot.kind) != LightKind::Ambient && snapshot.castShadow;
}
