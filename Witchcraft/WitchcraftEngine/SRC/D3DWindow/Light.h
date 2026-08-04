#pragma once

#include "D3D12_framework.h"
#include "D3DHelpers.h"

class Light
{
public:
	Light();
	Light(std::wstring name);
	~Light();

	void Create(std::wstring name);

	void SetName(std::wstring name);
	std::wstring GetName();

	// 索引到与此光照对应的常量缓冲区中。
	int LitCBIndex = -1;

	UINT NumFramesDirty = 3;

	// 0=环境光, 1=定向光, 2=点光, 3=聚光
	float Type = 0.0f;
	DirectX::XMFLOAT3 Color = { 0.0f, 0.0f, 0.0f };  // 颜色
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
	DirectX::XMFLOAT3 Up = { 0.0f, 1.0f, 0.0f };
	float Power = 1.0f;
	bool CastShadow = true;
	bool EnableVolumetric = true;
	float VolumetricIntensity = 1.0f;
	float VolumetricAttenuationDistance = 20.0f;
	float SpotRange = 20.0f;
	float SpotInnerAngleDegrees = 32.0f;
	float SpotOuterAngleDegrees = 40.0f;

private:
	std::wstring Name;

};
