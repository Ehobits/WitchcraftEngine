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

	DirectX::XMFLOAT3 Strength = { 0.0f, 0.0f, 0.0f };  // 力度
	float FalloffStart = 1.0f;                          // 仅点光源/聚光灯
	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// 仅定向光/聚光灯
	float FalloffEnd = 10.0f;                           // 仅点光源/聚光灯
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // 仅点光源/聚光灯
	float SpotPower = 64.0f;                            // 仅聚光灯

private:
	std::wstring Name;

};

