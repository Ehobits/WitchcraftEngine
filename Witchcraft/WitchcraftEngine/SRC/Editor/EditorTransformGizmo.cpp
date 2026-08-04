#include "EditorTransformGizmo.h"
#include "Editor.h"

#include "D3DWindow/D3DWindow.h"
#include "ECS/WitchcraECS.h"
#include "Helpers/MathHelpers.h"

#include <DirectXCollision.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

void EditorTransformGizmo::SetMode(GizmoMode mode)
{
	if (mMode == mode)
		return;
	mMode = mode;
	mHoverHandle = GizmoHandle::None;
	if (mDrag.Active)
		EndDrag();
}

GizmoMode EditorTransformGizmo::GetMode() const
{
	return mMode;
}

void EditorTransformGizmo::UpdateSelection(SceneEntityBase* entity)
{
	if (mSelectedEntity != entity && mDrag.Active)
		EndDrag();
	if (mSelectedEntity != entity)
		mHoverHandle = GizmoHandle::None;
	mSelectedEntity = entity;
}

void EditorTransformGizmo::ClearSelection()
{
	mSelectedEntity = nullptr;
	mHoverHandle = GizmoHandle::None;
	EndDrag();
}

bool EditorTransformGizmo::IsVisible() const
{
	return mSelectedEntity != nullptr &&
		(mMode == GizmoMode::Translate || mMode == GizmoMode::Rotate || mMode == GizmoMode::Scale);
}

bool EditorTransformGizmo::IsDragging() const
{
	return mDrag.Active;
}

GizmoRenderData EditorTransformGizmo::BuildRenderData(Editor* editor, WitchcraECS* ecs) const
{
	GizmoRenderData result;
	if (!IsVisible() || ecs == nullptr)
		return result;

	DirectX::XMFLOAT3 originWS{};
	if (!BuildSelectionWorldOrigin(ecs, &originWS))
		return result;

	result.Visible = true;
	result.Mode = mMode;
	result.HoverHandle = mDrag.Active ? GizmoHandle::None : mHoverHandle;
	result.ActiveHandle = mDrag.Active ? mDrag.Handle : GizmoHandle::None;
	result.OriginWS = originWS;
	result.DrawScale = ComputeDrawScale(editor, originWS);
	return result;
}

bool EditorTransformGizmo::TryBeginDrag(
	Editor* editor,
	WitchcraECS* ecs,
	const DirectX::XMVECTOR& rayOriginWS,
	const DirectX::XMVECTOR& rayDirWS)
{
	if (!IsVisible() || ecs == nullptr || mSelectedEntity == nullptr)
		return false;

	DirectX::XMFLOAT3 hitPointWS{};
	const GizmoHandle handle = ResolveHandleByRay(editor, ecs, rayOriginWS, rayDirWS, &hitPointWS);
	if (handle == GizmoHandle::None)
		return false;

	DirectX::XMFLOAT3 originWS{};
	Transform initialLocalTransform{};
	if (!BuildSelectionWorldOrigin(ecs, &originWS) ||
		!ecs->GetEntityEditableLocalTransform(mSelectedEntity, &initialLocalTransform))
	{
		return false;
	}

	DirectX::XMFLOAT4X4 parentWorld = MathHelps::Identity;
	if (SceneEntityBase* parentEntity = ecs->GetParentEntity(mSelectedEntity))
		ecs->GetEntityRenderMatrix(parentEntity, &parentWorld);

	DirectX::XMFLOAT4X4 initialWorldMatrix = MathHelps::Identity;
	DirectX::XMStoreFloat4x4(
		&initialWorldMatrix,
		TransformToMatrix(initialLocalTransform) * DirectX::XMLoadFloat4x4(&parentWorld));

	DirectX::XMFLOAT4 dragPlaneWS{};
	const DirectX::XMFLOAT3 axisWS = HandleAxis(handle);
	if (mMode == GizmoMode::Rotate)
	{
		if (!BuildRotationDragPlane(originWS, axisWS, &dragPlaneWS))
			return false;
	}
	else
	{
		if (!BuildAxisDragPlane(editor, originWS, axisWS, &dragPlaneWS))
			return false;
	}

	mDrag.Active = true;
	mDrag.Mode = mMode;
	mDrag.Handle = handle;
	mDrag.Entity = mSelectedEntity;
	mDrag.InitialLocalTransform = initialLocalTransform;
	{
		const DirectX::XMVECTOR initialLocalRotationQuat = QuaternionFromEulerDegrees(initialLocalTransform.rotation);
		const DirectX::XMVECTOR initialParentRotationQuat = ExtractRotationQuaternionFromMatrix(parentWorld);
		DirectX::XMStoreFloat4(&mDrag.InitialLocalRotationQuat, initialLocalRotationQuat);
		DirectX::XMStoreFloat4(&mDrag.InitialParentRotationQuat, initialParentRotationQuat);
		DirectX::XMStoreFloat4(&mDrag.LastAppliedLocalRotationQuat, initialLocalRotationQuat);
		mDrag.LastAppliedLocalEuler = initialLocalTransform.rotation;
	}
	mDrag.InitialWorldMatrix = initialWorldMatrix;
	mDrag.InitialParentWorld = parentWorld;
	mDrag.GizmoOriginWS = originWS;
	mDrag.AxisWS = axisWS;
	mDrag.DragPlaneWS = dragPlaneWS;
	mDrag.DragStartHitWS = hitPointWS;
	mHoverHandle = GizmoHandle::None;
	return true;
}

