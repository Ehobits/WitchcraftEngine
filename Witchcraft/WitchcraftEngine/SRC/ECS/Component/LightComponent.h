#pragma once

#include "BaseComponent.h"
#include "D3DWindow/D3D12_framework.h"

enum class LightKind : unsigned int
{
	Ambient = 0,
	Directional,
	Spot,
	Point
};

class WitchcraECS;
class SceneEntityBase;
struct EntityLightComponentData;

class LightComponent : public BaseComponent
{
public:
	void BindEntity(WitchcraECS* ecs, SceneEntityBase* ownerEntity);

	void SetKind(LightKind kind);
	LightKind GetKind() const;

	void SetType(float type);
	float GetType() const;

	void SetColor(const DirectX::XMFLOAT3& color);
	DirectX::XMFLOAT3 GetColor() const;

	void SetPower(float power);
	float GetPower() const;

	void SetCastShadow(bool castShadow);
	bool GetCastShadow() const;

	ComponentType GetComponentType() override { return ComponentType::Co_Unk; }

private:
	bool TryGetSnapshot(EntityLightComponentData* outSnapshot) const;
	bool TrySetSnapshot(const EntityLightComponentData& snapshot);

	WitchcraECS* mEcs = nullptr;
	SceneEntityBase* mOwnerEntity = nullptr;
};
