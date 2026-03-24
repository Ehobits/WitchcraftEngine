#pragma once

#include <xstring>

#include "Common/ComponentSharedTypes.h"
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
	void SetStatic(bool arg);           /* set static of Component */
	virtual void Destroy();              /* destroy Component & clear cache */

public:
	std::wstring GetName();                  /* return Component name */
	std::wstring GetTag();                   /* return Component tag */
	bool IsStatic();                         /* return true if Component is static */

public:
	virtual ComponentType GetComponentType() = 0;

private:
	std::wstring nameEntity = L"基本组件"; /* Component name */
	std::wstring tagEntity = L"空";          /* Component tag */
	bool staticEntity = false;                  /* if static Component */
};