bool EditorTransformGizmo::UpdateDrag(
	Editor* editor,
	WitchcraECS* ecs,
	const DirectX::XMVECTOR& rayOriginWS,
	const DirectX::XMVECTOR& rayDirWS)
{
	if (!mDrag.Active || ecs == nullptr || mDrag.Entity == nullptr)
		return false;

	DirectX::XMFLOAT3 currentHitWS{};
	if (!IntersectRayPlane(rayOriginWS, rayDirWS, mDrag.DragPlaneWS, &currentHitWS))
		return false;

	const DirectX::XMVECTOR axis = DirectX::XMLoadFloat3(&mDrag.AxisWS);
	const DirectX::XMVECTOR start = DirectX::XMLoadFloat3(&mDrag.DragStartHitWS);
	const DirectX::XMVECTOR current = DirectX::XMLoadFloat3(&currentHitWS);
	Transform updatedTransform = mDrag.InitialLocalTransform;

	switch (mDrag.Mode)
	{
	case GizmoMode::Translate:
	{
		const float axisDelta = DirectX::XMVectorGetX(DirectX::XMVector3Dot(current - start, axis));
		const DirectX::XMVECTOR gizmoOrigin = DirectX::XMLoadFloat3(&mDrag.GizmoOriginWS);
		const DirectX::XMVECTOR targetWorldPosition = gizmoOrigin + axis * axisDelta;

		const XMMATRIX parentWorld = DirectX::XMLoadFloat4x4(&mDrag.InitialParentWorld);
		const XMMATRIX inverseParent = DirectX::XMMatrixInverse(nullptr, parentWorld);
		const DirectX::XMVECTOR targetLocalPosition = DirectX::XMVector3TransformCoord(targetWorldPosition, inverseParent);
		DirectX::XMStoreFloat3(&updatedTransform.position, targetLocalPosition);
		updatedTransform.rotation = mDrag.InitialLocalTransform.rotation;
		updatedTransform.scale = mDrag.InitialLocalTransform.scale;
		break;
	}
	case GizmoMode::Scale:
	{
		const float axisDelta = DirectX::XMVectorGetX(DirectX::XMVector3Dot(current - start, axis));
		const float scaleDelta = axisDelta * GizmoScaleDeltaFactor;
		switch (mDrag.Handle)
		{
		case GizmoHandle::AxisX:
			updatedTransform.scale.x = (std::max)(0.0f, mDrag.InitialLocalTransform.scale.x + scaleDelta);
			break;
		case GizmoHandle::AxisY:
			updatedTransform.scale.y = (std::max)(0.0f, mDrag.InitialLocalTransform.scale.y + scaleDelta);
			break;
		case GizmoHandle::AxisZ:
			updatedTransform.scale.z = (std::max)(0.0f, mDrag.InitialLocalTransform.scale.z + scaleDelta);
			break;
		default:
			return false;
		}
		break;
	}
	case GizmoMode::Rotate:
	{
		const DirectX::XMVECTOR gizmoOrigin = DirectX::XMLoadFloat3(&mDrag.GizmoOriginWS);
		const DirectX::XMVECTOR startVector = start - gizmoOrigin;
		const DirectX::XMVECTOR currentVector = current - gizmoOrigin;
		if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(startVector)) < 1e-6f ||
			DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(currentVector)) < 1e-6f)
		{
			return false;
		}

		const float signedAngleRadians = ComputeSignedAngleAroundAxis(startVector, currentVector, axis);
		const DirectX::XMVECTOR deltaWorldRotationQuat = DirectX::XMQuaternionNormalize(DirectX::XMQuaternionRotationAxis(axis, signedAngleRadians));
		const DirectX::XMVECTOR initialLocalRotationQuat = DirectX::XMLoadFloat4(&mDrag.InitialLocalRotationQuat);
		const DirectX::XMVECTOR initialParentRotationQuat = DirectX::XMLoadFloat4(&mDrag.InitialParentRotationQuat);
		const DirectX::XMVECTOR initialWorldRotationQuat =
			DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(initialLocalRotationQuat, initialParentRotationQuat));
		DirectX::XMVECTOR targetWorldRotationQuat =
			DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(initialWorldRotationQuat, deltaWorldRotationQuat));
		const DirectX::XMVECTOR inverseParentRotationQuat = DirectX::XMQuaternionInverse(initialParentRotationQuat);
		DirectX::XMVECTOR targetLocalRotationQuat =
			DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(targetWorldRotationQuat, inverseParentRotationQuat));

		const DirectX::XMVECTOR lastAppliedLocalRotationQuat = DirectX::XMLoadFloat4(&mDrag.LastAppliedLocalRotationQuat);
		if (DirectX::XMVectorGetX(XMVector4Dot(targetLocalRotationQuat, lastAppliedLocalRotationQuat)) < 0.0f)
			targetLocalRotationQuat = DirectX::XMVectorNegate(targetLocalRotationQuat);

		updatedTransform.rotation =
			QuaternionToEulerDegreesClosest(targetLocalRotationQuat, mDrag.LastAppliedLocalEuler);
		DirectX::XMStoreFloat4(&mDrag.LastAppliedLocalRotationQuat, targetLocalRotationQuat);
		mDrag.LastAppliedLocalEuler = updatedTransform.rotation;
		break;
	}
	default:
		return false;
	}

	if (!ecs->SetEntityEditableLocalTransform(mDrag.Entity, updatedTransform))
		return false;

	return true;
}

