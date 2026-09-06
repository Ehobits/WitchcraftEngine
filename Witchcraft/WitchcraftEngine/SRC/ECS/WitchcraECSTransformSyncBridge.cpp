#include "WitchcraECSTransformSyncBridge.h"

#include "ECS/COMPONENT/TransformComponent.h"
#include "Helpers/MathHelpers.h"

DirectX::XMMATRIX WitchcraECSTransformSyncBridge::TransformToMatrix(const Transform& transform)
{
	const DirectX::XMVECTOR zero = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	return DirectX::XMMatrixAffineTransformation(
		DirectX::XMLoadFloat3(&transform.scale),
		zero,
		DirectX::XMQuaternionRotationRollPitchYaw(
			transform.rotation.x * MathHelps::Pi / 45.0f / 4.0f,
			transform.rotation.y * MathHelps::Pi / 45.0f / 4.0f,
			transform.rotation.z * MathHelps::Pi / 45.0f / 4.0f),
		DirectX::XMLoadFloat3(&transform.position));
}

DirectX::XMFLOAT4X4 WitchcraECSTransformSyncBridge::IdentityMatrix4x4()
{
	DirectX::XMFLOAT4X4 identity = MathHelps::Identity;
	return identity;
}

DirectX::XMFLOAT4X4 WitchcraECSTransformSyncBridge::ComposeWorldMatrix(const Transform& localTransform, const DirectX::XMFLOAT4X4& parentWorldMatrix)
{
	DirectX::XMFLOAT4X4 worldMatrix = MathHelps::Identity;
	const DirectX::XMMATRIX localMatrix = TransformToMatrix(localTransform);
	const DirectX::XMMATRIX parentMatrix = DirectX::XMLoadFloat4x4(&parentWorldMatrix);
	DirectX::XMStoreFloat4x4(&worldMatrix, localMatrix * parentMatrix);
	return worldMatrix;
}

Transform WitchcraECSTransformSyncBridge::DecomposeWorldTransform(const DirectX::XMFLOAT4X4& worldMatrix, const Transform& sourceTransform)
{
	using namespace DirectX;

	Transform worldTransform = sourceTransform;
	const XMMATRIX world = XMLoadFloat4x4(&worldMatrix);

	XMVECTOR scaleVector = XMVectorZero();
	XMVECTOR rotationQuaternion = XMQuaternionIdentity();
	XMVECTOR translationVector = XMVectorZero();
	if (!XMMatrixDecompose(&scaleVector, &rotationQuaternion, &translationVector, world))
		return worldTransform;

	XMStoreFloat3(&worldTransform.scale, scaleVector);
	XMStoreFloat3(&worldTransform.position, translationVector);

	XMFLOAT4 rotation{};
	XMStoreFloat4(&rotation, XMQuaternionNormalize(rotationQuaternion));

	const float pitch = std::atan2(
		2.0f * (rotation.w * rotation.x + rotation.y * rotation.z),
		1.0f - 2.0f * (rotation.x * rotation.x + rotation.y * rotation.y));
	// atan2 保留完整象限；asin 会把 ±180 度范围错误压缩到 ±90 度。
	const float yaw = std::atan2(
		2.0f * (rotation.w * rotation.y - rotation.z * rotation.x),
		1.0f - 2.0f * (rotation.y * rotation.y + rotation.z * rotation.z));
	const float roll = std::atan2(
		2.0f * (rotation.w * rotation.z + rotation.x * rotation.y),
		1.0f - 2.0f * (rotation.y * rotation.y + rotation.z * rotation.z));

	worldTransform.rotation.x = NormalizeDegrees(XMConvertToDegrees(pitch));
	worldTransform.rotation.y = NormalizeDegrees(XMConvertToDegrees(yaw));
	worldTransform.rotation.z = NormalizeDegrees(XMConvertToDegrees(roll));
	return worldTransform;
}

void WitchcraECSTransformSyncBridge::MarkAllTransformsDirty(WitchcraECS& ecs)
{
	ecs.mTransformsDirty = true;
	ecs.mTransformsDirtyFull = true;
	ecs.mDirtyTransformRoots.clear();
}

