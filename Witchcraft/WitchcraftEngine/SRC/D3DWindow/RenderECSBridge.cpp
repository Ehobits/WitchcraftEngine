#include "D3DWindow.h"
#include "ECS/WitchcraECS.h"
#include "ECS/Component/BillboardComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/RenderDrawSetComponent.h"
#include "ECS/Component/SkinningRuntimeComponent.h"
#include "ECS/Component/TransformComponent.h"

namespace RenderECSBridgeDetail
{
	std::wstring BuildBillboardRenderItemName(const std::wstring& baseRenderItemName)
	{
		return baseRenderItemName + L"__billboard";
	}

	UINT FindRenderItemLayerIndex(
		const std::map<std::wstring, RenderItem*> (&ritemLayers)[(UINT)渲染项目计数],
		const std::wstring& renderItemName)
	{
		for (UINT layerIndex = 0; layerIndex < (UINT)渲染项目计数; ++layerIndex)
		{
			if (ritemLayers[layerIndex].find(renderItemName) != ritemLayers[layerIndex].end())
				return layerIndex;
		}

		return static_cast<UINT>(渲染项目计数);
	}

	bool IsFiniteRenderBridgeMatrix(const DirectX::XMFLOAT4X4& value)
	{
		return
			std::isfinite(value._11) && std::isfinite(value._12) && std::isfinite(value._13) && std::isfinite(value._14) &&
			std::isfinite(value._21) && std::isfinite(value._22) && std::isfinite(value._23) && std::isfinite(value._24) &&
			std::isfinite(value._31) && std::isfinite(value._32) && std::isfinite(value._33) && std::isfinite(value._34) &&
			std::isfinite(value._41) && std::isfinite(value._42) && std::isfinite(value._43) && std::isfinite(value._44);
	}

	SceneEntityBase* ResolveSkinnedRenderTransformEntity(WitchcraECS* ecs, SceneEntityBase* sourceEntity)
	{
		if (ecs == nullptr || sourceEntity == nullptr)
			return sourceEntity;

		if (ecs->GetComponent<SkinningRuntimeComponent>(sourceEntity) != nullptr)
			return sourceEntity;

		for (SceneEntityBase* parentEntity = ecs->GetParentEntity(sourceEntity);
			parentEntity != nullptr;
			parentEntity = ecs->GetParentEntity(parentEntity))
		{
			if (ecs->GetComponent<SkinningRuntimeComponent>(parentEntity) != nullptr)
				return parentEntity;
		}

		return sourceEntity;
	}

	void EmitRenderBridgeDebugMessage(
		const wchar_t* stage,
		const std::wstring& renderItemName,
		WitchcraECS* ecs,
		SceneEntityBase* entity,
		const DirectX::XMFLOAT4X4& worldTransform,
		const DirectX::XMFLOAT4X4& texTransform)
	{
		const std::wstring entityName =
			(ecs != nullptr && entity != nullptr) ? ecs->GetEntityName(entity) : std::wstring(L"<null>");

		wchar_t debugText[1024] = {};
		swprintf_s(
			debugText,
			L"[RenderTransformDebug] stage=%s renderItem=%s entity=%s worldFinite=%d texFinite=%d worldTranslation=(%.4f,%.4f,%.4f)\n",
			stage != nullptr ? stage : L"<null>",
			renderItemName.c_str(),
			entityName.c_str(),
			IsFiniteRenderBridgeMatrix(worldTransform) ? 1 : 0,
			IsFiniteRenderBridgeMatrix(texTransform) ? 1 : 0,
			worldTransform._41,
			worldTransform._42,
			worldTransform._43);
		::OutputDebugStringW(debugText);
	}
}

using namespace RenderECSBridgeDetail;

// RenderECSBridge
// 说明：
// - 该实现文件承接 D3DWindow 中“ECS / SceneEntity -> RenderItem / RenderCache”的桥接逻辑；
// - 当前仍通过 D3DWindow 的成员函数形式暴露，先做物理拆分，不改现有接口与调用关系；
// - 目标是降低 D3DWindow.cpp 的职责密度，为后续继续模块化做准备。