void EditorTransformGizmo::UpdateHover(
	Editor* editor,
	WitchcraECS* ecs,
	const DirectX::XMVECTOR& rayOriginWS,
	const DirectX::XMVECTOR& rayDirWS)
{
	if (mDrag.Active)
		return;
	if (!IsVisible() || ecs == nullptr || mSelectedEntity == nullptr)
	{
		mHoverHandle = GizmoHandle::None;
		return;
	}

	mHoverHandle = ResolveHandleByRay(editor, ecs, rayOriginWS, rayDirWS, nullptr);
}

void EditorTransformGizmo::EndDrag()
{
	mDrag = DragContext{};
}

EditorTransformGizmo::DragContext* EditorTransformGizmo::GetDrag()
{
	return &mDrag;
}

bool EditorTransformGizmo::BuildSelectionWorldOrigin(WitchcraECS* ecs, DirectX::XMFLOAT3* outOriginWS) const
{
	if (ecs == nullptr || outOriginWS == nullptr || mSelectedEntity == nullptr)
		return false;

	DirectX::XMFLOAT4X4 worldMatrix = MathHelps::Identity;
	if (!ecs->GetEntityRenderMatrix(mSelectedEntity, &worldMatrix))
		return false;

	outOriginWS->x = worldMatrix._41;
	outOriginWS->y = worldMatrix._42;
	outOriginWS->z = worldMatrix._43;
	return true;
}

float EditorTransformGizmo::ComputeDrawScale(Editor* editor, const DirectX::XMFLOAT3& originWS) const
{
	const DirectX::XMFLOAT3 cameraPosition = editor->GetD3DWindow()->GetPosition3f();
	const DirectX::XMVECTOR cameraPos = DirectX::XMLoadFloat3(&cameraPosition);
	const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&originWS);
	const float distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(origin - cameraPos));
	return (std::max)(GizmoMinDrawScale, distance * 0.10f);
}

