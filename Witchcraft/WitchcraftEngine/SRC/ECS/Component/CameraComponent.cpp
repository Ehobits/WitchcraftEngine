#include "CameraComponent.h"

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

void CameraComponent::SetDXWindow(D3DWindow* dx)
{
	m_dx = dx;
}

void CameraComponent::SetFov(float _Fov)
{
	if (_Fov < 0.1f) return;
	if (_Fov > 1.0f) return;

	mFovY = _Fov;
	mProj = BuildProjectionMatrix(mFovY, mViewportScale, mNearZ, mFarZ);
}

float CameraComponent::GetFov()
{
	return mFovY;
}

void CameraComponent::SetNear(float _Near)
{
	if (_Near <= 0.0f) return;
	if (_Near < 0.0f) return;
	if (_Near > mFarZ) return;

	mNearZ = _Near;
	mProj = BuildProjectionMatrix(mFovY, mViewportScale, mNearZ, mFarZ);
}

float CameraComponent::GetNear()
{
	return mNearZ;
}

void CameraComponent::SetFar(float _Far)
{
	if (_Far < mNearZ)
	{
		mFarZ = mNearZ + 0.01f;
	}
	else
	{
		mFarZ = _Far;
	}

	mProj = BuildProjectionMatrix(mFovY, mViewportScale, mNearZ, mFarZ);
}

float CameraComponent::GetFar()
{
	return mFarZ;
}

void CameraComponent::SetScale(float _Scale)
{
	if (_Scale < 0.0f) return;

	mViewportScale = _Scale;
	mProj = BuildProjectionMatrix(mFovY, mViewportScale, mNearZ, mFarZ);
}

float CameraComponent::GetScale()
{
	return mViewportScale;
}

DirectX::XMMATRIX CameraComponent::GetViewMatrix()
{
	return mView;
}

DirectX::XMMATRIX CameraComponent::GetProjectionMatrix()
{
	return mProj;
}

float CameraComponent::GetProjectionValue()
{
	return mViewportScale;
}

void CameraComponent::RestoreScale()
{
	// 恢复到当前编辑器视口默认比例，但不再回写 D3DWindow，
	// 从而保证场景相机参数不会立即影响编辑器相机。
	if (m_dx != nullptr)
		mViewportScale = m_dx->GetViewportScale();
	else
		mViewportScale = 1.0f;

	mProj = BuildProjectionMatrix(mFovY, mViewportScale, mNearZ, mFarZ);
}

void CameraComponent::Destroy()
{
}
