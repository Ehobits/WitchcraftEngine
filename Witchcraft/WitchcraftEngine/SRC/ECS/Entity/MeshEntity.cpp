#include "MeshEntity.h"

void MeshEntity::SetAll(D3DWindow* dx)
{

}

void MeshEntity::Update()
{
	for (auto& component : mComponents)
	{
		// 在这里可以添加更新逻辑
		component.second;
	}
}

void MeshEntity::Destroy()
{
	for (auto& component : mComponents)
	{
		component.second->Destroy();
	}
}