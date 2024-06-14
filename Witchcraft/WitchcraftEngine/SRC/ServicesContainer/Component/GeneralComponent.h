#pragma once

#include <xstring>

#include "ServicesContainer/ServicesContainer.h"
#include "BaseComponent.h"
#include "HELPERS/Helpers.h"
#include "Engine/EngineUtils.h"

struct GeneralComponent : public BaseComponent
{
private:
	std::wstring nameEntity = L"General";      /* Component name */
	std::wstring tagEntity = L"Empty";         /* Component tag */
	bool enabledEntity = true;                 /* if enabled Component */
	bool staticEntity = false;                 /* if static Component */

	ComponentType mComponentType = ComponentType::Co_Unk;

public:
	void SetName(std::wstring name);                 /* set name of Component */
	void SetTag(std::wstring tag);                   /* set tag of Component */

public:
	std::wstring GetName();                  /* return Component name */
	std::wstring GetTag();                   /* return Component tag */
	bool IsEnabled();                        /* return true if Component is enabled */
	bool IsStatic();                         /* return true if Component is static */

public:
	void SetComponentType(ComponentType type) { mComponentType = type; }
	ComponentType GetComponentType() { return mComponentType; }
};