GizmoHandle EditorTransformGizmo::ResolveHandleByRay(
	Editor* editor,
	WitchcraECS* ecs,
	const DirectX::XMVECTOR& rayOriginWS,
	const DirectX::XMVECTOR& rayDirWS,
	DirectX::XMFLOAT3* outHitPointWS) const
{
	if (ecs == nullptr)
		return GizmoHandle::None;

	DirectX::XMFLOAT3 originWS{};
	if (!BuildSelectionWorldOrigin(ecs, &originWS))
		return GizmoHandle::None;

	const float drawScale = ComputeDrawScale(editor, originWS);
	const DirectX::XMMATRIX gizmoWorldTransform = BuildGizmoWorldMatrix(originWS, drawScale);
	const GizmoPickMeshData& pickMesh = editor->GetD3DWindow()->GetTransformGizmoPickMesh(mMode);

	GizmoHandle bestHandle = GizmoHandle::None;
	float bestDistanceAlongRay = FLT_MAX;
	DirectX::XMFLOAT3 bestHitPoint{};

	for (const GizmoSubmeshDesc& submesh : pickMesh.Submeshes)
	{
		float hitDistance = PickGizmoSubmesh(
			rayOriginWS,
			rayDirWS,
			pickMesh,
			submesh,
			gizmoWorldTransform);
		if (hitDistance >= bestDistanceAlongRay)
			continue;

		bestDistanceAlongRay = hitDistance;
		bestHandle = submesh.Handle;
		DirectX::XMVECTOR hitPoint = rayOriginWS + rayDirWS * hitDistance;
		DirectX::XMStoreFloat3(&bestHitPoint, hitPoint);
	}

	if (bestHandle == GizmoHandle::None && mMode != GizmoMode::Rotate)
	{
		const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&originWS);
		const GizmoHandle handles[] = { GizmoHandle::AxisX, GizmoHandle::AxisY, GizmoHandle::AxisZ };
		for (GizmoHandle handle : handles)
		{
			const DirectX::XMFLOAT3 axisWS = HandleAxis(handle);
			const DirectX::XMVECTOR axis = DirectX::XMLoadFloat3(&axisWS);
			const DirectX::XMVECTOR segmentEnd = origin + axis * (GizmoAxisPickLength * drawScale);

			float rayT = 0.0f;
			float segmentT = 0.0f;
			const float distance = ComputeRaySegmentDistance(rayOriginWS, rayDirWS, origin, segmentEnd, &rayT, &segmentT);
			if (rayT < 0.0f || segmentT < 0.0f || segmentT > 1.0f)
				continue;
			if (distance > GizmoAxisPickRadius * drawScale || rayT >= bestDistanceAlongRay)
				continue;

			bestDistanceAlongRay = rayT;
			bestHandle = handle;
			DirectX::XMVECTOR hitPoint = rayOriginWS + rayDirWS * rayT;
			DirectX::XMStoreFloat3(&bestHitPoint, hitPoint);
		}
	}

	if (bestHandle != GizmoHandle::None && outHitPointWS != nullptr)
		*outHitPointWS = bestHitPoint;
	return bestHandle;
}

DirectX::XMMATRIX EditorTransformGizmo::TransformToMatrix(const Transform& transform)
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

float EditorTransformGizmo::NormalizeDegrees(float degrees)
{
	while (degrees > 180.0f)
		degrees -= 360.0f;
	while (degrees < -180.0f)
		degrees += 360.0f;
	return degrees;
}

float EditorTransformGizmo::MakeDegreesClosestToReference(float degrees, float referenceDegrees)
{
	degrees = NormalizeDegrees(degrees);
	referenceDegrees = NormalizeDegrees(referenceDegrees);

	while ((degrees - referenceDegrees) > 180.0f)
		degrees -= 360.0f;
	while ((degrees - referenceDegrees) < -180.0f)
		degrees += 360.0f;

	return degrees;
}

DirectX::XMVECTOR EditorTransformGizmo::QuaternionFromEulerDegrees(const DirectX::XMFLOAT3& eulerDegrees)
{
	return DirectX::XMQuaternionRotationRollPitchYaw(
		DirectX::XMConvertToRadians(eulerDegrees.x),
		DirectX::XMConvertToRadians(eulerDegrees.y),
		DirectX::XMConvertToRadians(eulerDegrees.z));
}

