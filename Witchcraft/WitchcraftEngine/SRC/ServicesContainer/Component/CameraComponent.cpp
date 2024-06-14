#include "CameraComponent.h"
#include "TransformComponent.h"

#include "D3DWindow/D3DWindow.h"

void CameraComponent::SetDXWindow(D3DWindow* dx)
{
	m_dx = dx;
	SetAll();
}

void CameraComponent::SetPresent(bool v)
{
	present = v;

	if (present)
		UpdateAll();
	else
		SetAll();
}

bool CameraComponent::IsPresent()
{
	return present;
}

void CameraComponent::SetFov(float _Fov)
{
	if (_Fov < 0.1f) return;
	if (_Fov > 1.0f) return;

	mFovY = _Fov;
	if(present)
		m_dx->SetFovY(_Fov);
}

float CameraComponent::GetFov()
{
	if (present)
		return m_dx->GetFovY();
	else
		return mFovY;
}

void CameraComponent::SetNear(float _Near)
{
	if (_Near <= 0.0f) return;
	if (_Near < 0.0f) return;
	if (_Near > m_dx->GetFarZ()) return;

	mNearZ = _Near;
	if (present)
		m_dx->SetNearZ(_Near);
}

float CameraComponent::GetNear()
{
	if (present)
		return m_dx->GetNearZ();
	else
		return mNearZ;
}

void CameraComponent::SetFar(float _Far)
{
	if (_Far < m_dx->GetNearZ())
	{
		mFarZ = m_dx->GetNearZ() + 0.01f;
		if (present)
			m_dx->SetFarZ(m_dx->GetNearZ() + 0.01f);
	}
	else
	{
		mFarZ= _Far;
		if (present)
			m_dx->SetFarZ(_Far);
	}
}

float CameraComponent::GetFar()
{
	if (present)
		return m_dx->GetFarZ();
	else
		return mFarZ;
}

void CameraComponent::SetEnabled(bool _Enabled)
{
	enabledComponent = _Enabled;
}

bool CameraComponent::IsEnabled()
{
	return enabledComponent;
}

void CameraComponent::SetScale(float _Scale)
{
	if (_Scale < 0.0f) return;

	mViewportScale = _Scale;
	if (present)
		m_dx->SetViewportScale(_Scale);
}

float CameraComponent::GetScale()
{
	if (present)
		return m_dx->GetViewportScale();
	else
		return mViewportScale;
}

DirectX::XMMATRIX CameraComponent::GetViewMatrix()
{
	if (present)
		return m_dx->GetView();
	else
		return mView;
}

DirectX::XMMATRIX CameraComponent::GetProjectionMatrix()
{
	if (present)
		return m_dx->GetProj();
	else
		return mProj;
}

float CameraComponent::GetProjectionValue()
{
	if (present)
		return m_dx->GetViewportScale();
	else
		return mViewportScale;
}

void CameraComponent::RestoreScale()
{
	if (present)
	{
		m_dx->RestoreScale();
		mViewportScale = m_dx->GetViewportScale();
	}
	else
	{
		float tmp = mViewportScale;
		m_dx->RestoreScale();
		mViewportScale = m_dx->GetViewportScale();
		m_dx->SetViewportScale(tmp);
	}
}

void CameraComponent::SetAll()
{
	// 保存所有参数
	mFovY = m_dx->GetFovY();
	mNearZ = m_dx->GetNearZ();
	mFarZ = m_dx->GetFarZ();
	mViewportScale = m_dx->GetViewportScale();
	mView = m_dx->GetView();
	mProj = m_dx->GetProj();
}

void CameraComponent::UpdateAll()
{
	if (m_dx)
	{
		m_dx->SetFovY(mFovY);
		m_dx->SetNearZ(mNearZ);
		m_dx->SetFarZ(mFarZ);
		m_dx->SetViewportScale(mViewportScale);
	}
}

void CameraComponent::Destroy()
{
}
