#include "Material.h"

Material::Material()
{

}

Material::Material(std::wstring name)
{
	Name = name;
}

Material::~Material()
{

}

void Material::Create(std::wstring name)
{
	Name = name;
}

void Material::SetName(std::wstring name)
{
	Name = name;
}

std::wstring Material::GetName()
{
	return Name;
}
