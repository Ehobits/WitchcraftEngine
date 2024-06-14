#pragma once

#ifndef CAMERA_H
#define CAMERA_H

#include "D3D12_framework.h"
#include "D3DHelpers.h"

#include "Helpers/Helpers.h"

class Camera
{
public:

	Camera();
	~Camera();

	// 获取/设置相机位置。
	DirectX::XMFLOAT3 GetPosition3f()const;
	void SetPosition(float x, float y, float z);
	void SetPosition(const DirectX::XMFLOAT3& Position);

	// 获取/设置相机旋转。
	DirectX::XMFLOAT3 GetRotation3f()const;
	void SetRotation(DirectX::XMFLOAT3& Rotation);
	void SetRotationZ(float z);

	// 获取视椎体属性。
	float GetNearZ()const;
	float GetFarZ()const;
	float GetFovY()const;
	float GetFovX()const;
	
	// 设置视椎体属性。
	void SetNearZ(float nearZ);
	void SetFarZ(float farZ);
	void SetFovY(float fovY);

	float GetViewportScale()const;

	void SetViewportScale(float scale);

	//设置视锥。
	void SetLens(float fovY, float viewportScale, float zn, float zf);
	// 更新视锥。
	void UpdateLens();
	//设置半径(半径会加上所输入的值)。
	void SetRadius(float r);

	// 通过 LookAt 参数定义相机空间。
	void LookAt(DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR worldUp);
	void LookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);

	// 获取视图/项目矩阵。
	DirectX::XMMATRIX GetView()const;
	DirectX::XMMATRIX GetProj()const;

	DirectX::XMFLOAT4X4 GetProj4x4f()const;

	// 扫射——让相机走一段距离 d．
	void Strafe(float d);

	// 旋转相机
	void RotateCamera(float DeltaTime, DirectX::XMFLOAT2 angle);

	// 移动相机
	void MoveCamera(float DeltaTime, DirectX::XMFLOAT3 distance);
	
	// 修改相机位置/方向后，调用重建视图矩阵。
	void UpdateViewMatrix();

	void CameraUnidirectionalMove(float DeltaTime, UINT MovementDirection);

	DirectX::SimpleMath::Vector3 GetCamPosition();
	DirectX::SimpleMath::Vector3 GetCamTarget();
	DirectX::SimpleMath::Vector3 GetCamUp();

	CameraParameters CameraParame;

private:

	// 具有相对于世界空间的坐标的相机位置和角度。
	DirectX::XMFLOAT3 mPosition = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 mRotation = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3    lPosition = mPosition;
	DirectX::XMFLOAT3    lrotation = mRotation;

	// 相机内部视椎体参数
	float mNearZ = 0.0f;
	float mFarZ = 0.0f;
	float mFovY = 0.0f;
	float mNearWindowHeight = 0.0f;
	float mFarWindowHeight = 0.0f;
	float mViewportScale = 0.0f;
	bool mViewDirty = true; // 用以标记是否需要更新，调用UpdateViewMatrix()。

	// 缓存视图/项目矩阵。
	DirectX::XMMATRIX mView = MathHelps::Identity;
	DirectX::XMFLOAT4X4 mProj = MathHelps::Identity;
	// 辅助矩阵，用以处理相机在全局坐标中的移动和位置信息
	DirectX::XMMATRIX matrix = MathHelps::Identity;
};

#endif // CAMERA_H