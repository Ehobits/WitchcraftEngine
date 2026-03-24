#include "GeneralComponent.h"

#include "ECS/WitchcraECS.h"

void GeneralComponent::BindEntity(WitchcraECS* ecs, SceneEntityBase* ownerEntity)
{
	mEcs = ecs;
	mOwnerEntity = ownerEntity;
}

bool GeneralComponent::TryGetSnapshot(EntityGeneralComponentData* outSnapshot) const
{
	if (outSnapshot == nullptr)
		return false;

	if (mEcs == nullptr || mOwnerEntity == nullptr || !mEcs->HasEntity(mOwnerEntity))
		return false;

	outSnapshot->visible = mEcs->IsEntityVisible(mOwnerEntity);
	outSnapshot->componentType = static_cast<std::uint32_t>(mEcs->GetEntityGeneralComponentType(mOwnerEntity));
	return true;
}

bool GeneralComponent::TrySetSnapshot(const EntityGeneralComponentData& snapshot)
{
	if (mEcs == nullptr || mOwnerEntity == nullptr || !mEcs->HasEntity(mOwnerEntity))
		return false;

	if (!mEcs->SetEntityVisible(mOwnerEntity, snapshot.visible))
		return false;

	return mEcs->SetEntityGeneralComponentType(mOwnerEntity, static_cast<ComponentType>(snapshot.componentType));
}

void GeneralComponent::SetVisible(bool arg)
{
	EntityGeneralComponentData snapshot{};
	TryGetSnapshot(&snapshot);
	snapshot.visible = arg;
	TrySetSnapshot(snapshot);
}

bool GeneralComponent::IsVisible()
{
	EntityGeneralComponentData snapshot{};
	TryGetSnapshot(&snapshot);
	return snapshot.visible;
}

void GeneralComponent::SetComponentType(ComponentType type)
{
	EntityGeneralComponentData snapshot{};
	TryGetSnapshot(&snapshot);
	snapshot.componentType = static_cast<std::uint32_t>(type);
	TrySetSnapshot(snapshot);
}

ComponentType GeneralComponent::GetComponentType()
{
	EntityGeneralComponentData snapshot{};
	TryGetSnapshot(&snapshot);
	return static_cast<ComponentType>(snapshot.componentType);
}
