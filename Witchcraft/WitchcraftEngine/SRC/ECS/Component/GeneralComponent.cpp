#include "GeneralComponent.h"

#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
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

void GeneralComponent::SetStatic(bool arg)
{
	staticEntity = arg;
}

void GeneralComponent::SetVisible(bool arg)
{
	visibleEntity = arg;
}

std::wstring GeneralComponent::GetName()
{
	return nameEntity;
}

std::wstring GeneralComponent::GetTag()
{
	return tagEntity;
}

bool GeneralComponent::IsStatic()
{
	return staticEntity;
}

bool GeneralComponent::IsVisible()
{
	return visibleEntity;
}
