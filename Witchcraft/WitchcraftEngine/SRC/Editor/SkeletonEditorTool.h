#pragma once

#include <Windows.h>

#include <DirectXMath.h>
#include "Common/GizmoSharedTypes.h"

#include <cstdint>
#include <xstring>
#include <vector>
#include <chrono>
class D3DWindow;
class Engine;
class Editor;
class MouseClass;
class MouseEvent;

namespace Witchcraft::Animation
{
	struct VertexBoneInfluence4;
	struct SkeletonTopology;
}

struct SkeletonJoint
{
	std::wstring Name;
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4 Rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
	int ParentIndex = -1;
};

enum class SkeletonInteractionMode
{
	AddChild = 0,
	MoveJoint,
	RotateJoint,
	BrushWeight
};

enum class SkeletonDragMode
{
	None = 0,
	AddChild,
	MoveJoint,
	RotateJoint,
	BrushWeight
};

class SkeletonEditorTool
{
public:
	void SetEnabled(bool enabled);
	void LoadFromTopology(
		const Witchcraft::Animation::SkeletonTopology& topology,
		const std::vector<DirectX::XMFLOAT4X4>* globalPose = nullptr,
		const DirectX::XMFLOAT4X4* ownerWorldMatrix = nullptr);
	bool UpdateJointPositionsFromGlobalPose(
		const std::vector<DirectX::XMFLOAT4X4>& globalPose,
		const DirectX::XMFLOAT4X4& ownerWorldMatrix);
	bool UpdateJointPositionsFromBindPose(
		const Witchcraft::Animation::SkeletonTopology& topology,
		const DirectX::XMFLOAT4X4& ownerWorldMatrix);
	void SelectJoint(int jointIndex);
	bool HasPendingChanges() const;
	void ClearPendingChanges();
	bool ConsumeSaveToModelRequest();
	const std::vector<SkeletonJoint>& GetJoints() const;
	int GetSelectedJointIndex() const;
	SkeletonInteractionMode GetInteractionMode() const;
	bool WantsMouseCapture(const MouseEvent& me, MouseClass* mouse) const;
	bool HandleMouse(const MouseEvent& me, MouseClass* mouse, Engine* engine, D3DWindow* dx, HWND hwnd);
	bool HasActiveJointSelection() const;
	bool IsBrushDragging() const;
	void SetGizmoMode(GizmoMode mode);
	void SetBrushWeightModeEnabled(bool enabled);
	bool IsBrushWeightModeEnabled() const;
	void SetBrushRadius(float radius);
	float GetBrushRadius() const;
	void SetBrushStrength(float strength);
	float GetBrushStrength() const;
	void SetBrushFalloff(float falloff);
	float GetBrushFalloff() const;
	void SetBrushIncludeChildren(bool includeChildren);
	bool GetBrushIncludeChildren() const;
	bool ApplyBrushWeight(const DirectX::XMFLOAT3& brushRayOriginWS, const DirectX::XMFLOAT3& brushRayDirWS, D3DWindow* dx);
	void SetEditor(Editor* editor);
	Editor* GetEditor() const;
	bool IsBrushWeightJointLocked() const;
	void SetBrushWeightJointLockEnabled(bool enabled);
	GizmoMode GetGizmoMode() const;
	bool IsGizmoVisible() const;
	bool TryBeginGizmoDrag(
		Engine* engine,
		D3DWindow* dx,
		HWND hwnd,
		const DirectX::XMVECTOR& rayWorldPos,
		const DirectX::XMVECTOR& rayWorldDir);
	bool UpdateGizmoDrag(const DirectX::XMVECTOR& rayWorldPos, const DirectX::XMVECTOR& rayWorldDir);
	void UpdateGizmoHover(const DirectX::XMVECTOR& rayWorldPos, const DirectX::XMVECTOR& rayWorldDir);
	void EndGizmoDrag();
	GizmoRenderData BuildGizmoRenderData(D3DWindow* dx) const;
	void UpdateOverlay(D3DWindow* dx);
	void RenderWindow(float imguiDpiScale, bool* open);

private:
	void CancelActiveDrag();
	bool ResolveHandleByRay(
		const DirectX::XMVECTOR& rayWorldPos,
		const DirectX::XMVECTOR& rayWorldDir,
		GizmoHandle* outHandle,
		DirectX::XMFLOAT3* outHitPointWS) const;
	bool BuildAxisDragPlane(
		const DirectX::XMFLOAT3& axisWS,
		const DirectX::XMFLOAT3& originWS,
		const DirectX::XMFLOAT3& cameraPosition,
		DirectX::XMFLOAT4* outPlaneWS) const;
	void GatherSubtreeJointIndices(int rootJointIndex, bool includeRoot, std::vector<int>* outJointIndices) const;
	void ResetChain();
	bool IsJointIndexValid(int jointIndex) const;
	std::wstring MakeDefaultJointName() const;
	DirectX::XMFLOAT4 GetBoneDisplayColor(int boneIndex) const;

private:
	Editor* m_editor = nullptr;