bool D3DWindow::BuildEntityRenderTransforms(
	SceneEntityBase* entity,
	WitchcraECS* ecs,
	DirectX::XMFLOAT4X4* outWorldTransform,
	DirectX::XMFLOAT4X4* outTexTransform)
{
	if (entity == nullptr || ecs == nullptr || outWorldTransform == nullptr || outTexTransform == nullptr)
		return false;

	*outWorldTransform = MathHelps::Identity;
	*outTexTransform = MathHelps::Identity;

	EntityRenderView renderView;
	if (!ecs->BuildEntityRenderView(entity, &renderView))
	{
		BuildStandardEntityRenderTransforms(entity, ecs, outWorldTransform, outTexTransform);
		return true;
	}

	if (renderView.meshComponent == nullptr &&
		renderView.billboardComponent == nullptr &&
		renderView.renderDrawSetComponent == nullptr)
	{
		BuildStandardEntityRenderTransforms(entity, ecs, outWorldTransform, outTexTransform);
		return true;
	}

	if (renderView.renderLayerIndex == 天空渲染项目)
	{
		Transform skyTransform{};
		if (ecs->GetEntityRenderTransform(entity, &skyTransform))
			BuildSkyRenderTransforms(&skyTransform, outWorldTransform, outTexTransform);
		else
			BuildSkyRenderTransforms(nullptr, outWorldTransform, outTexTransform);

		return true;
	}

	BuildStandardEntityRenderTransforms(entity, ecs, outWorldTransform, outTexTransform);
	return true;
}

void D3DWindow::BuildStandardEntityRenderTransforms(
	SceneEntityBase* entity,
	WitchcraECS* ecs,
	DirectX::XMFLOAT4X4* outWorldTransform,
	DirectX::XMFLOAT4X4* outTexTransform)
{
	DirectX::XMStoreFloat4x4(outTexTransform, DirectX::XMMatrixScaling(1.0f, 1.0f, 1.0f));

	DirectX::XMFLOAT4X4 renderMatrix = MathHelps::Identity;
	if (ecs->GetEntityRenderMatrix(entity, &renderMatrix))
	{
		*outWorldTransform = renderMatrix;
		return;
	}

	Transform localTransform{};
	if (ecs->GetEntityEditableLocalTransform(entity, &localTransform))
		*outWorldTransform = ::BuildWorldMatrixFromTransformData(localTransform);
}