DirectX::XMFLOAT3 EditorTransformGizmo::QuaternionToEulerDegreesClosest(const DirectX::XMVECTOR& quaternion, const DirectX::XMFLOAT3& referenceDegrees)
{
	DirectX::XMFLOAT4 rotation{};
	DirectX::XMStoreFloat4(&rotation, DirectX::XMQuaternionNormalize(quaternion));

	const float pitch = std::atan2(
		2.0f * (rotation.w * rotation.x + rotation.y * rotation.z),
		1.0f - 2.0f * (rotation.x * rotation.x + rotation.y * rotation.y));
	const float yaw = std::asin(MathHelps::ClampUnit(2.0f * (rotation.w * rotation.y - rotation.z * rotation.x)));
	const float roll = std::atan2(
		2.0f * (rotation.w * rotation.z + rotation.x * rotation.y),
		1.0f - 2.0f * (rotation.y * rotation.y + rotation.z * rotation.z));

	const DirectX::XMFLOAT3 solutionA(
		MakeDegreesClosestToReference(DirectX::XMConvertToDegrees(pitch), referenceDegrees.x),
		MakeDegreesClosestToReference(DirectX::XMConvertToDegrees(yaw), referenceDegrees.y),
		MakeDegreesClosestToReference(DirectX::XMConvertToDegrees(roll), referenceDegrees.z));

	const DirectX::XMFLOAT3 solutionB(
		MakeDegreesClosestToReference(DirectX::XMConvertToDegrees(pitch + DirectX::XM_PI), referenceDegrees.x),
		MakeDegreesClosestToReference(DirectX::XMConvertToDegrees(DirectX::XM_PI - yaw), referenceDegrees.y),
		MakeDegreesClosestToReference(DirectX::XMConvertToDegrees(roll + DirectX::XM_PI), referenceDegrees.z));

	const float solutionADistance =
		std::abs(solutionA.x - referenceDegrees.x) +
		std::abs(solutionA.y - referenceDegrees.y) +
		std::abs(solutionA.z - referenceDegrees.z);
	const float solutionBDistance =
		std::abs(solutionB.x - referenceDegrees.x) +
		std::abs(solutionB.y - referenceDegrees.y) +
		std::abs(solutionB.z - referenceDegrees.z);

	return solutionBDistance < solutionADistance ? solutionB : solutionA;
}

DirectX::XMVECTOR EditorTransformGizmo::ExtractRotationQuaternionFromMatrix(const DirectX::XMFLOAT4X4& matrix)
{
	const DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&matrix);
	DirectX::XMVECTOR scale = DirectX::XMVectorZero();
	DirectX::XMVECTOR rotation = DirectX::XMQuaternionIdentity();
	DirectX::XMVECTOR translation = DirectX::XMVectorZero();
	if (!DirectX::XMMatrixDecompose(&scale, &rotation, &translation, m))
		return DirectX::XMQuaternionIdentity();
	return DirectX::XMQuaternionNormalize(rotation);
}

const DirectX::XMMATRIX EditorTransformGizmo::BuildGizmoWorldMatrix(const DirectX::XMFLOAT3& originWS, float drawScale) const
{
	return DirectX::XMMatrixScaling(drawScale, drawScale, drawScale) *
		DirectX::XMMatrixTranslation(originWS.x, originWS.y, originWS.z);
}

float EditorTransformGizmo::ComputeSignedAngleAroundAxis(
	const DirectX::XMVECTOR& fromVector,
	const DirectX::XMVECTOR& toVector,
	const DirectX::XMVECTOR& axis) const
{
	const DirectX::XMVECTOR fromN = DirectX::XMVector3Normalize(fromVector);
	const DirectX::XMVECTOR toN = DirectX::XMVector3Normalize(toVector);
	const float dotValue = MathHelps::ClampUnit(DirectX::XMVectorGetX(DirectX::XMVector3Dot(fromN, toN)));
	const float angle = std::acos(dotValue);
	const float sign = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVector3Cross(fromN, toN), axis)) >= 0.0f ? 1.0f : -1.0f;
	return angle * sign;
}

