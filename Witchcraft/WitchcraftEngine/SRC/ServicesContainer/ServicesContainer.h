#pragma once

#undef min
#undef max

#include "Engine/EngineUtils.h"
#include "D3DWindow/D3D12_framework.h"

#include <map>

class ProjectSceneSystem;
class D3DWindow;

enum ComponentType
{
	Co_Unk,
	Co_Camera,     // 相机
	Co_Mesh,       // 模型
	Co_Material,   // 材质
	Co_Skeleton,   // 骨骼
	Co_Physics,    // 物理
	Co_RigidBody,  // 刚体
	Co_Scripting   // 脚本
};

// 变换体的结构
struct Transform
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT3 rotation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT3 scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	DirectX::BoundingBox boundingBox = DirectX::BoundingBox(DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
};

// 服务容器
class ServicesContainer
{
public:
	ServicesContainer();
	
	bool Init(D3DWindow* dx, std::wstring Name);

	void AddService(std::wstring typeID, void* service);
	void RemoveService(std::wstring typeID);
	void* FindService(std::wstring typeID) const;
	void* begin();
	void* end();
	UINT Size();
	void RemoveAll();
private:
	D3DWindow* m_dx = nullptr;
	std::map<std::wstring, void*> mServices;
};
