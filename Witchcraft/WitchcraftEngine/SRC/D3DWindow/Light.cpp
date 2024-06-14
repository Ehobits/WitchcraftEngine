#include "Light.h"

Light::Light()
{
}

Light::Light(std::wstring name) 
{
	Name = name;
}

Light::~Light()
{
}

void Light::Create(std::wstring name) 
{
	Name = name;
}

void Light::SetName(std::wstring name)
{
	Name = name;
}

std::wstring Light::GetName()
{
	return Name;
}
