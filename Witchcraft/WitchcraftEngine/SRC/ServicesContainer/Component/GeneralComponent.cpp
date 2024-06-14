#include "GeneralComponent.h"

#include "ServicesContainer/COMPONENT/CameraComponent.h"
#include "ServicesContainer/COMPONENT/MeshComponent.h"
#include "String/SStringUtils.h"

void GeneralComponent::SetName(std::wstring name)
{
	if (!name.compare(L""))
		return;

	nameEntity = name;
}

void GeneralComponent::SetTag(std::wstring tag)
{
	if (!tag.compare(L""))
		return;

	tagEntity = tag;
}

std::wstring GeneralComponent::GetName()
{
	return nameEntity;
}

std::wstring GeneralComponent::GetTag()
{
	return tagEntity;
}

bool GeneralComponent::IsEnabled()
{
	return enabledEntity;
}

bool GeneralComponent::IsStatic()
{
	return staticEntity;
}
