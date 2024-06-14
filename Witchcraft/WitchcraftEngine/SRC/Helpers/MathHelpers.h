#pragma once

#include <Windows.h>

#include <DirectXMath.h>

#include <SimpleMath.h>

namespace MathHelps
{
	using namespace DirectX::SimpleMath;

	const float Pi = 3.1415926535f;

	static const Matrix Identity = { 1.f, 0.f, 0.f, 0.f,
											  0.f, 1.f, 0.f, 0.f,
											  0.f, 0.f, 1.f, 0.f,
											  0.f, 0.f, 0.f, 1.f };

	/* --------------------- */
	DirectX::XMFLOAT3 ToRadians(DirectX::XMFLOAT3 rotation);
	DirectX::XMFLOAT3 ToDegrees(DirectX::XMFLOAT3 rotation);
	float RadToDeg(float value);
	float DegToRad(float value);
	//physx::PxVec3 vector3_to_physics(DirectX::XMFLOAT3 value);
	//physx::PxQuat quat_to_physics(DirectX::XMFLOAT4 value);
	//DirectX::XMFLOAT3 physics_to_vector3(physx::PxVec3 value);
	//DirectX::XMFLOAT4 physics_to_quat(physx::PxQuat value);
	//physx::PxMat44 matrix_to_physics(DirectX::XMFLOAT4X4 value);
	//DirectX::XMFLOAT4X4 physics_to_matrix(physx::PxMat44 value);
	//physx::PxTransform position_rotation_to_physics(DirectX::XMFLOAT3 position, DirectX::XMFLOAT4 rotation);

}