	bool m_enabled = false;
	bool m_isDragging = false;
	bool m_hasPendingChanges = false;
	SkeletonInteractionMode m_interactionMode = SkeletonInteractionMode::AddChild;
	SkeletonDragMode m_dragMode = SkeletonDragMode::None;
	bool m_brushWeightModeEnabled = false;
	float m_brushRadius = 1.5f;
	float m_brushStrength = 0.35f;
	float m_brushFalloff = 2.0f;
	bool m_brushIncludeChildren = true;
	std::vector<SkeletonJoint> m_joints;
	int m_hoveredJointIndex = -1;
	int m_selectedJointIndex = -1;
	int m_dragParentJointIndex = -1;
	int m_dragTargetJointIndex = -1;
	DirectX::XMFLOAT3 m_dragPlanePointWS = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 m_dragPlaneNormalWS = { 0.0f, 0.0f, 1.0f };
	DirectX::XMFLOAT3 m_boneStartWS = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 m_boneEndWS = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 m_moveDragStartHitWS = { 0.0f, 0.0f, 0.0f };
	std::vector<int> m_moveDragJointIndices;
	std::vector<DirectX::XMFLOAT3> m_moveDragOriginalPositions;
	DirectX::XMFLOAT3 m_rotateDragStartHitWS = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 m_rotatePivotWS = { 0.0f, 0.0f, 0.0f };
	std::vector<int> m_rotateDragJointIndices;
	std::vector<DirectX::XMFLOAT3> m_rotateDragOriginalPositions;
	DirectX::XMFLOAT4 m_rotateDragOriginalTargetRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	bool m_saveToModelRequested = false;
	GizmoMode m_gizmoMode = GizmoMode::Translate;
	GizmoHandle m_gizmoHoverHandle = GizmoHandle::None;
	GizmoHandle m_gizmoActiveHandle = GizmoHandle::None;
	bool m_gizmoDragging = false;
	DirectX::XMFLOAT3 m_gizmoDragAxisWS = { 1.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4 m_gizmoDragPlaneWS = { 0.0f, 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT3 m_gizmoDragStartHitWS = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 m_gizmoDragStartTargetPositionWS = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4 m_gizmoDragStartTargetRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	std::vector<int> m_gizmoDragJointIndices;
	std::vector<DirectX::XMFLOAT3> m_gizmoDragOriginalPositions;
	DirectX::XMFLOAT3 m_gizmoDragRotatePivotWS = { 0.0f, 0.0f, 0.0f };
	POINT m_lastBrushSampleScreenPos = { 0, 0 };
	std::chrono::steady_clock::time_point m_lastBrushSampleTime{};
	bool m_hasLastBrushSample = false;
	// 覆盖层数据仅在姿势或交互状态发生变化时才重建/上传。
	std::uint64_t m_lastOverlayStateHash = 0;
	bool m_hasUploadedOverlay = false;
};
