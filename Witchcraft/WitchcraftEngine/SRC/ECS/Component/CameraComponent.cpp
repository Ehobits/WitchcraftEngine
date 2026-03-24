#include "CameraComponent.h"

#include "ECS/WitchcraECS.h"
#include "Engine/Engine.h"
#include "D3DWindow/D3DWindow.h"

namespace
{
	DirectX::XMMATRIX BuildProjectionMatrix(float fovY, float viewportScale, float nearZ, float farZ)
	{
		const float safeScale = viewportScale <= 0.0f ? 1.0f : viewportScale;
		const float safeNearZ = nearZ <= 0.0f ? 0.001f : nearZ;
		const float safeFarZ = farZ <= safeNearZ ? (safeNearZ + 0.01f) : farZ;
		return DirectX::XMMatrixPerspectiveFovLH(fovY, safeScale, safeNearZ, safeFarZ);
	}
}

void CameraComponent::BindEntity(WitchcraECS* ecs, SceneEntityBase* ownerEntity)
{
	mEcs = ecs;
	mOwnerEntity = ownerEntity;
}

bool CameraComponent::TryGetSnapshot(EntityCameraComponentData* outSnapshot) const
{
	if (outSnapshot == nullptr)
		return false;

	if (mEcs == nullptr || mOwnerEntity == nullptr || !mEcs->HasEntity(mOwnerEntity))
		return false;

	if (!mEcs->GetEntityCameraFov(mOwnerEntity, &outSnapshot->fovY))
		return false;
	if (!mEcs->GetEntityCameraNear(mOwnerEntity, &outSnapshot->nearZ))
		return false;
	if (!mEcs->GetEntityCameraFar(mOwnerEntity, &outSnapshot->farZ))
		return false;
	if (!mEcs->GetEntityCameraScale(mOwnerEntity, &outSnapshot->viewportScale))
		return false;

	return true;
}

void CameraComponent::SetEngine(Engine* engine)
{
	m_engine = engine;
}

Engine* CameraComponent::GetEngine() const
{
	return m_engine;
}

void CameraComponent::CopySettingsFrom(const CameraComponent& other)
{
	SetFov(other.GetFov());
	SetNear(other.GetNear());
	SetFar(other.GetFar());
	SetScale(other.GetScale());
}

void CameraComponent::SetFov(float _Fov)
{
	if (mEcs == nullptr || mOwnerEntity == nullptr)
		return;

	mEcs->SetEntityCameraFov(mOwnerEntity, _Fov);
}

float CameraComponent::GetFov() const
{
	EntityCameraComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return snapshot.fovY;
}

void CameraComponent::SetNear(float _Near)
{
	if (mEcs == nullptr || mOwnerEntity == nullptr)
		return;

	mEcs->SetEntityCameraNear(mOwnerEntity, _Near);
}

float CameraComponent::GetNear() const
{
	EntityCameraComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return snapshot.nearZ;
}

void CameraComponent::SetFar(float _Far)
{
	if (mEcs == nullptr || mOwnerEntity == nullptr)
		return;

	mEcs->SetEntityCameraFar(mOwnerEntity, _Far);
}

float CameraComponent::GetFar() const
{
	EntityCameraComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return snapshot.farZ;
}

void CameraComponent::SetScale(float _Scale)
{
	if (mEcs == nullptr || mOwnerEntity == nullptr)
		return;

	mEcs->SetEntityCameraScale(mOwnerEntity, _Scale);
}

float CameraComponent::GetScale() const
{
	EntityCameraComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return snapshot.viewportScale;
}

DirectX::XMMATRIX CameraComponent::GetViewMatrix() const
{
	return mView;
}

DirectX::XMMATRIX CameraComponent::GetProjectionMatrix() const
{
	EntityCameraComponentData snapshot;
	TryGetSnapshot(&snapshot);
	return BuildProjectionMatrix(snapshot.fovY, snapshot.viewportScale, snapshot.nearZ, snapshot.farZ);
}

float CameraComponent::GetProjectionValue() const
{
	return GetScale();
}

void CameraComponent::RestoreScale()
{
	D3DWindow* dx = m_engine != nullptr ? m_engine->GetD3DWindow() : nullptr;
	const float viewportScale = dx != nullptr ? dx->GetViewportScale() : 1.0f;
	SetScale(viewportScale);
}

void CameraComponent::Destroy()
{
}