float EditorTransformGizmo::PickGizmoSubmesh(
	const DirectX::XMVECTOR& rayOriginWS,
	const DirectX::XMVECTOR& rayDirWS,
	const GizmoPickMeshData& pickMesh,
	const GizmoSubmeshDesc& submesh,
	const DirectX::XMMATRIX& worldTransform) const
{
	if (submesh.IndexCount < 3u)
		return FLT_MAX;
	if (pickMesh.Vertices.empty() || pickMesh.Indices.empty())
		return FLT_MAX;
	if ((submesh.IndexStart + submesh.IndexCount) > pickMesh.Indices.size())
		return FLT_MAX;

	float nearestDistance = FLT_MAX;
	for (std::uint32_t indexOffset = 0; indexOffset + 2u < submesh.IndexCount; indexOffset += 3u)
	{
		const std::uint32_t i0 = pickMesh.Indices[submesh.IndexStart + indexOffset + 0u] + submesh.BaseVertex;
		const std::uint32_t i1 = pickMesh.Indices[submesh.IndexStart + indexOffset + 1u] + submesh.BaseVertex;
		const std::uint32_t i2 = pickMesh.Indices[submesh.IndexStart + indexOffset + 2u] + submesh.BaseVertex;
		if (i0 >= pickMesh.Vertices.size() || i1 >= pickMesh.Vertices.size() || i2 >= pickMesh.Vertices.size())
			continue;

		const DirectX::XMFLOAT3& v0 = pickMesh.Vertices[i0].Position;
		const DirectX::XMFLOAT3& v1 = pickMesh.Vertices[i1].Position;
		const DirectX::XMFLOAT3& v2 = pickMesh.Vertices[i2].Position;

		const DirectX::XMVECTOR triV0 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v0.x, v0.y, v0.z, 1.0f), worldTransform);
		const DirectX::XMVECTOR triV1 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v1.x, v1.y, v1.z, 1.0f), worldTransform);
		const DirectX::XMVECTOR triV2 = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(v2.x, v2.y, v2.z, 1.0f), worldTransform);

		float distance = 0.0f;
		if (!TriangleTests::Intersects(rayOriginWS, rayDirWS, triV0, triV1, triV2, distance))
			continue;
		if (distance <= 0.0f || distance >= nearestDistance)
			continue;

		nearestDistance = distance;
	}

	return nearestDistance;
}

DirectX::XMFLOAT3 EditorTransformGizmo::HandleAxis(GizmoHandle handle) const
{
	switch (handle)
	{
	case GizmoHandle::AxisX:
		return DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f);
	case GizmoHandle::AxisY:
		return DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
	case GizmoHandle::AxisZ:
		return DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f);
	default:
		return DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f);
	}
}

bool EditorTransformGizmo::IntersectRayPlane(
	const DirectX::XMVECTOR& rayOriginWS,
	const DirectX::XMVECTOR& rayDirWS,
	const DirectX::XMFLOAT4& planeWS,
	DirectX::XMFLOAT3* outHitPointWS) const
{
	const DirectX::XMVECTOR normal = DirectX::XMVectorSet(planeWS.x, planeWS.y, planeWS.z, 0.0f);
	const float denominator = DirectX::XMVectorGetX(DirectX::XMVector3Dot(rayDirWS, normal));
	if (std::abs(denominator) < 1e-5f)
		return false;

	const float numerator = -(DirectX::XMVectorGetX(DirectX::XMVector3Dot(rayOriginWS, normal)) + planeWS.w);
	const float t = numerator / denominator;
	if (t < 0.0f)
		return false;

	if (outHitPointWS != nullptr)
		DirectX::XMStoreFloat3(outHitPointWS, rayOriginWS + rayDirWS * t);
	return true;
}