std::vector<RenderItem*> D3DWindow::CollectSelectedRenderItems()
{
	std::vector<RenderItem*> selectedRenderItems;
	if (mLastExternalECS == nullptr || !mLastExternalECS->HasSelectedEntity())
		return selectedRenderItems;

	std::vector<SceneEntityBase*> selectedRootEntities;
	{
		std::vector<SceneEntityBase*> pendingEntities = mLastExternalECS->GetSceneRootEntities();
		while (!pendingEntities.empty())
		{
			SceneEntityBase* entity = pendingEntities.back();
			pendingEntities.pop_back();
			if (entity == nullptr)
				continue;

			if (mLastExternalECS->IsEntitySelectedInHierarchy(entity))
				selectedRootEntities.push_back(entity);

			const std::vector<SceneEntityBase*>& children = mLastExternalECS->GetSceneChildren(entity);
			for (SceneEntityBase* child : children)
				pendingEntities.push_back(child);
		}
	}

	if (selectedRootEntities.empty())
		return selectedRenderItems;

	std::unordered_set<std::wstring> appendedRenderItemNames;
	std::unordered_set<SceneEntityBase*> visitedEntities;
	std::vector<SceneEntityBase*> pendingEntities = selectedRootEntities;
	while (!pendingEntities.empty())
	{
		SceneEntityBase* entity = pendingEntities.back();
		pendingEntities.pop_back();
		if (entity == nullptr)
			continue;
		if (!visitedEntities.insert(entity).second)
			continue;

		const std::vector<SceneEntityBase*>& children = mLastExternalECS->GetSceneChildren(entity);
		for (SceneEntityBase* child : children)
			pendingEntities.push_back(child);

		EntityRenderView renderView;
		if (!mLastExternalECS->BuildEntityRenderView(entity, &renderView))
			continue;

		RenderDrawSetComponent* renderDrawSetComponent = mLastExternalECS->GetComponent<RenderDrawSetComponent>(entity);
		if (renderDrawSetComponent != nullptr && !renderDrawSetComponent->GetDraws().empty() && mLastExternalECS->IsEntityVisible(entity))
		{
			for (const RenderDrawSlice& drawSlice : renderDrawSetComponent->GetDraws())
			{
				if (appendedRenderItemNames.insert(drawSlice.RenderItemName).second)
				{
					RenderItem* renderItem = GetRenderItem(drawSlice.RenderItemName);
					if (renderItem != nullptr)
						selectedRenderItems.push_back(renderItem);
				}
			}
		}

		if ((renderView.meshComponent == nullptr && renderView.billboardComponent == nullptr) || !renderView.visible || renderView.isSkyEntity)
			continue;

		if (renderView.meshComponent != nullptr &&
			appendedRenderItemNames.insert(renderView.renderItemName).second)
		{
			RenderItem* renderItem = GetRenderItem(renderView.renderItemName);
			if (renderItem != nullptr)
				selectedRenderItems.push_back(renderItem);
		}

		if (renderView.billboardComponent != nullptr)
		{
			const std::wstring billboardRenderItemName = BuildBillboardRenderItemName(renderView.renderItemName);
			if (appendedRenderItemNames.insert(billboardRenderItemName).second)
			{
				RenderItem* renderItem = GetRenderItem(billboardRenderItemName);
				if (renderItem != nullptr)
					selectedRenderItems.push_back(renderItem);
			}
		}
	}

	return selectedRenderItems;
}

void D3DWindow::AppendRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr)
		return;
	if (!PrepareRenderItemEntityOperation(ecs, false))
		return;

	AppendRenderItemsFromEntityRecursive(entity, ecs);
}

