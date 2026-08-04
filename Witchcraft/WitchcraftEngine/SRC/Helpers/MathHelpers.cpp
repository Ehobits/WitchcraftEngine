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

float MathHelps::RadToDeg(float value)
{
	return DirectX::XMConvertToDegrees(value);
}

float MathHelps::DegToRad(float value)
{
	return DirectX::XMConvertToRadians(value);
}

DirectX::XMFLOAT4X4 MathHelps::ConvertAiMatrixToFloat4x4(const aiMatrix4x4& matrix)
{
	return DirectX::XMFLOAT4X4(
		matrix.a1, matrix.b1, matrix.c1, matrix.d1,
		matrix.a2, matrix.b2, matrix.c2, matrix.d2,
		matrix.a3, matrix.b3, matrix.c3, matrix.d3,
		matrix.a4, matrix.b4, matrix.c4, matrix.d4);
}

aiMatrix4x4 MathHelps::ConvertFloat4x4ToAiMatrix(const DirectX::XMFLOAT4X4& matrix)
{
	return aiMatrix4x4(
		matrix._11, matrix._21, matrix._31, matrix._41,
		matrix._12, matrix._22, matrix._32, matrix._42,
		matrix._13, matrix._23, matrix._33, matrix._43,
		matrix._14, matrix._24, matrix._34, matrix._44);
}
