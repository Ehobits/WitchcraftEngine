#pragma once

#include "BaseComponent.h"
#include "HELPERS/Helpers.h"
#include "Engine/EngineUtils.h"

#define MIN_FOV 1.0f
#define MAX_FOV 256.0f

class Engine;
class WitchcraECS;
class SceneEntityBase;
struct EntityCameraComponentData;

class CameraComponent : public BaseComponent
{
public:
	void BindEntity(WitchcraECS* ecs, SceneEntityBase* ownerEntity);

	void SetEngine(Engine* engine);
	Engine* GetEngine() const;
	void CopySettingsFrom(const CameraComponent& other);

	void SetFov(float _Fov);
	float GetFov() const;
	void SetNear(float _Near);
	float GetNear() const;
	void SetFar(float _Far);
	float GetFar() const;
	void SetScale(float _Scale);
	float GetScale() const;
	DirectX::XMMATRIX GetViewMatrix() const;
	DirectX::XMMATRIX GetProjectionMatrix() const;
	float GetProjectionValue() const;

	void RestoreScale();

	void Destroy() override;

	virtual ComponentType GetComponentType() { return mComponentType; }

private:
	bool TryGetSnapshot(EntityCameraComponentData* outSnapshot) const;

	WitchcraECS* mEcs = nullptr;
	SceneEntityBase* mOwnerEntity = nullptr;
	Engine* m_engine = nullptr;
	DirectX::XMMATRIX mView = MathHelps::Identity;

private:
	ComponentType mComponentType = ComponentType::Co_Camera;
};
