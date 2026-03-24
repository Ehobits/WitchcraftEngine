#include "ServicesContainer.h"

#include <Windows.h>
#include <cassert>

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
	// ServicesContainer 现在只负责“按名称索引服务”。
	// 组件唯一性与替换语义统一交给 WitchcraECS 管理，
	// 这样容器本身就不再依赖具体组件类型。
	if (mServices.find(typeID) != mServices.end())
	{
		MessageBox(nullptr, L"与现有项目重名", L"信息", MB_OK);
		return false;
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

unsigned int ServicesContainer::Size() const
{
	return static_cast<unsigned int>(mServices.size());
}

void ServicesContainer::RemoveAll()
{
	// 这里只清空索引，不负责 delete。
	// 真正的组件销毁由 WitchcraECS 统一处理。
	std::map<std::wstring, void*> empty_map1;
	mServices.swap(empty_map1);
}
