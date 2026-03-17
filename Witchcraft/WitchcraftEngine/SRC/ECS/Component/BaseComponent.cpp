#include "BaseComponent.h"

#include "String/SStringUtils.h"

BaseComponent::BaseComponent()
{
}

BaseComponent::~BaseComponent()
{
}

void BaseComponent::SetName(std::wstring name)
{
	if (!name.compare(L""))
		return;

	nameEntity = name;
}

void BaseComponent::SetTag(std::wstring tag)
{
	if (!tag.compare(L""))
		return;

	tagEntity = tag;
}

void BaseComponent::SetStatic(bool arg)
{
	staticEntity = arg;
}

void BaseComponent::Destroy()
{
}

std::wstring BaseComponent::GetName()
{
	return nameEntity;
}

std::wstring BaseComponent::GetTag()
{
	return tagEntity;
}

bool BaseComponent::IsStatic()
{
	return staticEntity;
}
