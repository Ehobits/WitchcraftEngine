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

void BaseComponent::SetEnabled(bool arg)
{
	SetEnabledAll(Children, arg);
}

void BaseComponent::SetStatic(bool arg)
{
	SetStaticAll(Children, arg);
}

void BaseComponent::AddChild(ServicesContainer* ChildrenContainer)
{
	Children = ChildrenContainer;
}

void BaseComponent::Destroy()
{
	DestroyAll(Children);
}

void BaseComponent::DestroyChildren()
{
	DestroyAll(Children);
}

bool BaseComponent::HasChildren()
{
	if (Children) 
		return true;
	else 
		return false;
}

std::wstring BaseComponent::GetName()
{
	return nameEntity;
}

std::wstring BaseComponent::GetTag()
{
	return tagEntity;
}

bool BaseComponent::IsEnabled()
{
	return enabledEntity;
}

bool BaseComponent::IsStatic()
{
	return staticEntity;
}

void BaseComponent::SetEnabledAll(ServicesContainer* ChildrenContainer, bool arg)
{
	BaseComponent* tmp = (BaseComponent*)ChildrenContainer->begin();
	for (int i = 0; i < ChildrenContainer->Size(); i++)
	{
		if(tmp)
			tmp->SetEnabled(arg);
		tmp++;
	}
}

void BaseComponent::SetStaticAll(ServicesContainer* ChildrenContainer, bool arg)
{
	BaseComponent* tmp = (BaseComponent*)ChildrenContainer->begin();
	for (int i = 0; i < ChildrenContainer->Size(); i++)
	{
		if (tmp)
			tmp->SetStatic(arg);
		tmp++;
	}
}

void BaseComponent::DestroyAll(ServicesContainer* ChildrenContainer)
{
	ChildrenContainer->RemoveAll();
}