bool EditorTransformGizmo::BuildAxisDragPlane(
	Editor* editor,
	const DirectX::XMFLOAT3& originWS,
	const DirectX::XMFLOAT3& axisWS,
	DirectX::XMFLOAT4* outPlaneWS) const
{
	if (outPlaneWS == nullptr)
		return false;

	const DirectX::XMFLOAT3 cameraPosition = editor->GetD3DWindow()->GetPosition3f();
	const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&originWS);
	const DirectX::XMVECTOR axis = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&axisWS));
	DirectX::XMVECTOR viewDir = origin - DirectX::XMLoadFloat3(&cameraPosition);
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(viewDir)) < 1e-6f)
		viewDir = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	viewDir = DirectX::XMVector3Normalize(viewDir);

	DirectX::XMVECTOR planeNormal = DirectX::XMVector3Cross(axis, DirectX::XMVector3Cross(viewDir, axis));
	if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(planeNormal)) < 1e-6f)
	{
		const DirectX::XMVECTOR fallback = std::abs(axisWS.x) < 0.9f ?
			DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f) :
			DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		planeNormal = DirectX::XMVector3Cross(axis, fallback);
	}

	planeNormal = DirectX::XMVector3Normalize(planeNormal);
	DirectX::XMFLOAT3 planeNormalStorage{};
	DirectX::XMStoreFloat3(&planeNormalStorage, planeNormal);

	outPlaneWS->x = planeNormalStorage.x;
	outPlaneWS->y = planeNormalStorage.y;
	outPlaneWS->z = planeNormalStorage.z;
	outPlaneWS->w = -DirectX::XMVectorGetX(DirectX::XMVector3Dot(origin, planeNormal));
	return true;
}

bool EditorTransformGizmo::BuildRotationDragPlane(
	const DirectX::XMFLOAT3& originWS,
	const DirectX::XMFLOAT3& axisWS,
	DirectX::XMFLOAT4* outPlaneWS) const
{
	if (outPlaneWS == nullptr)
		return false;

	const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&originWS);
	const DirectX::XMVECTOR axis = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&axisWS));
	DirectX::XMFLOAT3 axisStorage{};
	DirectX::XMStoreFloat3(&axisStorage, axis);
	outPlaneWS->x = axisStorage.x;
	outPlaneWS->y = axisStorage.y;
	outPlaneWS->z = axisStorage.z;
	outPlaneWS->w = -DirectX::XMVectorGetX(DirectX::XMVector3Dot(origin, axis));
	return true;
}

float EditorTransformGizmo::ComputeRaySegmentDistance(
	const DirectX::XMVECTOR& rayOriginWS,
	const DirectX::XMVECTOR& rayDirWS,
	const DirectX::XMVECTOR& segmentStartWS,
	const DirectX::XMVECTOR& segmentEndWS,
	float* outRayT,
	float* outSegmentT) const
{
	const DirectX::XMVECTOR segmentDir = segmentEndWS - segmentStartWS;
	const float segmentLength = DirectX::XMVectorGetX(DirectX::XMVector3Length(segmentDir));
	if (segmentLength < 1e-6f)
		return FLT_MAX;

	const DirectX::XMVECTOR segmentDirNormalized = segmentDir / segmentLength;
	const DirectX::XMVECTOR r = rayOriginWS - segmentStartWS;

	const float a = DirectX::XMVectorGetX(DirectX::XMVector3Dot(rayDirWS, rayDirWS));
	const float e = DirectX::XMVectorGetX(DirectX::XMVector3Dot(segmentDirNormalized, segmentDirNormalized));
	const float b = DirectX::XMVectorGetX(DirectX::XMVector3Dot(rayDirWS, segmentDirNormalized));
	const float c = DirectX::XMVectorGetX(DirectX::XMVector3Dot(rayDirWS, r));
	const float f = DirectX::XMVectorGetX(DirectX::XMVector3Dot(segmentDirNormalized, r));
	const float denom = a * e - b * b;

	float s = 0.0f;
	float t = 0.0f;
	if (std::abs(denom) > 1e-6f)
		s = (b * f - c * e) / denom;
	t = (b * s + f) / e;

	if (s < 0.0f)
		s = 0.0f;
	t = (std::max)(0.0f, (std::min)(segmentLength, t));

	const DirectX::XMVECTOR closestPointRay = rayOriginWS + rayDirWS * s;
	const DirectX::XMVECTOR closestPointSegment = segmentStartWS + segmentDirNormalized * t;
	if (outRayT != nullptr)
		*outRayT = s;
	if (outSegmentT != nullptr)
		*outSegmentT = segmentLength > 1e-6f ? (t / segmentLength) : 0.0f;
	return DirectX::XMVectorGetX(DirectX::XMVector3Length(closestPointRay - closestPointSegment));
}
