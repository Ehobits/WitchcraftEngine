#pragma once

#include <Windows.h>

#include <algorithm>
#include <cmath>

#include <DirectXMath.h>

#include <SimpleMath.h>

#include <assimp/matrix3x3.h>
#include <assimp/matrix4x4.h>
#include <assimp/quaternion.h>

namespace MathHelps
{
	using namespace DirectX::SimpleMath;

	const float Pi = 3.1415926535f;

	static const Matrix Identity = { 1.f, 0.f, 0.f, 0.f,
											  0.f, 1.f, 0.f, 0.f,
											  0.f, 0.f, 1.f, 0.f,
											  0.f, 0.f, 0.f, 1.f };

	// 返回 [0, 1) 之间的随机浮点数。
	static float RandF()
	{
		return (float)(rand()) / (float)RAND_MAX;
	}

	// 返回 [a, b) 之间的随机浮点数。
	static float RandF(float a, float b)
	{
		return a + RandF() * (b - a);
	}

	// 数值限位工具
	static float ClampUnit(float value)
	{
		return (std::max)(-1.0f, (std::min)(1.0f, value));
	}

	/* --------------------- */
	DirectX::XMFLOAT3 ToRadians(DirectX::XMFLOAT3 rotation);
	DirectX::XMFLOAT3 ToDegrees(DirectX::XMFLOAT3 rotation);
	float RadToDeg(float value);
	float DegToRad(float value);
	DirectX::XMFLOAT4X4 ConvertAiMatrixToFloat4x4(const aiMatrix4x4& matrix);
	aiMatrix4x4 ConvertFloat4x4ToAiMatrix(const DirectX::XMFLOAT4X4& matrix);

	// ── 浮点校验工具 ──
	inline bool IsFiniteFloat(float value) { return std::isfinite(value); }

	inline bool IsFiniteFloat3(const DirectX::XMFLOAT3& value)
	{
		return IsFiniteFloat(value.x) && IsFiniteFloat(value.y) && IsFiniteFloat(value.z);
	}

	inline bool IsFiniteFloat4(const DirectX::XMFLOAT4& value)
	{
		return IsFiniteFloat(value.x) && IsFiniteFloat(value.y) && IsFiniteFloat(value.z) && IsFiniteFloat(value.w);
	}

	inline bool NearlyEqualFloat(float lhs, float rhs, float epsilon = 1e-4f)
	{
		return std::abs(lhs - rhs) <= epsilon;
	}

	inline bool NearlyEqualFloat3(const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs, float epsilon = 1e-4f)
	{
		return NearlyEqualFloat(lhs.x, rhs.x, epsilon) && NearlyEqualFloat(lhs.y, rhs.y, epsilon) && NearlyEqualFloat(lhs.z, rhs.z, epsilon);
	}

	inline bool NearlyEqualFloat4(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs, float epsilon = 1e-4f)
	{
		return NearlyEqualFloat(lhs.x, rhs.x, epsilon) && NearlyEqualFloat(lhs.y, rhs.y, epsilon) && NearlyEqualFloat(lhs.z, rhs.z, epsilon) && NearlyEqualFloat(lhs.w, rhs.w, epsilon);
	}

	inline bool IsFiniteFloat4x4(const DirectX::XMFLOAT4X4& value)
	{
		return
			IsFiniteFloat(value._11) && IsFiniteFloat(value._12) && IsFiniteFloat(value._13) && IsFiniteFloat(value._14) &&
			IsFiniteFloat(value._21) && IsFiniteFloat(value._22) && IsFiniteFloat(value._23) && IsFiniteFloat(value._24) &&
			IsFiniteFloat(value._31) && IsFiniteFloat(value._32) && IsFiniteFloat(value._33) && IsFiniteFloat(value._34) &&
			IsFiniteFloat(value._41) && IsFiniteFloat(value._42) && IsFiniteFloat(value._43) && IsFiniteFloat(value._44);
	}

	inline bool AreFloat4x4NearlyEqual(const DirectX::XMFLOAT4X4& lhs, const DirectX::XMFLOAT4X4& rhs, float epsilon = 1e-4f)
	{
		const float* lhsValues = reinterpret_cast<const float*>(&lhs);
		const float* rhsValues = reinterpret_cast<const float*>(&rhs);
		for (UINT valueIndex = 0; valueIndex < 16; ++valueIndex)
		{
			if (!NearlyEqualFloat(lhsValues[valueIndex], rhsValues[valueIndex], epsilon))
				return false;
		}
		return true;
	}

	inline DirectX::XMFLOAT4X4 TransposeFloat4x4(const DirectX::XMFLOAT4X4& value)
	{
		DirectX::XMFLOAT4X4 result{};
		DirectX::XMStoreFloat4x4(
			&result,
			DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&value)));
		return result;
	}

	inline DirectX::XMFLOAT4X4 MultiplyFloat4x4(const DirectX::XMFLOAT4X4& lhs, const DirectX::XMFLOAT4X4& rhs)
	{
		DirectX::XMFLOAT4X4 result{};
		DirectX::XMStoreFloat4x4(
			&result,
			DirectX::XMMatrixMultiply(
				DirectX::XMLoadFloat4x4(&lhs),
				DirectX::XMLoadFloat4x4(&rhs)));
		return result;
	}

}