void D3DWindow::AppendRenderItemsFromEntityRecursive(SceneEntityBase* currentEntity, WitchcraECS* ecs)
{
	if (currentEntity == nullptr || ecs == nullptr)
		return;

	RenderDrawSetComponent* renderDrawSetComponent = ecs->GetComponent<RenderDrawSetComponent>(currentEntity);
	if (renderDrawSetComponent != nullptr && !renderDrawSetComponent->GetDraws().empty() && ecs->IsEntityVisible(currentEntity))
	{
		SceneEntityType drawSetSceneType = SceneEntityType::StaticScenery;
		(void)ecs->GetEntitySceneType(currentEntity, &drawSetSceneType);

		for (const RenderDrawSlice& drawSlice : renderDrawSetComponent->GetDraws())
		{
			SceneEntityBase* meshSourceEntity =
				drawSlice.TransformSourceEntity != nullptr ? drawSlice.TransformSourceEntity : currentEntity;
			SceneEntityBase* transformSourceEntity = meshSourceEntity;
			if (drawSlice.IsSkinned)
				transformSourceEntity = ResolveSkinnedRenderTransformEntity(ecs, transformSourceEntity);

			DirectX::XMFLOAT4X4 worldTransform = MathHelps::Identity;
			DirectX::XMFLOAT4X4 texTransform = MathHelps::Identity;
			BuildEntityRenderTransforms(transformSourceEntity, ecs, &worldTransform, &texTransform);

			TransformComponent* transformComponent = ecs->GetComponent<TransformComponent>(transformSourceEntity);
			DirectX::BoundingBox localBoundsStorage{};
			const DirectX::BoundingBox* localBounds = nullptr;
			if (transformComponent != nullptr)
			{
				localBoundsStorage = transformComponent->GetBoundingBox();
				localBounds = &localBoundsStorage;
			}

			AggregateGraphicObj& drawAggrObject = DrawSetAggrObjectCache[drawSlice.RenderItemName];
			drawAggrObject.IndexCount = drawSlice.IndexCount;
			drawAggrObject.StartIndexLocation = drawSlice.StartIndexLocation;
			drawAggrObject.BaseVertexLocation = drawSlice.BaseVertexLocation;

			ObjectCollection& drawObjectCollection = DrawSetObjectCollectionCache[drawSlice.RenderItemName];
			drawObjectCollection.AggrObject = &drawAggrObject;
			drawObjectCollection.Material = nullptr;

			if (!drawSlice.MaterialName.empty())
			{
				auto materialIt = Materials.find(drawSlice.MaterialName);
				if (materialIt != Materials.end())
					drawObjectCollection.Material = &materialIt->second;
			}

			if (drawObjectCollection.Material == nullptr)
			{
				auto autoMaterialIt = Materials.find(L"autoMat");
				if (autoMaterialIt != Materials.end())
					drawObjectCollection.Material = &autoMaterialIt->second;
			}

#ifdef _DEBUG
			const bool geometryExists = HasShapeGeometry(drawSlice.GeometryName);
			const UINT resolvedLayerIndexForSlice =
				(drawObjectCollection.Material != nullptr)
				? ResolveRenderLayerIndexByMaterial(drawSlice.RenderLayerIndex, drawObjectCollection.Material->GetName())
				: drawSlice.RenderLayerIndex;
			const bool preCreateSuspicious =
				!geometryExists ||
				drawObjectCollection.Material == nullptr;

			if (preCreateSuspicious)
			{
				EngineHelpers::AddLog(
					L"[DrawSetDebug] stage=PreCreate entity=%s renderItem=%s transformSource=%s geometry=%s geometryExists=%d materialName=%s materialBound=%s requestedLayer=%u resolvedLayer=%u indexCount=%u startIndex=%u baseVertex=%d isSkinned=%d",
					ecs->GetEntityName(currentEntity).c_str(),
					drawSlice.RenderItemName.c_str(),
					transformSourceEntity != nullptr ? ecs->GetEntityName(transformSourceEntity).c_str() : L"<null>",
					drawSlice.GeometryName.c_str(),
					geometryExists ? 1 : 0,
					drawSlice.MaterialName.empty() ? L"<empty>" : drawSlice.MaterialName.c_str(),
					drawObjectCollection.Material != nullptr ? drawObjectCollection.Material->GetName().c_str() : L"<null>",
					drawSlice.RenderLayerIndex,
					resolvedLayerIndexForSlice,
					drawSlice.IndexCount,
					drawSlice.StartIndexLocation,
					drawSlice.BaseVertexLocation,
					drawSlice.IsSkinned ? 1 : 0);
			}
#endif

			AddRenderItem(
				drawSlice.RenderItemName,
				&drawObjectCollection,
				drawSlice.GeometryName,
				drawSlice.RenderLayerIndex,
				&worldTransform,
				&texTransform,
				drawSlice.MaterialName.empty() ? nullptr : &drawSlice.MaterialName,
				localBounds,
				drawSetSceneType,
				false);

			RenderItem* drawRenderItem = GetRenderItem(drawSlice.RenderItemName);
			if (drawRenderItem != nullptr)
			{
				drawRenderItem->SourceEntity = meshSourceEntity;
				drawRenderItem->CullingSourceEntity = currentEntity;
				drawRenderItem->IsSkinned = drawSlice.IsSkinned;
				drawRenderItem->SceneType = drawSetSceneType;
				drawRenderItem->DisableFrustumCulling = false;
			}

#ifdef _DEBUG
			const UINT actualLayerIndex = FindRenderItemLayerIndex(RitemLayer, drawSlice.RenderItemName);
			const bool postCreateSuspicious =
				drawRenderItem == nullptr ||
				drawRenderItem->Obj == nullptr ||
				drawRenderItem->Obj->Material == nullptr ||
				drawRenderItem->Geo == nullptr ||
				actualLayerIndex >= (UINT)渲染项目计数;
			if (postCreateSuspicious)
			{
				EngineHelpers::AddLog(
					L"[DrawSetDebug] stage=PostCreate entity=%s renderItem=%s created=%d objectCollection=%p material=%s geometryBound=%s geoIndexBytes=%u actualLayer=%u inLayer=%d indexCount=%u startIndex=%u baseVertex=%d",
					ecs->GetEntityName(currentEntity).c_str(),
					drawSlice.RenderItemName.c_str(),
					drawRenderItem != nullptr ? 1 : 0,
					&drawObjectCollection,
					(drawRenderItem != nullptr && drawRenderItem->Obj != nullptr && drawRenderItem->Obj->Material != nullptr)
					? drawRenderItem->Obj->Material->GetName().c_str()
					: (drawObjectCollection.Material != nullptr ? drawObjectCollection.Material->GetName().c_str() : L"<null>"),
					(drawRenderItem != nullptr && drawRenderItem->Geo != nullptr) ? drawRenderItem->Geo->Name.c_str() : L"<null>",
					(drawRenderItem != nullptr && drawRenderItem->Geo != nullptr) ? drawRenderItem->Geo->indexBufferView.SizeInBytes : 0u,
					actualLayerIndex,
					actualLayerIndex < (UINT)渲染项目计数 ? 1 : 0,
					drawSlice.IndexCount,
					drawSlice.StartIndexLocation,
					drawSlice.BaseVertexLocation);
			}
#endif
		}
	}

	EntityRenderView renderView;
	if (ecs->BuildEntityRenderView(currentEntity, &renderView) &&
		(renderView.meshComponent != nullptr || renderView.billboardComponent != nullptr) &&
		renderView.visible)
	{
		SceneEntityBase* transformSourceEntity =
			renderView.isSkinned ? ResolveSkinnedRenderTransformEntity(ecs, currentEntity) : currentEntity;
		DirectX::XMFLOAT4X4 worldTransform = MathHelps::Identity;
		DirectX::XMFLOAT4X4 texTransform = MathHelps::Identity;
		BuildEntityRenderTransforms(transformSourceEntity, ecs, &worldTransform, &texTransform);
		TransformComponent* transformComponent = ecs->GetComponent<TransformComponent>(transformSourceEntity);
		DirectX::BoundingBox localBoundsStorage{};
		const DirectX::BoundingBox* localBounds = nullptr;
		if (transformComponent != nullptr)
		{
			localBoundsStorage = transformComponent->GetBoundingBox();
			localBounds = &localBoundsStorage;
		}

		if (renderView.meshComponent != nullptr)
		{
			AddRenderItem(renderView.renderItemName, renderView.meshComponent->GetObjectCollection(), renderView.geometryName,
				renderView.renderLayerIndex, &worldTransform, &texTransform,
				renderView.materialName.empty() ? nullptr : &renderView.materialName,
				localBounds, renderView.sceneEntityType, false);

			RenderItem* renderItem = GetRenderItem(renderView.renderItemName);
			if (renderItem != nullptr)
			{
				renderItem->SourceEntity = currentEntity;
				renderItem->CullingSourceEntity = currentEntity;
				renderItem->IsSkinned = renderView.isSkinned;
			}
		}

		if (renderView.billboardComponent != nullptr)
		{
			const std::wstring billboardRenderItemName = BuildBillboardRenderItemName(renderView.renderItemName);
			BillboardAggrObjectCache[billboardRenderItemName] = AggrObject[D3DWindow::DefaultBillboardGeometryName];
			ObjectCollection& billboardObjectCollection = BillboardObjectCollectionCache[billboardRenderItemName];
			billboardObjectCollection.Material = nullptr;
			billboardObjectCollection.AggrObject = &BillboardAggrObjectCache[billboardRenderItemName];
			const std::wstring& billboardMaterialName = renderView.billboardComponent->GetMaterialName();

			AddRenderItem(
				billboardRenderItemName,
				&billboardObjectCollection,
				D3DWindow::DefaultBillboardGeometryName,
				透明物体渲染项目,
				&worldTransform,
				&texTransform,
				billboardMaterialName.empty() ? nullptr : &billboardMaterialName,
				nullptr,
				renderView.sceneEntityType,
				false);

			RenderItem* billboardRenderItem = GetRenderItem(billboardRenderItemName);
			if (billboardRenderItem != nullptr)
			{
				billboardRenderItem->SourceEntity = currentEntity;
				billboardRenderItem->CullingSourceEntity = currentEntity;
				billboardRenderItem->IsBillboard = true;
				billboardRenderItem->Billboard = renderView.billboardComponent->BuildData();
				billboardRenderItem->BillboardAnchorTransform = worldTransform;
				billboardRenderItem->NumFramesDirty = SwapChainBufferCount;
				DirtyObjectCBItems.insert(billboardRenderItemName);
			}
		}
	}

	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(currentEntity))
	{
		AppendRenderItemsFromEntityRecursive(childEntity, ecs);
	}
}

