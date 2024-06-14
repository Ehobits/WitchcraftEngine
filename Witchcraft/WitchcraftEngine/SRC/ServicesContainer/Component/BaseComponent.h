#pragma once

#include <xstring>

#include "ServicesContainer/ServicesContainer.h"
#include "HELPERS/Helpers.h"
#include "Engine/EngineUtils.h"

// 基础组件
class BaseComponent
{
public:
	BaseComponent();
	virtual ~BaseComponent();

	void SetName(std::wstring name);                 /* set name of Component */
	void SetTag(std::wstring tag);                   /* set tag of Component */
	void SetEnabled(bool arg);          /* set active of Component */
	void SetStatic(bool arg);           /* set static of Component */
	void AddChild(ServicesContainer* ChildrenContainer); /* add child */
	void Destroy();                       /* destroy Component & clear cache */
	void DestroyChildren();               /* destroy all Component child & clear cache */

public:
	bool HasChildren();                      /* return true if has Component any child */
	std::wstring GetName();                  /* return Component name */
	std::wstring GetTag();                   /* return Component tag */
	bool IsEnabled();                        /* return true if Component is enabled */
	bool IsStatic();                         /* return true if Component is static */

public:
	ComponentType GetComponentType() { return ComponentType::Co_Unk; }

private:
	void SetEnabledAll(ServicesContainer* ChildrenContainer, bool arg);
	void SetStaticAll(ServicesContainer* ChildrenContainer, bool arg);
	void DestroyAll(ServicesContainer* ChildrenContainer);

private:
	std::wstring nameEntity = L"基本组件"; /* Component name */
	std::wstring tagEntity = L"空";          /* Component tag */
	bool enabledEntity = true;                  /* if enabled Component */
	bool staticEntity = false;                  /* if static Component */
	ServicesContainer* Children = nullptr;
};