#include "ServicesContainer.h"
#include "HELPERS/Helpers.h"
#include "D3DWindow/D3DWindow.h"
#include "../COMPONENT/MeshComponent.h"
#include "../COMPONENT/GeneralComponent.h"
#include "../COMPONENT/TransformComponent.h"
#include "../COMPONENT/RigidbodyComponent.h"
#include "../COMPONENT/CameraComponent.h"
#include "../COMPONENT/ScriptingComponent.h"
#include "../COMPONENT/PhysicsComponent.h"
#include "System/ProjectSceneSystem.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "ENGINE/EngineUtils.h"

#include <vector>

namespace
{
	bool IsUniqueComponentInstance(const BaseComponent* component)
	{
		if (component == nullptr)
			return false;

		// 当前阶段先对“一个实体只能有一个”的核心组件做约束。
		// GeneralComponent 会复用 ComponentType 表达实体语义，因此不能仅按 GetComponentType 判断。
		return dynamic_cast<const TransformComponent*>(component) != nullptr ||
			dynamic_cast<const MeshComponent*>(component) != nullptr ||
			dynamic_cast<const CameraComponent*>(component) != nullptr;
	}

	bool IsSameUniqueComponentKind(const BaseComponent* lhs, const BaseComponent* rhs)
	{
		if (lhs == nullptr || rhs == nullptr)
			return false;

		if (dynamic_cast<const TransformComponent*>(lhs) != nullptr)
			return dynamic_cast<const TransformComponent*>(rhs) != nullptr;

		if (dynamic_cast<const MeshComponent*>(lhs) != nullptr)
			return dynamic_cast<const MeshComponent*>(rhs) != nullptr;

		if (dynamic_cast<const CameraComponent*>(lhs) != nullptr)
			return dynamic_cast<const CameraComponent*>(rhs) != nullptr;

		return false;
	}
}

ServicesContainer::ServicesContainer()
	: mServices() 
{
}

bool ServicesContainer::Init(std::wstring Name)
{
	m_Name = Name;

	return true;
}

void ServicesContainer::ReName(std::wstring Name)
{
	m_Name = Name;
}

std::wstring ServicesContainer::GetName()
{
	return m_Name;
}

bool ServicesContainer::AddService(const std::wstring& typeID, void* service)
{
	BaseComponent* incomingComponent = static_cast<BaseComponent*>(service);

	// 先检查重名，再检查“一个实体只能有一个”的组件类别。
	for (const auto& servicePair : mServices)
	{
		if (wcscmp(servicePair.first.c_str(), typeID.c_str()) == 0)
		{
			MessageBox(nullptr, L"与现有项目重名", L"信息", MB_OK);
#ifdef _DEBUG
			assert(0);
#endif // DEBUG
			return false;
		}

		BaseComponent* existingComponent = static_cast<BaseComponent*>(servicePair.second);
		if (IsUniqueComponentInstance(incomingComponent) &&
			IsSameUniqueComponentKind(incomingComponent, existingComponent))
		{
			MessageBox(nullptr, L"该实体已存在同类型唯一组件", L"信息", MB_OK);
#ifdef _DEBUG
			assert(0);
#endif // DEBUG
			return false;
		}
	}
	mServices.insert(std::pair<std::wstring, void*>(typeID, service));
	return true;
}

void ServicesContainer::RemoveService(const std::wstring& typeID)
{
	mServices.erase(typeID);
}

void* ServicesContainer::FindService(const std::wstring& typeID) const
{
	std::map<std::wstring, void*>::const_iterator it = mServices.find(typeID);

	return (it != mServices.end() ? it->second : nullptr);
}

bool ServicesContainer::HasService(const std::wstring& typeID) const
{
	return mServices.find(typeID) != mServices.end();
}

const std::map<std::wstring, void*>& ServicesContainer::GetAllServices() const
{
	return mServices;
}

UINT ServicesContainer::Size() const
{
	return static_cast<UINT>(mServices.size());
}

void ServicesContainer::RemoveAll()
{
	// 这里只清空索引，不负责 delete；
	// 真正的销毁由 SceneEntityBase::DestroyAllContainer 统一处理。
	std::map<std::wstring, void*> empty_map1;
	mServices.swap(empty_map1);
}
