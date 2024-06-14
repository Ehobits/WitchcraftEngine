#include "MathHelpers.h"

#include "D3DWindow/D3DWindow.h"

DirectX::XMFLOAT3 MathHelps::ToRadians(DirectX::XMFLOAT3 rotation)
{
	return DirectX::XMFLOAT3(
		DirectX::XMConvertToRadians(rotation.x),
		DirectX::XMConvertToRadians(rotation.y),
		DirectX::XMConvertToRadians(rotation.z));
}

DirectX::XMFLOAT3 MathHelps::ToDegrees(DirectX::XMFLOAT3 rotation)
{
	return DirectX::XMFLOAT3(
		DirectX::XMConvertToDegrees(rotation.x),
		DirectX::XMConvertToDegrees(rotation.y),
		DirectX::XMConvertToDegrees(rotation.z));
}

//physx::PxVec3 MathHelps::vector3_to_physics(DirectX::XMFLOAT3 value)
//{
//	return physx::PxVec3(value.x, value.y, value.z);
//}
//
//physx::PxQuat MathHelps::quat_to_physics(DirectX::XMFLOAT4 value)
//{
//	return physx::PxQuat(value.x, value.y, value.z, value.w);
//}
//
//DirectX::XMFLOAT3 MathHelps::physics_to_vector3(physx::PxVec3 value)
//{
//	return DirectX::XMFLOAT3(value.x, value.y, value.z);
//}
//
//DirectX::XMFLOAT4 MathHelps::physics_to_quat(physx::PxQuat value)
//{
//	return DirectX::XMFLOAT4(value.x, value.y, value.z, value.w);
//}
//
//physx::PxMat44 MathHelps::matrix_to_physics(DirectX::XMFLOAT4X4 value)
//{
//	physx::PxMat44 matrix;
//
//	matrix[0][0] = value.m[0][0];
//	matrix[0][1] = value.m[0][1];
//	matrix[0][2] = value.m[0][2];
//	matrix[0][3] = value.m[0][3];
//
//	matrix[1][0] = value.m[1][0];
//	matrix[1][1] = value.m[1][1];
//	matrix[1][2] = value.m[1][2];
//	matrix[1][3] = value.m[1][3];
//
//	matrix[2][0] = value.m[2][0];
//	matrix[2][1] = value.m[2][1];
//	matrix[2][2] = value.m[2][2];
//	matrix[2][3] = value.m[2][3];
//
//	matrix[3][0] = value.m[3][0];
//	matrix[3][1] = value.m[3][1];
//	matrix[3][2] = value.m[3][2];
//	matrix[3][3] = value.m[3][3];
//
//	return matrix;
//}

//DirectX::XMFLOAT4X4 MathHelps::physics_to_matrix(physx::PxMat44 value)
//{
//	DirectX::XMFLOAT4X4 matrix;
//
//	matrix.m[0][0] = value[0][0];
//	matrix.m[0][1] = value[0][1];
//	matrix.m[0][2] = value[0][2];
//	matrix.m[0][3] = value[0][3];
//
//	matrix.m[1][0] = value[1][0];
//	matrix.m[1][1] = value[1][1];
//	matrix.m[1][2] = value[1][2];
//	matrix.m[1][3] = value[1][3];
//
//	matrix.m[2][0] = value[2][0];
//	matrix.m[2][1] = value[2][1];
//	matrix.m[2][2] = value[2][2];
//	matrix.m[2][3] = value[2][3];
//
//	matrix.m[3][0] = value[3][0];
//	matrix.m[3][1] = value[3][1];
//	matrix.m[3][2] = value[3][2];
//	matrix.m[3][3] = value[3][3];
//
//	return matrix;
//}

//physx::PxTransform MathHelps::position_rotation_to_physics(DirectX::XMFLOAT3 position, DirectX::XMFLOAT4 rotation)
//{
//	return physx::PxTransform(
//		position.x, position.y, position.z,
//		physx::PxQuat(rotation.x, rotation.y, rotation.z, rotation.w));
//}

float MathHelps::RadToDeg(float value)
{
	return DirectX::XMConvertToDegrees(value);
}

float MathHelps::DegToRad(float value)
{
	return DirectX::XMConvertToRadians(value);
}