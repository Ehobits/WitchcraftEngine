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
	Co_Transform,  // 变换
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

// 轻量级组件/服务容器。
// 当前 ECS 仍在过渡阶段，因此这里继续用“名称 -> 原始指针”的方式保存对象；
// 生命周期由外层实体负责，ServicesContainer 只负责索引、查询和唯一组件约束。
class ServicesContainer
{
public:
	ServicesContainer();
	
	bool Init(std::wstring Name);
	void ReName(std::wstring Name);
	std::wstring GetName();

	// typeID 既是存储键，也是当前阶段的“组件名”。
	bool AddService(const std::wstring& typeID, void* service);
	void RemoveService(const std::wstring& typeID);
	void* FindService(const std::wstring& typeID) const;
	bool HasService(const std::wstring& typeID) const;
	template<typename T>
	T* FindServiceAs(const std::wstring& typeID) const
	{
		return static_cast<T*>(FindService(typeID));
	}
	const std::map<std::wstring, void*>& GetAllServices() const;
	UINT Size() const;
	void RemoveAll();
private:
	std::wstring m_Name;                     // 仅用于调试/显示的容器名
	std::map<std::wstring, void*> mServices; // typeID -> service/component
};
