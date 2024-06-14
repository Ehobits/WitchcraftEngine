
#include "Camera.h"

using namespace DirectX;

Camera::Camera()
{
	SetLens(0.25f * MathHelps::Pi, 1.0f, 1.0f, 1000.0f);
}

Camera::~Camera()
{
}

XMFLOAT3 Camera::GetPosition3f()const
{
	return mPosition;
}

void Camera::SetPosition(float x, float y, float z)
{
	mPosition = XMFLOAT3(x, y, z);
	mViewDirty = true;
}

void Camera::SetPosition(const XMFLOAT3& Position)
{
	mPosition = Position;
	mViewDirty = true;
}

float Camera::GetNearZ()const
{
	return mNearZ;
}

float Camera::GetFarZ()const
{
	return mFarZ;
}

float Camera::GetFovY()const
{
	return mFovY;
}

float Camera::GetFovX()const
{
	float halfWidth = 0.5f* mNearWindowHeight;
	return 2.0f*atan(halfWidth / mNearZ);
}

void Camera::SetNearZ(float nearZ)
{
	mNearZ = nearZ;
	UpdateLens();
}

void Camera::SetFarZ(float farZ)
{
	mFarZ = farZ;
	UpdateLens();
}

void Camera::SetFovY(float fovY)
{
	mFovY = fovY;
	UpdateLens();
}

float Camera::GetViewportScale()const
{
	return mViewportScale;
}

void Camera::SetViewportScale(float scale)
{
	mViewportScale = scale;
	UpdateLens();
}

void Camera::SetLens(float fovY, float viewportScale, float zn, float zf)
{
	// cache properties
	mFovY = fovY;
	mNearZ = zn;
	mFarZ = zf;
	mViewportScale = viewportScale;

	UpdateLens();
}

void Camera::UpdateLens()
{
	mNearWindowHeight = 2.0f * mNearZ * tanf(0.5f * mFovY);
	mFarWindowHeight = 2.0f * mFarZ * tanf(0.5f * mFovY);

	XMMATRIX P = XMMatrixPerspectiveFovLH(mFovY, mViewportScale, mNearZ, mFarZ);
	XMStoreFloat4x4(&mProj, P);

	mViewDirty = true;
}

void Camera::SetRadius(float r)
{
	mPosition.z = r;

	mViewDirty = true;
}

void Camera::SetRotation(DirectX::XMFLOAT3& Rotation)
{
	lrotation.x = Rotation.x;
	lrotation.y = Rotation.y;
	lrotation.z = Rotation.z;

	mRotation = Rotation;

	mViewDirty = true;
}

void Camera::SetRotationZ(float z)
{
	mRotation.z = z;

	mViewDirty = true;
}

DirectX::XMFLOAT3 Camera::GetRotation3f()const
{
	return mRotation;
}

void Camera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
	XMVECTOR L = XMVector3Normalize(XMVectorSubtract(target, pos));
	XMVECTOR R = XMVector3Normalize(XMVector3Cross(worldUp, L));
	XMVECTOR U = XMVector3Cross(L, R);

	XMStoreFloat3(&mPosition, pos);
	XMStoreFloat3(&mRotation, R);

	mViewDirty = true;
}

void Camera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
	XMVECTOR P = XMLoadFloat3(&pos);
	XMVECTOR T = XMLoadFloat3(&target);
	XMVECTOR U = XMLoadFloat3(&up);

	LookAt(P, T, U);

	mViewDirty = true;
}

XMMATRIX Camera::GetView()const
{
	return mView;
}

XMMATRIX Camera::GetProj()const
{
	return XMLoadFloat4x4(&mProj);
}

XMFLOAT4X4 Camera::GetProj4x4f()const
{
	return mProj;
}

void Camera::Strafe(float d)
{
	// mPosition += d*mRight
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR r = XMLoadFloat3(&mRotation);
	XMVECTOR p = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, r, p));

	mViewDirty = true;
}

void Camera::RotateCamera(float DeltaTime, DirectX::XMFLOAT2 angle)
{
	angle.x = angle.x * (CameraParame.camSpeed) * DeltaTime;
	angle.y = angle.y * (CameraParame.camSpeed) * DeltaTime;
	lrotation.x += angle.y;
	lrotation.y += angle.x;
	mRotation = DirectX::SimpleMath::Vector3::Lerp(mRotation, lrotation, 16.0f * DeltaTime);
	
	mViewDirty = true;
}

