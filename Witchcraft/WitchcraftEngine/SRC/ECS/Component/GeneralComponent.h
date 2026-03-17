#pragma once

#include <xstring>

#include "BaseComponent.h"
#include "HELPERS/Helpers.h"
#include "Engine/EngineUtils.h"

struct GeneralComponent : public BaseComponent
{
private:
	std::wstring nameEntity = L"General";      /* Component name */
	std::wstring tagEntity = L"Empty";         /* Component tag */
	bool staticEntity = false;                 /* if static Component */
	bool visibleEntity = true;                 /* if visible Component */

	ComponentType mComponentType = ComponentType::Co_Unk;

public:
	void SetName(std::wstring name);                 /* set name of Component */
	void SetTag(std::wstring tag);                   /* set tag of Component */
	void SetStatic(bool arg);
	void SetVisible(bool arg);

public:
	std::wstring GetName();                  /* return Component name */
	std::wstring GetTag();                   /* return Component tag */
	bool IsStatic();                         /* return true if Component is static */
	bool IsVisible();                        /* return true if Component is visible */

public:
	void SetComponentType(ComponentType type) { mComponentType = type; }
	virtual ComponentType GetComponentType() { return mComponentType; }
};