bool D3DWindow::PrepareRenderItemEntityOperation(WitchcraECS* ecs, bool syncTransforms)
{
	if (ecs == nullptr)
	{
#ifdef _DEBUG
		assert(false && "需要一个有效的 WitchcraECS 指针。");
#endif
		return false;
	}

	mLastExternalECS = ecs;
	mSkinWeightVizPass.SetECS(ecs);
	if (syncTransforms)
		ecs->SyncTransformsToFlecs();
	return true;
}

void D3DWindow::TraverseEntityHierarchy(
	SceneEntityBase* entity,
	WitchcraECS* ecs,
	const std::function<void(SceneEntityBase*)>& visitor)
{
	if (entity == nullptr || ecs == nullptr || !visitor)
		return;

	visitor(entity);
	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
	{
		TraverseEntityHierarchy(childEntity, ecs, visitor);
	}
}

void D3DWindow::RebuildRenderItemsFromEntities(WitchcraECS* ecs)
{
	if (!PrepareRenderItemEntityOperation(ecs, true))
		return;

	ClearRenderItems();

	const std::vector<SceneEntityBase*>& rootEntities = ecs->GetSceneRootEntities();
	for (UINT i = 0; i < ecs->GetSceneRootEntityCount(); ++i)
	{
		AppendRenderItemsFromEntity(rootEntities[i], ecs);
	}

	// ClearRenderItems 后重新创建的 RenderItem 默认 SkinningCBIndex 为 UINT(-1)。
	// 全量重建必须同时重排 Object/Skinning CB 索引，否则 UpdateSkinningCBs 会
	// 将所有蒙皮项视为没有常量缓冲槽位而直接跳过。
	RefreshRenderItemCachesAfterStructuralChange(true);
}

