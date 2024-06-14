#include "ServicesContainer.h"
#include "HELPERS/Helpers.h"
#include "D3DWindow/D3DWindow.h"
#include "COMPONENT/MeshComponent.h"
#include "COMPONENT/GeneralComponent.h"
#include "COMPONENT/TransformComponent.h"
#include "COMPONENT/RigidbodyComponent.h"
#include "COMPONENT/CameraComponent.h"
#include "COMPONENT/ScriptingComponent.h"
#include "COMPONENT/PhysicsComponent.h"
#include "System/ProjectSceneSystem.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "ENGINE/EngineUtils.h"

#include <vector>

ServicesContainer::ServicesContainer()
	: mServices() 
{
}

bool ServicesContainer::Init(D3DWindow* dx, std::wstring Name)
{
	m_dx = dx;
	m_dx->SetPosition3f(DirectX::XMFLOAT3(0.0f, 0.0f, -5.0f));

	return true;
}

void ServicesContainer::AddService(std::wstring typeID, void* service)
{
	auto tmp = mServices.begin();
	for (UINT i = 0; i < mServices.size(); i++)
	{
		if (wcscmp(tmp->first.c_str(), typeID.c_str()) == 0)
		{
			MessageBox(nullptr, L"与现有项目重名", L"信息", MB_OK);
#ifdef DEBUG
			assert(0);
#endif // DEBUG
			return;
		}
		tmp++;
	}
	mServices.insert(std::pair<std::wstring, void*>(typeID, service));
}

void ServicesContainer::RemoveService(std::wstring typeID)
{
	mServices.erase(typeID);
}

void* ServicesContainer::FindService(std::wstring typeID) const
{
	std::map<std::wstring, void*>::const_iterator it = mServices.find(typeID);

	return (it != mServices.end() ? it->second : nullptr);
}

void* ServicesContainer::begin()
{
	std::map<std::wstring, void*>::const_iterator it = mServices.begin();
	return it->second;
}

void* ServicesContainer::end()
{
	std::map<std::wstring, void*>::const_iterator it = mServices.end();
	return it->second;
}

UINT ServicesContainer::Size()
{
	return mServices.size();
}

void ServicesContainer::RemoveAll()
{
	std::map<std::wstring, void*> empty_map1;
	mServices.swap(empty_map1);
}
