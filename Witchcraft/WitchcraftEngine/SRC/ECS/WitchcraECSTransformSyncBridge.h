#pragma once

#include "WitchcraECS.h"

class WitchcraECSTransformSyncBridge
{
public:
	static DirectX::XMMATRIX TransformToMatrix(const Transform& transform);
	static DirectX::XMFLOAT4X4 IdentityMatrix4x4();
	static DirectX::XMFLOAT4X4 ComposeWorldMatrix(const Transform& localTransform, const DirectX::XMFLOAT4X4& parentWorldMatrix);
	static Transform DecomposeWorldTransform(const DirectX::XMFLOAT4X4& worldMatrix, const Transform& sourceTransform);
	static void MarkAllTransformsDirty(WitchcraECS& ecs);
	static void MarkTransformDirty(WitchcraECS& ecs, SceneEntityBase* entity);
	static void InitializeEntityTransformState(WitchcraECS& ecs, SceneEntityBase* entity, const Transform& localTransform = Transform{});
	static bool GetEntityLocalTransform(const WitchcraECS& ecs, SceneEntityBase* entity, Transform* outTransform);
	static bool GetEntityWorldTransform(const WitchcraECS& ecs, SceneEntityBase* entity, Transform* outTransform);
	static bool GetEntityWorldMatrix(const WitchcraECS& ecs, SceneEntityBase* entity, DirectX::XMFLOAT4X4* outMatrix);
	static bool GetEntityEditableLocalTransform(const WitchcraECS& ecs, SceneEntityBase* entity, Transform* outTransform);
	static bool SetEntityEditableLocalTransform(WitchcraECS& ecs, SceneEntityBase* entity, const Transform& transform);
	static bool GetEntityRenderTransform(const WitchcraECS& ecs, SceneEntityBase* entity, Transform* outTransform);
	static bool GetEntityRenderMatrix(const WitchcraECS& ecs, SceneEntityBase* entity, DirectX::XMFLOAT4X4* outMatrix);
	static void SyncTransformsToFlecs(WitchcraECS& ecs);
	static void SyncTransformComponentCacheFromFlecs(WitchcraECS& ecs);
	static void SyncTransformSubtree(WitchcraECS& ecs, SceneEntityBase* entity, const DirectX::XMFLOAT4X4& parentWorldMatrix);
};
