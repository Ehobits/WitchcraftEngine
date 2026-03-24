#pragma once

#include <xstring>

#include "BaseComponent.h"
#include "HELPERS/Helpers.h"
#include "Engine/EngineUtils.h"

class WitchcraECS;
class SceneEntityBase;
struct EntityGeneralComponentData;

class GeneralComponent : public BaseComponent
{
public:
	void BindEntity(WitchcraECS* ecs, SceneEntityBase* ownerEntity);
	void SetVisible(bool arg);
	bool IsVisible();
	void SetComponentType(ComponentType type);
	virtual ComponentType GetComponentType() override;

private:
	bool TryGetSnapshot(EntityGeneralComponentData* outSnapshot) const;
	bool TrySetSnapshot(const EntityGeneralComponentData& snapshot);

	WitchcraECS* mEcs = nullptr;
	SceneEntityBase* mOwnerEntity = nullptr;
};
