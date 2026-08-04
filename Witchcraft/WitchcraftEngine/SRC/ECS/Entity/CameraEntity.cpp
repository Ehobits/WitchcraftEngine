#include "CameraEntity.h"
#include "../Component/CameraComponent.h"

void CameraEntity::SetAll(D3DWindow* dx)
{
	for (auto& component : mComponents)
	{
		ComponentType p = component.second->GetComponentType();
		switch (p)
		{
		case Co_Camera:
			CameraComponent* cam = (CameraComponent*)component.second;
			cam->SetDXWindow(dx);
			break;
		}
	}
}

void CameraEntity::Update()
{
	for (auto& component : mComponents)
	{
		// 在这里可以添加更新逻辑
		component.second;
	}
}

void CameraEntity::Destroy()
{
	for (auto& component : mComponents)
	{
		component.second->Destroy();
	}
}