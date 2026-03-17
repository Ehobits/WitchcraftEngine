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