void D3DWindow::ResetSceneRuntimeRenderState()
{
	// 这些数据只描述当前场景的 RenderToTexture 运行时目标与绑定关系。
	// 切换/新建场景时必须清空，避免上一场景创建过的 RTT 资源、slot 和矩阵映射
	// 泄漏到新场景，造成冷启动打开场景与热切场景得到不同的资源状态。
	mRenderToTextureManager.Clear();
	mRenderToTextureSlotById.clear();
	mRenderToTextureViewProjTexById.clear();
	mActiveRenderToTextureTargetId = 0;
	mRenderingRenderToTexturePass = false;
}

void D3DWindow::AddRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr)
		return;
	if (!PrepareRenderItemEntityOperation(ecs, true))
		return;

	RemoveRenderItemsFromEntityRecursive(entity, ecs);
	AppendRenderItemsFromEntity(entity, ecs);
	RefreshRenderItemCachesAfterStructuralChange(true);
}

void D3DWindow::RemoveRenderItemsFromEntity(SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	if (mLastExternalECS != nullptr)
	{
		RemoveRenderItemsFromEntity(entity, mLastExternalECS);
		return;
	}

#ifdef _DEBUG
	assert(false && "RemoveRenderItemsFromEntity(entity) 已弃用，请传入有效的 WitchcraECS*。");
#endif
}