void Camera::MoveCamera(float DeltaTime, DirectX::XMFLOAT3 distance)
{
	if (distance.x != 0)
		mPosition = mPosition + distance.x * DirectX::SimpleMath::Matrix(matrix).Right() *
		CameraParame.camSpeed * DeltaTime;
	if (distance.y != 0)
		mPosition = mPosition + distance.y * DirectX::SimpleMath::Matrix(matrix).Up() *
		CameraParame.camSpeed * DeltaTime;
	if(distance.z != 0)
		mPosition = mPosition + distance.z * DirectX::SimpleMath::Matrix(matrix).Backward() *
		CameraParame.camSpeed * DeltaTime;

	mViewDirty = true;
}

void Camera::UpdateViewMatrix()
{
	if(mViewDirty)
	{
		DirectX::XMFLOAT3 pRotation;
		pRotation.x = mRotation.x / (MathHelps::Pi * 18.24f);
		pRotation.y = mRotation.y / (MathHelps::Pi * 18.24f);
		pRotation.z = mRotation.z / (MathHelps::Pi * 18.24f);
		matrix = DirectX::SimpleMath::Matrix::Identity;
		matrix = DirectX::SimpleMath::Matrix::CreateTranslation(mPosition) * matrix;
		matrix = DirectX::SimpleMath::Matrix::CreateFromQuaternion(DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(pRotation)) * matrix;
		mView = XMMatrixLookAtLH(GetCamPosition(), GetCamPosition() + GetCamTarget(), GetCamUp());

		mViewDirty = false;
	}

}

void Camera::CameraUnidirectionalMove(float DeltaTime, UINT MovementDirection)
{
	if (MovementDirection == MOVE_UP)
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Up() *
		CameraParame.camSpeed * DeltaTime;
	else if (MovementDirection == MOVE_DOWN)
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Up() *
		CameraParame.camSpeed * DeltaTime;
	else if (MovementDirection == MOVE_DEEPEN)
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Backward() *
		CameraParame.camSpeed * DeltaTime;
	else if (MovementDirection == MOVE_FROMAW)
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Backward() *
		CameraParame.camSpeed * DeltaTime;
	else if (MovementDirection == MOVE_LEFT)
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Right() *
		CameraParame.camSpeed * DeltaTime;
	else if (MovementDirection == MOVE_RIGHT)
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Right() *
		CameraParame.camSpeed * DeltaTime;
	else if (MovementDirection == (MOVE_UP + MOVE_LEFT))
	{
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Up() *
			CameraParame.camSpeed * DeltaTime;
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Right() *
			CameraParame.camSpeed * DeltaTime;
	}
	else if (MovementDirection == (MOVE_UP + MOVE_RIGHT))
	{
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Up() *
			CameraParame.camSpeed * DeltaTime;
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Right() *
			CameraParame.camSpeed * DeltaTime;
	}
	else if (MovementDirection == (MOVE_DOWN + MOVE_LEFT))
	{
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Up() *
			CameraParame.camSpeed * DeltaTime;
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Right() *
			CameraParame.camSpeed * DeltaTime;
	}
	else if (MovementDirection == (MOVE_DOWN + MOVE_RIGHT))
	{
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Up() *
			CameraParame.camSpeed * DeltaTime;
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Right() *
			CameraParame.camSpeed * DeltaTime;
	}
	else if (MovementDirection == (MOVE_LEFT + MOVE_DEEPEN))
	{
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Right() *
			CameraParame.camSpeed * DeltaTime;
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Backward() *
			CameraParame.camSpeed * DeltaTime;
	}
	else if (MovementDirection == (MOVE_LEFT + MOVE_FROMAW))
	{
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Right() *
			CameraParame.camSpeed * DeltaTime;
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Backward() *
			CameraParame.camSpeed * DeltaTime;
	}
	else if (MovementDirection == (MOVE_RIGHT + MOVE_DEEPEN))
	{
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Right() *
			CameraParame.camSpeed * DeltaTime;
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Backward() *
			CameraParame.camSpeed * DeltaTime;
	}
	else if (MovementDirection == (MOVE_RIGHT + MOVE_FROMAW))
	{
		mPosition = mPosition + DirectX::SimpleMath::Matrix(matrix).Right() *
			CameraParame.camSpeed * DeltaTime;
		mPosition = mPosition - DirectX::SimpleMath::Matrix(matrix).Backward() *
			CameraParame.camSpeed * DeltaTime;
	}

	mViewDirty = true;
}

DirectX::SimpleMath::Vector3 Camera::GetCamPosition()
{
	return DirectX::SimpleMath::Matrix(matrix).Translation();
}

DirectX::SimpleMath::Vector3 Camera::GetCamTarget()
{
	return DirectX::SimpleMath::Matrix(matrix).Backward();
}

DirectX::SimpleMath::Vector3 Camera::GetCamUp()
{
	return DirectX::SimpleMath::Matrix(matrix).Up();
}
