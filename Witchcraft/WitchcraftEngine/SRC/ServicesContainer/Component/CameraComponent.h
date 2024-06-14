#pragma once

#include "ServicesContainer/ServicesContainer.h"
#include "BaseComponent.h"
#include "HELPERS/Helpers.h"
#include "Engine/EngineUtils.h"

#define MIN_FOV 1.0f
#define MAX_FOV 256.0f

class D3DWindow;

struct CameraComponent : public BaseComponent
{
public:
	bool          enabledComponent = true;
	bool          present = false; // 当前的

public:
	void SetDXWindow(D3DWindow* dx);
	void          SetPresent(bool v);
	bool          IsPresent();

	void          SetFov(float _Fov);
	float         GetFov();
	void          SetNear(float _Near);
	float         GetNear();
	void          SetFar(float _Far);
	float         GetFar();
	void          SetEnabled(bool _Enabled);
	bool          IsEnabled();
	void          SetScale(float _Scale);
	float         GetScale();
	DirectX::XMMATRIX       GetViewMatrix();
	DirectX::XMMATRIX       GetProjectionMatrix();
	// 获取投影值
	float       GetProjectionValue();

	void RestoreScale();

	void SetAll();
	void UpdateAll();

	void Destroy();
private:
	D3DWindow* m_dx = nullptr;

	// 相机内部视椎体参数
	float mNearZ = 0.0f;
	float mFarZ = 0.0f;
	float mFovY = 0.0f;
	float mViewportScale = 0.0f;

	// 缓存视图/项目矩阵。
	DirectX::XMMATRIX mView = MathHelps::Identity;
	DirectX::XMMATRIX mProj = MathHelps::Identity;

private:
	ComponentType mComponentType = ComponentType::Co_Camera;
};