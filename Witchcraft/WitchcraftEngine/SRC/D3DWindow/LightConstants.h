#pragma once

#include "D3D12_framework.h"

enum class ShadowSamplingMode : UINT
{
	None = 0,
	DirectionalCascade = 1,
	SpotMap = 2,
	PointCube = 3
};

struct LightData
{
	DirectX::XMFLOAT3 Color = { 0.0f, 0.0f, 0.0f };
	float Type = 0.0f;

	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	float ShadowTextureIndex = -1.0f;

	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
	float Power = 1.0f;

	DirectX::XMFLOAT3 Up = { 0.0f, 1.0f, 0.0f };
	float ShadowSamplingMode = static_cast<float>(ShadowSamplingMode::None);

	float ShadowNearPlane = 0.1f;
	float ShadowFarPlane = 100.0f;
	float ShadowSoftnessScale = 1.0f;
	float ShadowBiasScale = 1.0f;

	float VolumetricEnable = 1.0f;
	float VolumetricIntensity = 1.0f;
	float VolumetricAttenuationDistance = 20.0f;
	float SpotRange = 20.0f;
	float SpotInnerCos = 0.8480481f;
	float SpotOuterCos = 0.7660444f;
	float ShadowTransformIndex = -1.0f;
	float PointRange = 20.0f;
};

struct LightConstants
{
	DirectX::XMFLOAT4 AmbientColor;
	LightData Lights[256];
};
