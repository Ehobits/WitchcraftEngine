#pragma once

#include "Common/GizmoSharedTypes.h"
#include "Common/TransformSharedTypes.h"

#include <DirectXMath.h>

class D3DWindow;
class WitchcraECS;
class Editor;
class SceneEntityBase;

class EditorTransformGizmo
{
public:
	void SetMode(GizmoMode mode);
	GizmoMode GetMode() const;

	void UpdateSelection(SceneEntityBase* entity);
	void ClearSelection();

	bool IsVisible() const;
	bool IsDragging() const;

	GizmoRenderData BuildRenderData(Editor* editor, WitchcraECS* ecs) const;

	bool TryBeginDrag(
		Editor* editor,
		WitchcraECS* ecs,
		const DirectX::XMVECTOR& rayOriginWS,
		const DirectX::XMVECTOR& rayDirWS);

	bool UpdateDrag(
		Editor* editor,
		WitchcraECS* ecs,
		const DirectX::XMVECTOR& rayOriginWS,
		const DirectX::XMVECTOR& rayDirWS);

	void UpdateHover(
		Editor* editor,
		WitchcraECS* ecs,
		const DirectX::XMVECTOR& rayOriginWS,
		const DirectX::XMVECTOR& rayDirWS);

	void EndDrag();

private:
	struct DragContext
	{
		bool Active = false;
		GizmoMode Mode = GizmoMode::Translate;
		GizmoHandle Handle = GizmoHandle::None;
		SceneEntityBase* Entity = nullptr;
		Transform InitialLocalTransform{};
		DirectX::XMFLOAT4 InitialLocalRotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMFLOAT4 InitialParentRotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMFLOAT4 LastAppliedLocalRotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMFLOAT3 LastAppliedLocalEuler = { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT4X4 InitialWorldMatrix = {};
		DirectX::XMFLOAT4X4 InitialParentWorld = {};
		DirectX::XMFLOAT3 GizmoOriginWS = { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT3 AxisWS = { 1.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT4 DragPlaneWS = { 0.0f, 0.0f, 1.0f, 0.0f };
		DirectX::XMFLOAT3 DragStartHitWS = { 0.0f, 0.0f, 0.0f };
	};

public:
	DragContext* GetDrag();

private:
	bool BuildSelectionWorldOrigin(WitchcraECS* ecs, DirectX::XMFLOAT3* outOriginWS) const;
	float ComputeDrawScale(Editor* editor, const DirectX::XMFLOAT3& originWS) const;
	GizmoHandle ResolveHandleByRay(
		Editor* editor,
		WitchcraECS* ecs,
		const DirectX::XMVECTOR& rayOriginWS,
		const DirectX::XMVECTOR& rayDirWS,
		DirectX::XMFLOAT3* outHitPointWS = nullptr) const;
	DirectX::XMMATRIX TransformToMatrix(const Transform& transform);
	float NormalizeDegrees(float degrees);
	float MakeDegreesClosestToReference(float degrees, float referenceDegrees);
	DirectX::XMVECTOR QuaternionFromEulerDegrees(const DirectX::XMFLOAT3& eulerDegrees);
	DirectX::XMFLOAT3 QuaternionToEulerDegreesClosest(const DirectX::XMVECTOR& quaternion, const DirectX::XMFLOAT3& referenceDegrees);
	DirectX::XMVECTOR ExtractRotationQuaternionFromMatrix(const DirectX::XMFLOAT4X4& matrix);
	const DirectX::XMMATRIX BuildGizmoWorldMatrix(const DirectX::XMFLOAT3& originWS, float drawScale) const;
	float PickGizmoSubmesh(
		const DirectX::XMVECTOR& rayOriginWS,
		const DirectX::XMVECTOR& rayDirWS,
		const GizmoPickMeshData& pickMesh,
		const GizmoSubmeshDesc& submesh,
		const DirectX::XMMATRIX& worldTransform) const;
	float ComputeSignedAngleAroundAxis(
		const DirectX::XMVECTOR& fromVector,
		const DirectX::XMVECTOR& toVector,
		const DirectX::XMVECTOR& axis)  const;
	DirectX::XMFLOAT3 HandleAxis(GizmoHandle handle) const;
	bool IntersectRayPlane(
		const DirectX::XMVECTOR& rayOriginWS,
		const DirectX::XMVECTOR& rayDirWS,
		const DirectX::XMFLOAT4& planeWS,
		DirectX::XMFLOAT3* outHitPointWS) const;
	bool BuildAxisDragPlane(
		Editor* editor,
		const DirectX::XMFLOAT3& originWS,
		const DirectX::XMFLOAT3& axisWS,
		DirectX::XMFLOAT4* outPlaneWS) const;
	bool BuildRotationDragPlane(
		const DirectX::XMFLOAT3& originWS,
		const DirectX::XMFLOAT3& axisWS,
		DirectX::XMFLOAT4* outPlaneWS) const;
	float ComputeRaySegmentDistance(
		const DirectX::XMVECTOR& rayOriginWS,
		const DirectX::XMVECTOR& rayDirWS,
		const DirectX::XMVECTOR& segmentStartWS,
		const DirectX::XMVECTOR& segmentEndWS,
		float* outRayT,
		float* outSegmentT) const;

private:
	GizmoMode mMode = GizmoMode::Translate;
	SceneEntityBase* mSelectedEntity = nullptr;
	GizmoHandle mHoverHandle = GizmoHandle::None;
	DragContext mDrag{};
	float GizmoMinDrawScale = 0.26f;
	float GizmoAxisPickLength = 1.10f;
	float GizmoAxisPickRadius = 0.12f;
	float GizmoScaleDeltaFactor = 0.75f;
};
