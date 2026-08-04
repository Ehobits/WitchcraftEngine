#pragma once

#undef min
#undef max

#include "Common/ComponentSharedTypes.h"

#include <map>
#include <xstring>

// 轻量级组件/服务容器。
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
	unsigned int Size() const;
	void RemoveAll();

private:
	std::wstring m_Name;
	std::map<std::wstring, void*> mServices;
};