void WitchcraECSTransformSyncBridge::MarkTransformDirty(WitchcraECS& ecs, SceneEntityBase* entity)
{
	if (entity == nullptr)
	{
		MarkAllTransformsDirty(ecs);
		return;
	}

	ecs.mTransformsDirty = true;
	if (ecs.mTransformsDirtyFull)
		return;

	for (SceneEntityBase* dirtyRoot : ecs.mDirtyTransformRoots)
	{
		if (dirtyRoot == entity || ecs.ContainsEntity(dirtyRoot, entity))
			return;
	}

	for (auto it = ecs.mDirtyTransformRoots.begin(); it != ecs.mDirtyTransformRoots.end();)
	{
		if (ecs.ContainsEntity(entity, *it))
			it = ecs.mDirtyTransformRoots.erase(it);
		else
			++it;
	}

	ecs.mDirtyTransformRoots.push_back(entity);
}

void WitchcraECSTransformSyncBridge::InitializeEntityTransformState(WitchcraECS& ecs, SceneEntityBase* entity, const Transform& localTransform)
{
	if (entity == nullptr || entity->entity == 0)
		return;

	const DirectX::XMFLOAT4X4 worldMatrix = ComposeWorldMatrix(localTransform, IdentityMatrix4x4());
	const Transform worldTransform = DecomposeWorldTransform(worldMatrix, localTransform);

	ecs.entityWorld.entity(entity->entity).set<EntityLocalTransform>({ localTransform });
	ecs.entityWorld.entity(entity->entity).set<EntityWorldTransform>({ worldTransform });
	ecs.entityWorld.entity(entity->entity).set<EntityWorldMatrix>({ worldMatrix });
	MarkTransformDirty(ecs, entity);
}

bool WitchcraECSTransformSyncBridge::GetEntityLocalTransform(const WitchcraECS& ecs, SceneEntityBase* entity, Transform* outTransform)
{
	if (entity == nullptr || entity->entity == 0 || outTransform == nullptr || ecs.mLocalTransformComponentId == 0)
		return false;

	const EntityLocalTransform* transformData = static_cast<const EntityLocalTransform*>(
		ecs_get_id(ecs.entityWorld, entity->entity, ecs.mLocalTransformComponentId));
	if (transformData == nullptr)
		return false;

	*outTransform = transformData->value;
	return true;
}

bool WitchcraECSTransformSyncBridge::GetEntityWorldTransform(const WitchcraECS& ecs, SceneEntityBase* entity, Transform* outTransform)
{
	if (entity == nullptr || entity->entity == 0 || outTransform == nullptr || ecs.mWorldTransformComponentId == 0)
		return false;

	const EntityWorldTransform* transformData = static_cast<const EntityWorldTransform*>(
		ecs_get_id(ecs.entityWorld, entity->entity, ecs.mWorldTransformComponentId));
	if (transformData == nullptr)
		return false;

	*outTransform = transformData->value;
	return true;
}

bool WitchcraECSTransformSyncBridge::GetEntityWorldMatrix(const WitchcraECS& ecs, SceneEntityBase* entity, DirectX::XMFLOAT4X4* outMatrix)
{
	if (entity == nullptr || entity->entity == 0 || outMatrix == nullptr || ecs.mWorldMatrixComponentId == 0)
		return false;

	const EntityWorldMatrix* matrixData = static_cast<const EntityWorldMatrix*>(
		ecs_get_id(ecs.entityWorld, entity->entity, ecs.mWorldMatrixComponentId));
	if (matrixData == nullptr)
		return false;

	*outMatrix = matrixData->value;
	return true;
}

bool WitchcraECSTransformSyncBridge::GetEntityEditableLocalTransform(const WitchcraECS& ecs, SceneEntityBase* entity, Transform* outTransform)
{
	if (entity == nullptr || outTransform == nullptr)
		return false;

	return GetEntityLocalTransform(ecs, entity, outTransform);
}

bool WitchcraECSTransformSyncBridge::SetEntityEditableLocalTransform(WitchcraECS& ecs, SceneEntityBase* entity, const Transform& transform, bool syncImmediately)
{
	if (entity == nullptr || entity->entity == 0 || ecs.mLocalTransformComponentId == 0)
		return false;

	Transform sanitizedTransform = transform;
	sanitizedTransform.scale.x = sanitizedTransform.scale.x < 0.0f ? 0.0f : sanitizedTransform.scale.x;
	sanitizedTransform.scale.y = sanitizedTransform.scale.y < 0.0f ? 0.0f : sanitizedTransform.scale.y;
	sanitizedTransform.scale.z = sanitizedTransform.scale.z < 0.0f ? 0.0f : sanitizedTransform.scale.z;

	ecs.entityWorld.entity(entity->entity).set<EntityLocalTransform>({ sanitizedTransform });

	if (TransformComponent* transformComponent = ecs.GetComponent<TransformComponent>(entity))
		transformComponent->SyncLocalTransformCache(sanitizedTransform);

	MarkTransformDirty(ecs, entity);
	if (syncImmediately)
		SyncTransformsToFlecs(ecs);
	return true;
}

