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

	// Returns random float in [0, 1).
	static float RandF()
	{
		return (float)(rand()) / (float)RAND_MAX;
	}

	// Returns random float in [a, b).
	static float RandF(float a, float b)
	{
		return a + RandF() * (b - a);
	}

	/* --------------------- */
	DirectX::XMFLOAT3 ToRadians(DirectX::XMFLOAT3 rotation);
	DirectX::XMFLOAT3 ToDegrees(DirectX::XMFLOAT3 rotation);
	float RadToDeg(float value);
	float DegToRad(float value);

}