void D3DWindow::RemoveRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr)
		return;
	if (!PrepareRenderItemEntityOperation(ecs, false))
		return;

	RemoveRenderItemsFromEntityRecursive(entity, ecs);
	RefreshRenderItemCachesAfterStructuralChange(true);
}

void D3DWindow::UpdateRenderItemsTransformFromEntity(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr)
		return;
	if (!PrepareRenderItemEntityOperation(ecs, true))
		return;

	UpdateRenderItemsTransformFromEntityRecursive(entity, ecs);
}

void D3DWindow::UpdateRenderItemsTransformFromEntityRecursive(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr || ecs == nullptr)
		return;

	RenderDrawSetComponent* renderDrawSetComponent = ecs->GetComponent<RenderDrawSetComponent>(entity);
	if (renderDrawSetComponent != nullptr && !renderDrawSetComponent->GetDraws().empty() && ecs->IsEntityVisible(entity))
	{
		SceneEntityType drawSetSceneType = SceneEntityType::StaticScenery;
		(void)ecs->GetEntitySceneType(entity, &drawSetSceneType);

		for (const RenderDrawSlice& drawSlice : renderDrawSetComponent->GetDraws())
		{
			SceneEntityBase* meshSourceEntity =
				drawSlice.TransformSourceEntity != nullptr ? drawSlice.TransformSourceEntity : entity;
			SceneEntityBase* transformSourceEntity = meshSourceEntity;
			if (drawSlice.IsSkinned)
				transformSourceEntity = ResolveSkinnedRenderTransformEntity(ecs, transformSourceEntity);

			DirectX::XMFLOAT4X4 worldTransform = MathHelps::Identity;
			DirectX::XMFLOAT4X4 texTransform = MathHelps::Identity;
			const bool hasRenderTransforms = BuildEntityRenderTransforms(transformSourceEntity, ecs, &worldTransform, &texTransform);

			RenderItem* renderItem = GetRenderItem(drawSlice.RenderItemName);
			if (renderItem == nullptr)
				continue;

			renderItem->SceneType = drawSetSceneType;
			renderItem->SourceEntity = meshSourceEntity;
			renderItem->CullingSourceEntity = entity;
			renderItem->IsSkinned = drawSlice.IsSkinned;

			if (!hasRenderTransforms)
				continue;

			if (!IsFiniteRenderBridgeMatrix(worldTransform) || !IsFiniteRenderBridgeMatrix(texTransform))
			{
				EmitRenderBridgeDebugMessage(
					L"UpdateRenderItemsTransformFromEntityRecursive_DrawSetInvalidInput",
					drawSlice.RenderItemName,
					ecs,
					transformSourceEntity,
					worldTransform,
					texTransform);
				worldTransform = MathHelps::Identity;
				texTransform = MathHelps::Identity;
			}

			renderItem->WorldTransform = worldTransform;
			renderItem->TexTransform = texTransform;
			renderItem->NumFramesDirty = SwapChainBufferCount;
			DirtyObjectCBItems.insert(drawSlice.RenderItemName);
		}
	}

	EntityRenderView renderView;
	if (ecs->BuildEntityRenderView(entity, &renderView) &&
		(renderView.meshComponent != nullptr || renderView.billboardComponent != nullptr) &&
		renderView.visible)
	{
		SceneEntityBase* transformSourceEntity =
			renderView.isSkinned ? ResolveSkinnedRenderTransformEntity(ecs, entity) : entity;
		DirectX::XMFLOAT4X4 worldTransform = MathHelps::Identity;
		DirectX::XMFLOAT4X4 texTransform = MathHelps::Identity;
		const bool hasRenderTransforms = BuildEntityRenderTransforms(transformSourceEntity, ecs, &worldTransform, &texTransform);

		RenderItem* renderItem = renderView.meshComponent != nullptr ? GetRenderItem(renderView.renderItemName) : nullptr;
		if (renderItem != nullptr)
		{
			renderItem->SceneType = renderView.sceneEntityType;
			renderItem->SourceEntity = entity;
			renderItem->CullingSourceEntity = entity;
			renderItem->IsSkinned = renderView.isSkinned;
			if (hasRenderTransforms)
			{
				if (!IsFiniteRenderBridgeMatrix(worldTransform) || !IsFiniteRenderBridgeMatrix(texTransform))
				{
					EmitRenderBridgeDebugMessage(
						L"UpdateRenderItemsTransformFromEntityRecursive_InvalidInput",
						renderView.renderItemName,
						ecs,
						entity,
						worldTransform,
						texTransform);
					worldTransform = MathHelps::Identity;
					texTransform = MathHelps::Identity;
				}

				renderItem->WorldTransform = worldTransform;
				renderItem->TexTransform = texTransform;
				renderItem->NumFramesDirty = SwapChainBufferCount;
				DirtyObjectCBItems.insert(renderView.renderItemName);
			}
		}

		if (renderView.billboardComponent != nullptr)
		{
			const std::wstring billboardRenderItemName = BuildBillboardRenderItemName(renderView.renderItemName);
			RenderItem* billboardRenderItem = GetRenderItem(billboardRenderItemName);
			if (billboardRenderItem != nullptr && hasRenderTransforms)
			{
				billboardRenderItem->SceneType = renderView.sceneEntityType;
				billboardRenderItem->SourceEntity = entity;
				billboardRenderItem->CullingSourceEntity = entity;
				billboardRenderItem->IsBillboard = true;
				billboardRenderItem->Billboard = renderView.billboardComponent->BuildData();
				billboardRenderItem->BillboardAnchorTransform = worldTransform;
				billboardRenderItem->TexTransform = texTransform;
				billboardRenderItem->NumFramesDirty = SwapChainBufferCount;
				DirtyObjectCBItems.insert(billboardRenderItemName);

				const std::wstring& billboardMaterialName = renderView.billboardComponent->GetMaterialName();
				SetMaterial(
					billboardRenderItemName,
					billboardMaterialName.empty() ? std::wstring(L"autoMat") : billboardMaterialName);
			}
		}
	}

	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
	{
		UpdateRenderItemsTransformFromEntityRecursive(childEntity, ecs);
	}
}