bool WitchcraECSTransformSyncBridge::GetEntityRenderTransform(const WitchcraECS& ecs, SceneEntityBase* entity, Transform* outTransform)
{
	if (entity == nullptr || outTransform == nullptr)
		return false;

	if (GetEntityWorldTransform(ecs, entity, outTransform))
		return true;

	if (TransformComponent* transformComponent = ecs.GetComponent<TransformComponent>(entity))
	{
		*outTransform = transformComponent->GetTransform();
		return true;
	}

	return false;
}

bool WitchcraECSTransformSyncBridge::GetEntityRenderMatrix(const WitchcraECS& ecs, SceneEntityBase* entity, DirectX::XMFLOAT4X4* outMatrix)
{
	return GetEntityWorldMatrix(ecs, entity, outMatrix);
}

void WitchcraECSTransformSyncBridge::SyncTransformsToFlecs(WitchcraECS& ecs)
{
	if (!ecs.mTransformsDirty)
		return;

	SyncTransformComponentCacheFromFlecs(ecs);
	ecs.mTransformsDirty = false;
	ecs.mTransformsDirtyFull = false;
	ecs.mDirtyTransformRoots.clear();
}

void WitchcraECSTransformSyncBridge::SyncTransformComponentCacheFromFlecs(WitchcraECS& ecs)
{
	const DirectX::XMFLOAT4X4 identityMatrix = IdentityMatrix4x4();

	if (ecs.mTransformsDirtyFull)
	{
		for (SceneEntityBase* entity : ecs.entities)
			SyncTransformSubtree(ecs, entity, identityMatrix);
		return;
	}

	for (SceneEntityBase* dirtyRoot : ecs.mDirtyTransformRoots)
	{
		if (dirtyRoot == nullptr || dirtyRoot->entity == 0)
			continue;

		SceneEntityBase* parentEntity = ecs.GetParentEntity(dirtyRoot);
		if (parentEntity == nullptr)
		{
			SyncTransformSubtree(ecs, dirtyRoot, identityMatrix);
			continue;
		}

		DirectX::XMFLOAT4X4 parentWorldMatrix = identityMatrix;
		ecs.GetEntityWorldMatrix(parentEntity, &parentWorldMatrix);
		SyncTransformSubtree(ecs, dirtyRoot, parentWorldMatrix);
	}
}

void WitchcraECSTransformSyncBridge::SyncTransformSubtree(WitchcraECS& ecs, SceneEntityBase* entity, const DirectX::XMFLOAT4X4& parentWorldMatrix)
{
	if (entity == nullptr || entity->entity == 0)
		return;

	Transform localTransform{};
	const EntityLocalTransform* localTransformData = static_cast<const EntityLocalTransform*>(
		ecs_get_id(ecs.entityWorld, entity->entity, ecs.mLocalTransformComponentId));
	if (localTransformData != nullptr)
		localTransform = localTransformData->value;

	const DirectX::XMFLOAT4X4 worldMatrix = ComposeWorldMatrix(localTransform, parentWorldMatrix);
	const Transform worldTransform = DecomposeWorldTransform(worldMatrix, localTransform);

	if (TransformComponent* transformComponent = ecs.GetComponent<TransformComponent>(entity))
	{
		transformComponent->SyncLocalTransformCache(localTransform);
		transformComponent->SyncGlobalTransformCache(worldTransform);
	}

	if (ecs.mWorldMatrixComponentId != 0)
		ecs.entityWorld.entity(entity->entity).set<EntityWorldMatrix>({ worldMatrix });
	if (ecs.mWorldTransformComponentId != 0)
		ecs.entityWorld.entity(entity->entity).set<EntityWorldTransform>({ worldTransform });

	for (SceneEntityBase* child : ecs.GetEntityChildren(entity))
		SyncTransformSubtree(ecs, child, worldMatrix);
}