void D3DWindow::RemoveRenderItemsFromEntityRecursive(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr || ecs == nullptr)
		return;

	RenderDrawSetComponent* renderDrawSetComponent = ecs->GetComponent<RenderDrawSetComponent>(entity);
	if (renderDrawSetComponent != nullptr)
	{
		for (const RenderDrawSlice& drawSlice : renderDrawSetComponent->GetDraws())
		{
			EraseRenderItemFromAllLayers(drawSlice.RenderItemName);
			DirtyObjectCBItems.erase(drawSlice.RenderItemName);
			AllRitems.erase(drawSlice.RenderItemName);
			DrawSetObjectCollectionCache.erase(drawSlice.RenderItemName);
			DrawSetAggrObjectCache.erase(drawSlice.RenderItemName);
		}
	}

	EntityRenderView renderView;
	if (ecs->BuildEntityRenderView(entity, &renderView) &&
		(renderView.meshComponent != nullptr || renderView.billboardComponent != nullptr))
	{
		if (renderView.meshComponent != nullptr)
		{
			EraseRenderItemFromAllLayers(renderView.renderItemName);
			DirtyObjectCBItems.erase(renderView.renderItemName);
			AllRitems.erase(renderView.renderItemName);
		}

		if (renderView.billboardComponent != nullptr || renderView.meshComponent != nullptr)
		{
			const std::wstring billboardRenderItemName = BuildBillboardRenderItemName(renderView.renderItemName);
			EraseRenderItemFromAllLayers(billboardRenderItemName);
			DirtyObjectCBItems.erase(billboardRenderItemName);
			AllRitems.erase(billboardRenderItemName);
			BillboardObjectCollectionCache.erase(billboardRenderItemName);
			BillboardAggrObjectCache.erase(billboardRenderItemName);
		}
	}

	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
	{
		RemoveRenderItemsFromEntityRecursive(childEntity, ecs);
	}
}
