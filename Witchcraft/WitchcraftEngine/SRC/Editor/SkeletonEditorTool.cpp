#include "SkeletonEditorTool.h"

#include "D3DWindow/D3DWindow.h"
#include "Common/SkeletonSharedTypes.h"
#include "Common/SkinningSharedTypes.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/SkinnedMeshComponent.h"
#include "ECS/WitchcraECS.h"
#include "Editor/Editor.h"
#include "Engine/Engine.h"
#include "Engine/EngineUtils.h"
#include "String/SStringUtils.h"
#include "UserInput/Mouse/MouseClass.h"
#include "UserInput/Mouse/MouseEvent.h"

#include <algorithm>
#include <cmath>
#include <DirectXCollision.h>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>
#include <IconsFontAwesome5.h>
#include <imgui.h>

namespace SkeletonEditorToolDetail
{
	DirectX::XMFLOAT3 NormalizeOrFallback(const DirectX::XMFLOAT3& value, const DirectX::XMFLOAT3& fallback)
	{
		const XMVECTOR vectorValue = XMLoadFloat3(&value);
		const float lengthSquared = XMVectorGetX(XMVector3LengthSq(vectorValue));
		if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-8f)
			return fallback;

		DirectX::XMFLOAT3 result{};
		XMStoreFloat3(&result, XMVector3Normalize(vectorValue));
		return result;
	}

	DirectX::XMFLOAT3 TransformSkeletonPositionToWorld(
		const DirectX::XMFLOAT3& skeletonPosition,
		const DirectX::XMFLOAT4X4* ownerWorldMatrix)
	{
		if (ownerWorldMatrix == nullptr)
			return skeletonPosition;

		DirectX::XMFLOAT3 worldPosition{};
		XMStoreFloat3(
			&worldPosition,
			XMVector3TransformCoord(XMLoadFloat3(&skeletonPosition), XMLoadFloat4x4(ownerWorldMatrix)));
		return worldPosition;
	}

	bool IntersectRayWithPlane(
		const DirectX::XMVECTOR& rayOrigin,
		const DirectX::XMVECTOR& rayDirection,
		const DirectX::XMFLOAT3& planePoint,
		const DirectX::XMFLOAT3& planeNormal,
		DirectX::XMFLOAT3* outHitPoint)
	{
		if (outHitPoint == nullptr)
			return false;

		const XMVECTOR planePointV = XMLoadFloat3(&planePoint);
		const XMVECTOR planeNormalV = XMVector3Normalize(XMLoadFloat3(&planeNormal));
		const float denominator = XMVectorGetX(XMVector3Dot(rayDirection, planeNormalV));
		if (!std::isfinite(denominator) || std::abs(denominator) <= 1.0e-6f)
			return false;

		const XMVECTOR originToPlane = planePointV - rayOrigin;
		const float distance = XMVectorGetX(XMVector3Dot(originToPlane, planeNormalV)) / denominator;
		if (!std::isfinite(distance) || distance < 0.0f)
			return false;

		const XMVECTOR hitPoint = rayOrigin + rayDirection * distance;
		XMStoreFloat3(outHitPoint, hitPoint);
		return true;
	}

	void AppendSkeletonOverlayBox(
		SkeletonOverlayRenderData* renderData,
		const DirectX::XMFLOAT3& center,
		const DirectX::XMFLOAT3& axisX,
		const DirectX::XMFLOAT3& axisY,
		const DirectX::XMFLOAT3& axisZ,
		float halfExtentX,
		float halfExtentY,
		float halfExtentZ,
		const DirectX::XMFLOAT4& color)
	{
		if (renderData == nullptr)
			return;

		const XMVECTOR centerV = XMLoadFloat3(&center);
		const XMVECTOR axisXV = XMLoadFloat3(&axisX) * halfExtentX;
		const XMVECTOR axisYV = XMLoadFloat3(&axisY) * halfExtentY;
		const XMVECTOR axisZV = XMLoadFloat3(&axisZ) * halfExtentZ;

		const XMVECTOR corners[8] =
		{
			centerV - axisXV - axisYV - axisZV,
			centerV + axisXV - axisYV - axisZV,
			centerV + axisXV + axisYV - axisZV,
			centerV - axisXV + axisYV - axisZV,
			centerV - axisXV - axisYV + axisZV,
			centerV + axisXV - axisYV + axisZV,
			centerV + axisXV + axisYV + axisZV,
			centerV - axisXV + axisYV + axisZV
		};

		const std::uint32_t baseIndex = static_cast<std::uint32_t>(renderData->Vertices.size());
		for (const XMVECTOR& corner : corners)
		{
			DirectX::XMFLOAT3 position{};
			XMStoreFloat3(&position, corner);
			renderData->Vertices.push_back({ position, color });
		}

		static constexpr std::uint32_t kBoxIndices[] =
		{
			0, 1, 2, 0, 2, 3,
			4, 6, 5, 4, 7, 6,
			0, 4, 5, 0, 5, 1,
			1, 5, 6, 1, 6, 2,
			2, 6, 7, 2, 7, 3,
			3, 7, 4, 3, 4, 0
		};

		for (std::uint32_t index : kBoxIndices)
			renderData->Indices.push_back(baseIndex + index);
	}

	void AppendSkeletonOverlayCube(
		SkeletonOverlayRenderData* renderData,
		const DirectX::XMFLOAT3& center,
		float halfExtent,
		const DirectX::XMFLOAT4& color)
	{
		AppendSkeletonOverlayBox(
			renderData,
			center,
			DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f),
			DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f),
			DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f),
			halfExtent,
			halfExtent,
			halfExtent,
			color);
	}

	void BuildSkeletonOverlayBasis(
		const DirectX::XMVECTOR& direction,
		DirectX::XMVECTOR* outAxisX,
		DirectX::XMVECTOR* outAxisY,
		DirectX::XMVECTOR* outAxisZ)
	{
		if (outAxisX == nullptr || outAxisY == nullptr || outAxisZ == nullptr)
			return;

		XMVECTOR axisZ = XMVector3Normalize(direction);
		XMVECTOR referenceUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		if (std::abs(XMVectorGetX(XMVector3Dot(axisZ, referenceUp))) > 0.95f)
			referenceUp = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);

		const XMVECTOR axisX = XMVector3Normalize(XMVector3Cross(referenceUp, axisZ));
		const XMVECTOR axisY = XMVector3Normalize(XMVector3Cross(axisZ, axisX));
		*outAxisX = axisX;
		*outAxisY = axisY;
		*outAxisZ = axisZ;
	}

	void AppendSkeletonOverlaySphere(
		SkeletonOverlayRenderData* renderData,
		const DirectX::XMFLOAT3& center,
		float radius,
		const DirectX::XMFLOAT4& color,
		std::uint32_t sliceCount = 6,
		std::uint32_t stackCount = 4)
	{
		if (renderData == nullptr || radius <= 1.0e-5f || sliceCount < 3 || stackCount < 2)
			return;

		const std::uint32_t baseIndex = static_cast<std::uint32_t>(renderData->Vertices.size());
		const XMVECTOR centerV = XMLoadFloat3(&center);
		const float pi = DirectX::XM_PI;

		for (std::uint32_t stack = 0; stack <= stackCount; ++stack)
		{
			const float v = static_cast<float>(stack) / static_cast<float>(stackCount);
			const float phi = v * pi;
			const float y = std::cos(phi);
			const float ringRadius = std::sin(phi);

			for (std::uint32_t slice = 0; slice <= sliceCount; ++slice)
			{
				const float u = static_cast<float>(slice) / static_cast<float>(sliceCount);
				const float theta = u * DirectX::XM_2PI;
				const float x = ringRadius * std::cos(theta);
				const float z = ringRadius * std::sin(theta);

				const XMVECTOR localPos = XMVectorSet(x * radius, y * radius, z * radius, 0.0f);
				DirectX::XMFLOAT3 position{};
				XMStoreFloat3(&position, centerV + localPos);
				renderData->Vertices.push_back({ position, color });
			}
		}

		const std::uint32_t ringVertexCount = sliceCount + 1;
		for (std::uint32_t stack = 0; stack < stackCount; ++stack)
		{
			for (std::uint32_t slice = 0; slice < sliceCount; ++slice)
			{
				const std::uint32_t i0 = baseIndex + stack * ringVertexCount + slice;
				const std::uint32_t i1 = i0 + 1;
				const std::uint32_t i2 = i0 + ringVertexCount;
				const std::uint32_t i3 = i2 + 1;

				renderData->Indices.push_back(i0);
				renderData->Indices.push_back(i2);
				renderData->Indices.push_back(i1);

				renderData->Indices.push_back(i1);
				renderData->Indices.push_back(i2);
				renderData->Indices.push_back(i3);
			}
		}
	}

	void AppendSkeletonOverlayCone(
		SkeletonOverlayRenderData* renderData,
		const DirectX::XMFLOAT3& baseCenter,
		const DirectX::XMFLOAT3& tip,
		float baseRadius,
		const DirectX::XMFLOAT4& color,
		std::uint32_t sideCount = 10)
	{
		if (renderData == nullptr || baseRadius <= 1.0e-5f || sideCount < 3)
			return;

		const XMVECTOR baseCenterV = XMLoadFloat3(&baseCenter);
		const XMVECTOR tipV = XMLoadFloat3(&tip);
		const XMVECTOR direction = tipV - baseCenterV;
		const float directionLengthSq = XMVectorGetX(XMVector3LengthSq(direction));
		if (!std::isfinite(directionLengthSq) || directionLengthSq <= 1.0e-8f)
			return;

		XMVECTOR axisX = XMVectorZero();
		XMVECTOR axisY = XMVectorZero();
		XMVECTOR axisZ = XMVectorZero();
		BuildSkeletonOverlayBasis(direction, &axisX, &axisY, &axisZ);

		const std::uint32_t baseIndex = static_cast<std::uint32_t>(renderData->Vertices.size());

		DirectX::XMFLOAT3 tipPos{};
		XMStoreFloat3(&tipPos, tipV);
		renderData->Vertices.push_back({ tipPos, color });

		DirectX::XMFLOAT3 baseCenterPos{};
		XMStoreFloat3(&baseCenterPos, baseCenterV);
		renderData->Vertices.push_back({ baseCenterPos, color });

		for (std::uint32_t side = 0; side < sideCount; ++side)
		{
			const float angle = (static_cast<float>(side) / static_cast<float>(sideCount)) * DirectX::XM_2PI;
			const XMVECTOR radial = axisX * (std::cos(angle) * baseRadius) + axisY * (std::sin(angle) * baseRadius);
			DirectX::XMFLOAT3 ringPos{};
			XMStoreFloat3(&ringPos, baseCenterV + radial);
			renderData->Vertices.push_back({ ringPos, color });
		}

		for (std::uint32_t side = 0; side < sideCount; ++side)
		{
			const std::uint32_t curr = baseIndex + 2 + side;
			const std::uint32_t next = baseIndex + 2 + ((side + 1) % sideCount);

			renderData->Indices.push_back(baseIndex + 0);
			renderData->Indices.push_back(curr);
			renderData->Indices.push_back(next);

			renderData->Indices.push_back(baseIndex + 1);
			renderData->Indices.push_back(next);
			renderData->Indices.push_back(curr);
		}
	}

	void AppendSkeletonOverlayBoneBody(
		SkeletonOverlayRenderData* renderData,
		const DirectX::XMFLOAT3& start,
		const DirectX::XMFLOAT3& end,
		float halfThickness,
		const DirectX::XMFLOAT4& color)
	{
		if (renderData == nullptr)
			return;

		const XMVECTOR startV = XMLoadFloat3(&start);
		const XMVECTOR endV = XMLoadFloat3(&end);
		const XMVECTOR boneVector = endV - startV;
		const float boneLength = XMVectorGetX(XMVector3Length(boneVector));
		if (!std::isfinite(boneLength) || boneLength <= 1.0e-4f)
			return;

		XMVECTOR axisX = XMVectorZero();
		XMVECTOR axisY = XMVectorZero();
		XMVECTOR axisZ = XMVectorZero();
		BuildSkeletonOverlayBasis(boneVector, &axisX, &axisY, &axisZ);

		const float coneLength = std::max(halfThickness * 5.0f, boneLength * 0.28f);
		const float shaftLength = std::max(0.0f, boneLength - coneLength);
		const XMVECTOR shaftCenterV = startV + axisZ * (shaftLength * 0.5f);
		const XMVECTOR coneBaseCenterV = startV + axisZ * shaftLength;

		DirectX::XMFLOAT3 shaftCenter{};
		DirectX::XMFLOAT3 coneBaseCenter{};
		DirectX::XMFLOAT3 axisXF{};
		DirectX::XMFLOAT3 axisYF{};
		DirectX::XMFLOAT3 axisZF{};
		XMStoreFloat3(&shaftCenter, shaftCenterV);
		XMStoreFloat3(&coneBaseCenter, coneBaseCenterV);
		XMStoreFloat3(&axisXF, axisX);
		XMStoreFloat3(&axisYF, axisY);
		XMStoreFloat3(&axisZF, axisZ);

		if (shaftLength > 1.0e-4f)
		{
			AppendSkeletonOverlayBox(
				renderData,
				shaftCenter,
				axisXF,
				axisYF,
				axisZF,
				halfThickness * 0.55f,
				halfThickness * 0.55f,
				shaftLength * 0.5f,
				color);
		}

		AppendSkeletonOverlayCone(
			renderData,
			coneBaseCenter,
			end,
			halfThickness * 1.6f,
			color);
	}

	void BuildWorldRayFromScreenPoint(
		HWND hwnd,
		D3DWindow* dx,
		const POINT& point,
		DirectX::XMVECTOR* outRayWorldPos,
		DirectX::XMVECTOR* outRayWorldDir)
	{
		if (outRayWorldPos == nullptr || outRayWorldDir == nullptr)
			return;

		*outRayWorldPos = XMVectorZero();
		*outRayWorldDir = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

		if (dx == nullptr || hwnd == nullptr)
			return;

		const float contextWidth = static_cast<float>(EngineHelpers::GetContextWidth(hwnd));
		const float contextHeight = static_cast<float>(EngineHelpers::GetContextHeight(hwnd));
		if (contextWidth <= 0.0f || contextHeight <= 0.0f)
			return;

		const XMMATRIX projection = dx->GetProj();
		const XMMATRIX view = dx->GetView();
		const float projX = XMVectorGetX(projection.r[0]);
		const float projY = XMVectorGetY(projection.r[1]);
		if (std::abs(projX) < 1.0e-6f || std::abs(projY) < 1.0e-6f)
			return;

		const float rayViewX = (((2.0f * static_cast<float>(point.x)) / contextWidth) - 1.0f) / projX;
		const float rayViewY = (-((2.0f * static_cast<float>(point.y)) / contextHeight) + 1.0f) / projY;

		const XMVECTOR pickRayInViewSpaceDir = XMVectorSet(rayViewX, rayViewY, 1.0f, 0.0f);
		const XMMATRIX inverseView = XMMatrixInverse(nullptr, view);
		*outRayWorldPos = XMVector3TransformCoord(XMVectorZero(), inverseView);
		*outRayWorldDir = XMVector3Normalize(XMVector3TransformNormal(pickRayInViewSpaceDir, inverseView));
	}

	bool IntersectRayWithPlaneEquation(
		const DirectX::XMVECTOR& rayOrigin,
		const DirectX::XMVECTOR& rayDirection,
		const DirectX::XMFLOAT4& plane,
		DirectX::XMFLOAT3* outHitPoint)
	{
		if (outHitPoint == nullptr)
			return false;

		const XMVECTOR normal = XMVectorSet(plane.x, plane.y, plane.z, 0.0f);
		const float denominator = XMVectorGetX(XMVector3Dot(rayDirection, normal));
		if (!std::isfinite(denominator) || std::abs(denominator) < 1e-6f)
			return false;

		const float numerator = -(XMVectorGetX(XMVector3Dot(rayOrigin, normal)) + plane.w);
		const float t = numerator / denominator;
		if (!std::isfinite(t) || t < 0.0f)
			return false;

		XMStoreFloat3(outHitPoint, rayOrigin + rayDirection * t);
		return true;
	}

	bool TryIntersectRayWithSphere(
		const DirectX::XMVECTOR& rayOrigin,
		const DirectX::XMVECTOR& rayDirection,
		const DirectX::XMFLOAT3& sphereCenter,
		float sphereRadius,
		float* outDistance)
	{
		if (outDistance == nullptr || sphereRadius <= 0.0f)
			return false;

		const XMVECTOR center = XMLoadFloat3(&sphereCenter);
		const XMVECTOR offset = rayOrigin - center;
		const float b = XMVectorGetX(XMVector3Dot(offset, rayDirection));
		const float c = XMVectorGetX(XMVector3Dot(offset, offset)) - (sphereRadius * sphereRadius);
		const float discriminant = b * b - c;
		if (!std::isfinite(discriminant) || discriminant < 0.0f)
			return false;

		const float sqrtDiscriminant = std::sqrt(discriminant);
		const float nearDistance = -b - sqrtDiscriminant;
		const float farDistance = -b + sqrtDiscriminant;
		float hitDistance = nearDistance;
		if (hitDistance < 0.0f)
			hitDistance = farDistance;
		if (!std::isfinite(hitDistance) || hitDistance < 0.0f)
			return false;

		*outDistance = hitDistance;
		return true;
	}

	float ComputeSignedAngleAroundAxis(
		const DirectX::XMVECTOR& fromVector,
		const DirectX::XMVECTOR& toVector,
		const DirectX::XMVECTOR& axis)
	{
		const XMVECTOR fromN = XMVector3Normalize(fromVector);
		const XMVECTOR toN = XMVector3Normalize(toVector);
		const float dotValue = MathHelps::ClampUnit(XMVectorGetX(XMVector3Dot(fromN, toN)));
		const float angle = std::acos(dotValue);
		const float sign = XMVectorGetX(XMVector3Dot(XMVector3Cross(fromN, toN), axis)) >= 0.0f ? 1.0f : -1.0f;
		return angle * sign;
	}

	float ComputeRaySegmentDistance(
		const DirectX::XMVECTOR& rayOriginWS,
		const DirectX::XMVECTOR& rayDirWS,
		const DirectX::XMVECTOR& segmentStartWS,
		const DirectX::XMVECTOR& segmentEndWS,
		float* outRayT,
		float* outSegmentT)
	{
		const XMVECTOR segmentDir = segmentEndWS - segmentStartWS;
		const float segmentLength = XMVectorGetX(XMVector3Length(segmentDir));
		if (segmentLength < 1e-6f)
			return FLT_MAX;

		const XMVECTOR segmentDirNormalized = segmentDir / segmentLength;
		const XMVECTOR r = rayOriginWS - segmentStartWS;

		const float a = XMVectorGetX(XMVector3Dot(rayDirWS, rayDirWS));
		const float e = XMVectorGetX(XMVector3Dot(segmentDirNormalized, segmentDirNormalized));
		const float b = XMVectorGetX(XMVector3Dot(rayDirWS, segmentDirNormalized));
		const float c = XMVectorGetX(XMVector3Dot(rayDirWS, r));
		const float f = XMVectorGetX(XMVector3Dot(segmentDirNormalized, r));
		const float denom = a * e - b * b;

		float s = 0.0f;
		float t = 0.0f;
		if (std::abs(denom) > 1e-6f)
			s = (b * f - c * e) / denom;
		t = (b * s + f) / e;

		if (s < 0.0f)
			s = 0.0f;
		t = (std::max)(0.0f, (std::min)(segmentLength, t));

		const XMVECTOR closestPointRay = rayOriginWS + rayDirWS * s;
		const XMVECTOR closestPointSegment = segmentStartWS + segmentDirNormalized * t;
		if (outRayT != nullptr)
			*outRayT = s;
		if (outSegmentT != nullptr)
			*outSegmentT = segmentLength > 1e-6f ? (t / segmentLength) : 0.0f;
		return XMVectorGetX(XMVector3Length(closestPointRay - closestPointSegment));
	}
}

using namespace SkeletonEditorToolDetail;

bool SkeletonEditorTool::WantsMouseCapture(const MouseEvent& me, MouseClass* mouse) const
{
	if (!m_enabled || mouse == nullptr)
		return false;

	const MouseEvent::EventType mouseEventType = me.GetType();
	return mouseEventType == MouseEvent::EventType::LPress ||
		mouseEventType == MouseEvent::EventType::LRelease ||
		mouseEventType == MouseEvent::EventType::Move;
}

void SkeletonEditorTool::LoadFromTopology(
	const Witchcraft::Animation::SkeletonTopology& topology,
	const std::vector<DirectX::XMFLOAT4X4>* globalPose,
	const DirectX::XMFLOAT4X4* ownerWorldMatrix)
{
	CancelActiveDrag();
	m_joints.clear();
	m_joints.reserve(topology.Bones.size());

	const bool hasGlobalPose =
		globalPose != nullptr &&
		globalPose->size() == topology.Bones.size();

	for (std::uint32_t boneIndex = 0; boneIndex < static_cast<std::uint32_t>(topology.Bones.size()); ++boneIndex)
	{
		const auto& bone = topology.Bones[boneIndex];
		const DirectX::XMFLOAT4X4& worldMatrix =
			hasGlobalPose
			? (*globalPose)[boneIndex]
			: bone.BindGlobalMatrix;

		SkeletonJoint joint;
		joint.Name = bone.Name;
		joint.ParentIndex = bone.ParentIndex;
		joint.Position = TransformSkeletonPositionToWorld(
			DirectX::XMFLOAT3(worldMatrix._41, worldMatrix._42, worldMatrix._43),
			ownerWorldMatrix);
		joint.Rotation = bone.BindLocalPose.Rotation;
		joint.Scale = bone.BindLocalPose.Scale;
		m_joints.push_back(joint);
	}

	if (!IsJointIndexValid(m_selectedJointIndex))
		m_selectedJointIndex = -1;
	if (m_brushWeightModeEnabled && !IsJointIndexValid(m_selectedJointIndex) && !m_joints.empty())
		m_selectedJointIndex = 0;
	m_hoveredJointIndex = -1;
	m_hasPendingChanges = false;
}

bool SkeletonEditorTool::UpdateJointPositionsFromGlobalPose(
	const std::vector<DirectX::XMFLOAT4X4>& globalPose,
	const DirectX::XMFLOAT4X4& ownerWorldMatrix)
{
	if (m_hasPendingChanges || m_isDragging || globalPose.size() != m_joints.size())
		return false;

	for (size_t jointIndex = 0; jointIndex < m_joints.size(); ++jointIndex)
	{
		const DirectX::XMFLOAT4X4& globalMatrix = globalPose[jointIndex];
		m_joints[jointIndex].Position = TransformSkeletonPositionToWorld(
			DirectX::XMFLOAT3(globalMatrix._41, globalMatrix._42, globalMatrix._43),
			&ownerWorldMatrix);
	}

	return true;
}

bool SkeletonEditorTool::UpdateJointPositionsFromBindPose(
	const Witchcraft::Animation::SkeletonTopology& topology,
	const DirectX::XMFLOAT4X4& ownerWorldMatrix)
{
	if (m_hasPendingChanges || m_isDragging || topology.Bones.size() != m_joints.size())
		return false;

	for (size_t jointIndex = 0; jointIndex < m_joints.size(); ++jointIndex)
	{
		const DirectX::XMFLOAT4X4& bindGlobalMatrix = topology.Bones[jointIndex].BindGlobalMatrix;
		m_joints[jointIndex].Position = TransformSkeletonPositionToWorld(
			DirectX::XMFLOAT3(bindGlobalMatrix._41, bindGlobalMatrix._42, bindGlobalMatrix._43),
			&ownerWorldMatrix);
	}

	return true;
}

void SkeletonEditorTool::SelectJoint(int jointIndex)
{
	if (m_brushWeightModeEnabled && IsJointIndexValid(m_selectedJointIndex))
	{
		m_hoveredJointIndex = -1;
		return;
	}

	m_selectedJointIndex = IsJointIndexValid(jointIndex) ? jointIndex : -1;
	m_hoveredJointIndex = -1;
}

bool SkeletonEditorTool::HasPendingChanges() const
{
	return m_hasPendingChanges;
}

void SkeletonEditorTool::ClearPendingChanges()
{
	m_hasPendingChanges = false;
}

bool SkeletonEditorTool::ConsumeSaveToModelRequest()
{
	const bool requested = m_saveToModelRequested;
	m_saveToModelRequested = false;
	return requested;
}

const std::vector<SkeletonJoint>& SkeletonEditorTool::GetJoints() const
{
	return m_joints;
}

int SkeletonEditorTool::GetSelectedJointIndex() const
{
	return m_selectedJointIndex;
}

SkeletonInteractionMode SkeletonEditorTool::GetInteractionMode() const
{
	return m_interactionMode;
}

bool SkeletonEditorTool::HasActiveJointSelection() const
{
	return m_enabled && IsJointIndexValid(m_selectedJointIndex);
}

bool SkeletonEditorTool::IsBrushDragging() const
{
	return m_enabled && m_isDragging && m_dragMode == SkeletonDragMode::BrushWeight;
}

void SkeletonEditorTool::SetBrushWeightModeEnabled(bool enabled)
{
	m_brushWeightModeEnabled = enabled;
	if (m_brushWeightModeEnabled)
	{
		m_interactionMode = SkeletonInteractionMode::BrushWeight;
		m_gizmoHoverHandle = GizmoHandle::None;
		m_gizmoActiveHandle = GizmoHandle::None;
		if (m_gizmoDragging)
			EndGizmoDrag();
		m_isDragging = false;
		m_dragMode = SkeletonDragMode::None;
		m_dragTargetJointIndex = IsJointIndexValid(m_selectedJointIndex) ? m_selectedJointIndex : -1;
	}
}

bool SkeletonEditorTool::IsBrushWeightModeEnabled() const
{
	return m_brushWeightModeEnabled;
}

void SkeletonEditorTool::SetBrushRadius(float radius)
{
	m_brushRadius = (std::max)(0.01f, radius);
}

float SkeletonEditorTool::GetBrushRadius() const
{
	return m_brushRadius;
}

void SkeletonEditorTool::SetBrushStrength(float strength)
{
	m_brushStrength = std::clamp(strength, 0.0f, 1.0f);
}

float SkeletonEditorTool::GetBrushStrength() const
{
	return m_brushStrength;
}

void SkeletonEditorTool::SetBrushFalloff(float falloff)
{
	m_brushFalloff = (std::max)(0.1f, falloff);
}

float SkeletonEditorTool::GetBrushFalloff() const
{
	return m_brushFalloff;
}

void SkeletonEditorTool::SetBrushIncludeChildren(bool includeChildren)
{
	m_brushIncludeChildren = includeChildren;
}

bool SkeletonEditorTool::GetBrushIncludeChildren() const
{
	return m_brushIncludeChildren;
}

// 以鼠标射线为刷笔：对每个模型顶点计算到射线的垂距，
// 在半径范围内的顶点增加选中骨骼的蒙皮权重。
bool SkeletonEditorTool::ApplyBrushWeight(const DirectX::XMFLOAT3& brushRayOriginWS, const DirectX::XMFLOAT3& brushRayDirWS, D3DWindow* dx)
{
	if (!m_enabled || dx == nullptr || m_editor == nullptr || !IsJointIndexValid(m_selectedJointIndex))
		return false;

	WModelFileData* modelData = m_editor->GetBrushWeightModelDataMutable();
	if (modelData == nullptr || modelData->Meshes.empty())
		return false;

	const DirectX::XMVECTOR rayOrigin = DirectX::XMLoadFloat3(&brushRayOriginWS);
	const DirectX::XMVECTOR rayDirection = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&brushRayDirWS));
	const float radius = (std::max)(m_brushRadius, 0.0001f);
	const float radiusSq = radius * radius;
	const float invRadius = 1.0f / radius;
	const float falloff = (std::max)(0.001f, m_brushFalloff);
	const float strength = std::clamp(m_brushStrength, 0.0f, 1.0f);
	if (strength <= 0.00001f)
		return false;

	bool anyChanged = false;
	std::vector<std::tuple<const void*, UINT, Witchcraft::Animation::VertexBoneInfluence4>> gpuChanges;
	gpuChanges.reserve(256);
	WitchcraECS* ecs = m_editor->GetEngine() != nullptr ? m_editor->GetEngine()->GetECS() : nullptr;
	SceneEntityBase* targetEntity = m_editor->GetBrushWeightVisualizationTargetEntity();
	DirectX::XMMATRIX targetWorld = DirectX::XMMatrixIdentity();
	DirectX::XMFLOAT3 targetScale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	if (ecs != nullptr && targetEntity != nullptr)
	{
		if (TransformComponent* transformComponent = ecs->GetComponent<TransformComponent>(targetEntity))
		{
			const Transform targetTransform = transformComponent->GetTransform();
			targetScale = targetTransform.scale;
			targetWorld =
				DirectX::XMMatrixScaling(targetTransform.scale.x, targetTransform.scale.y, targetTransform.scale.z) *
				DirectX::XMMatrixRotationRollPitchYaw(
					DirectX::XMConvertToRadians(targetTransform.rotation.x),
					DirectX::XMConvertToRadians(targetTransform.rotation.y),
					DirectX::XMConvertToRadians(targetTransform.rotation.z)) *
				DirectX::XMMatrixTranslation(targetTransform.position.x, targetTransform.position.y, targetTransform.position.z);
		}
	}

	const DirectX::XMMATRIX inverseTargetWorld = DirectX::XMMatrixInverse(nullptr, targetWorld);
	const DirectX::XMVECTOR localRayOrigin = DirectX::XMVector3TransformCoord(rayOrigin, inverseTargetWorld);
	DirectX::XMVECTOR localRayDirection = DirectX::XMVector3TransformNormal(rayDirection, inverseTargetWorld);
	const float localRayDirLenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(localRayDirection));
	if (!std::isfinite(localRayDirLenSq) || localRayDirLenSq <= 1.0e-8f)
		return false;
	localRayDirection = DirectX::XMVector3Normalize(localRayDirection);

	const float absScaleX = std::abs(targetScale.x);
	const float absScaleY = std::abs(targetScale.y);
	const float absScaleZ = std::abs(targetScale.z);
	const float minScale = (std::max)(0.0001f, (std::min)({ absScaleX, absScaleY, absScaleZ }));
	const float localCoarseRadius = radius / minScale;
	const float localCoarseRadiusSq = localCoarseRadius * localCoarseRadius;

	// 局部空间包围球缓存：先快速排除与刷笔射线明显无关的 mesh。
	static std::unordered_map<const WModelMeshData*, std::pair<DirectX::XMFLOAT3, float>> sMeshBoundsCache;

	for (WModelMeshData& meshData : modelData->Meshes)
	{
		if (meshData.Vertices.empty())
			continue;

		auto cacheIt = sMeshBoundsCache.find(&meshData);
		if (cacheIt == sMeshBoundsCache.end())
		{
			DirectX::XMFLOAT3 minPt(FLT_MAX, FLT_MAX, FLT_MAX), maxPt(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (const Vertex& v : meshData.Vertices)
			{
				if (v.Pos.x < minPt.x) minPt.x = v.Pos.x;
				if (v.Pos.y < minPt.y) minPt.y = v.Pos.y;
				if (v.Pos.z < minPt.z) minPt.z = v.Pos.z;
				if (v.Pos.x > maxPt.x) maxPt.x = v.Pos.x;
				if (v.Pos.y > maxPt.y) maxPt.y = v.Pos.y;
				if (v.Pos.z > maxPt.z) maxPt.z = v.Pos.z;
			}
			DirectX::XMFLOAT3 center((minPt.x + maxPt.x) * 0.5f, (minPt.y + maxPt.y) * 0.5f, (minPt.z + maxPt.z) * 0.5f);
			float meshRadius = std::sqrt(
				(maxPt.x - minPt.x) * (maxPt.x - minPt.x) +
				(maxPt.y - minPt.y) * (maxPt.y - minPt.y) +
				(maxPt.z - minPt.z) * (maxPt.z - minPt.z)) * 0.5f;
			cacheIt = sMeshBoundsCache.emplace(&meshData, std::make_pair(center, meshRadius)).first;
		}

		{
			const DirectX::XMVECTOR meshCenterLS = DirectX::XMLoadFloat3(&cacheIt->second.first);
			const float meshCullRadius = cacheIt->second.second + localCoarseRadius;
			const DirectX::XMVECTOR toMeshCenter = meshCenterLS - localRayOrigin;
			const float projDist = DirectX::XMVectorGetX(DirectX::XMVector3Dot(toMeshCenter, localRayDirection));
			if (projDist < -meshCullRadius)
				continue;

			const DirectX::XMVECTOR closestPointLS = localRayOrigin + localRayDirection * projDist;
			const float distToRaySq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(meshCenterLS - closestPointLS));
			if (!std::isfinite(distToRaySq) || distToRaySq > meshCullRadius * meshCullRadius)
				continue;
		}

		if (meshData.Skinning.size() != meshData.Vertices.size())
		{
			const size_t oldSize = meshData.Skinning.size();
			meshData.Skinning.resize(meshData.Vertices.size());
			for (size_t k = oldSize; k < meshData.Skinning.size(); ++k)
			meshData.Skinning[k].BoneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
		}

		for (size_t vertexIndex = 0; vertexIndex < meshData.Vertices.size(); ++vertexIndex)
		{
			const Vertex& vertex = meshData.Vertices[vertexIndex];
			const DirectX::XMVECTOR localPosition = DirectX::XMLoadFloat3(&vertex.Pos);

			// 先在局部空间做便宜的粗筛，减少后面的世界变换次数。
			const DirectX::XMVECTOR localToVertex = localPosition - localRayOrigin;
			const float localT = DirectX::XMVectorGetX(DirectX::XMVector3Dot(localToVertex, localRayDirection));
			if (localT <= 0.0f)
				continue;

			const DirectX::XMVECTOR localClosestPoint = localRayOrigin + localRayDirection * localT;
			const float localDistanceSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(localPosition - localClosestPoint));
			if (!std::isfinite(localDistanceSq) || localDistanceSq > localCoarseRadiusSq)
				continue;

			const DirectX::XMVECTOR position = DirectX::XMVector3TransformCoord(localPosition, targetWorld);
			const DirectX::XMVECTOR toVertex = position - rayOrigin;
			const float t = DirectX::XMVectorGetX(DirectX::XMVector3Dot(toVertex, rayDirection));
			if (t <= 0.0f)
				continue;

			const DirectX::XMVECTOR projected = rayOrigin + rayDirection * t;
			const DirectX::XMVECTOR delta = position - projected;
			const float distanceSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(delta));
			if (!std::isfinite(distanceSq) || distanceSq > radiusSq)
				continue;

			const float distance = std::sqrt(distanceSq);
			const float attenuation = (std::max)(0.0f, 1.0f - distance * invRadius);
			if (attenuation <= 0.0f)
				continue;

			const float influence =
				(std::abs(falloff - 1.0f) <= 1.0e-4f)
				? (strength * attenuation)
				: (strength * std::pow(attenuation, falloff));
			if (influence <= 0.00001f)
				continue;

		Witchcraft::Animation::VertexBoneInfluence4& skinning = meshData.Skinning[vertexIndex];
			float existingWeightSum = 0.0f;
			for (float weight : skinning.BoneWeights)
				existingWeightSum += weight;
			if (existingWeightSum <= 0.0f)
			{
				skinning.BoneIndices = { static_cast<std::uint32_t>(m_selectedJointIndex), 0u, 0u };
				skinning.BoneWeights = { influence, 0.0f, 0.0f };
				gpuChanges.push_back({ &meshData, static_cast<UINT>(vertexIndex), skinning });
				anyChanged = true;
				continue;
			}

			int targetSlot = -1;
			for (size_t slotIndex = 0; slotIndex < skinning.BoneIndices.size(); ++slotIndex)
			{
				if (skinning.BoneIndices[slotIndex] == static_cast<std::uint32_t>(m_selectedJointIndex))
				{
					targetSlot = static_cast<int>(slotIndex);
					break;
				}
			}

			if (targetSlot < 0)
			{
				targetSlot = 0;
				for (size_t slotIndex = 1; slotIndex < skinning.BoneWeights.size(); ++slotIndex)
				{
					if (skinning.BoneWeights[slotIndex] < skinning.BoneWeights[static_cast<size_t>(targetSlot)])
						targetSlot = static_cast<int>(slotIndex);
				}
				skinning.BoneIndices[static_cast<size_t>(targetSlot)] = static_cast<std::uint32_t>(m_selectedJointIndex);
			}

			skinning.BoneWeights[static_cast<size_t>(targetSlot)] =
				(std::min)(1.0f, skinning.BoneWeights[static_cast<size_t>(targetSlot)] + influence);

			float weightSum = 0.0f;
			for (float weight : skinning.BoneWeights)
				weightSum += weight;
			if (weightSum <= 0.00001f)
				weightSum = 1.0f;

			for (float& weight : skinning.BoneWeights)
				weight /= weightSum;

			auto swapSlotsIfNeeded = [&skinning](size_t lhs, size_t rhs)
			{
				if (skinning.BoneWeights[lhs] < skinning.BoneWeights[rhs])
				{
					std::swap(skinning.BoneWeights[lhs], skinning.BoneWeights[rhs]);
					std::swap(skinning.BoneIndices[lhs], skinning.BoneIndices[rhs]);
				}
			};

			swapSlotsIfNeeded(0u, 1u);
			swapSlotsIfNeeded(0u, 2u);
			swapSlotsIfNeeded(1u, 2u);

			skinning.Normalize();
			gpuChanges.push_back({ &meshData, static_cast<UINT>(vertexIndex), skinning });
			anyChanged = true;
		}
	}

	if (anyChanged)
	{
		m_editor->MarkBrushWeightModelDirty();
		if (!gpuChanges.empty() && m_editor->GetD3DWindow() != nullptr)
			m_editor->GetD3DWindow()->IncrementalUpdateBrushWeightVB(gpuChanges);
	}

	return anyChanged;
}

void SkeletonEditorTool::SetEditor(Editor* editor)
{
	m_editor = editor;
}

Editor* SkeletonEditorTool::GetEditor() const
{
	return m_editor;
}

bool SkeletonEditorTool::IsBrushWeightJointLocked() const
{
	return m_brushWeightModeEnabled;
}

void SkeletonEditorTool::SetBrushWeightJointLockEnabled(bool enabled)
{
	m_brushWeightModeEnabled = enabled;
}

void SkeletonEditorTool::SetGizmoMode(GizmoMode mode)
{
	if (mode == GizmoMode::Translate || mode == GizmoMode::Rotate)
		m_gizmoMode = mode;
	else
		m_gizmoMode = GizmoMode::Translate;

	m_gizmoHoverHandle = GizmoHandle::None;
	if (m_gizmoDragging)
		EndGizmoDrag();
}

GizmoMode SkeletonEditorTool::GetGizmoMode() const
{
	return m_gizmoMode;
}

bool SkeletonEditorTool::IsGizmoVisible() const
{
	return HasActiveJointSelection() &&
		m_interactionMode != SkeletonInteractionMode::AddChild &&
		m_interactionMode != SkeletonInteractionMode::BrushWeight;
}

bool SkeletonEditorTool::BuildAxisDragPlane(
	const DirectX::XMFLOAT3& axisWS,
	const DirectX::XMFLOAT3& originWS,
	const DirectX::XMFLOAT3& cameraPosition,
	DirectX::XMFLOAT4* outPlaneWS) const
{
	if (outPlaneWS == nullptr)
		return false;

	const XMVECTOR origin = XMLoadFloat3(&originWS);
	const XMVECTOR axis = XMVector3Normalize(XMLoadFloat3(&axisWS));
	XMVECTOR viewDir = origin - XMLoadFloat3(&cameraPosition);
	if (XMVectorGetX(XMVector3LengthSq(viewDir)) < 1e-6f)
		viewDir = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	viewDir = XMVector3Normalize(viewDir);

	XMVECTOR planeNormal = XMVector3Cross(axis, XMVector3Cross(viewDir, axis));
	if (XMVectorGetX(XMVector3LengthSq(planeNormal)) < 1e-6f)
	{
		const XMVECTOR fallback = std::abs(axisWS.x) < 0.9f
			? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
			: XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		planeNormal = XMVector3Cross(axis, fallback);
	}

	planeNormal = XMVector3Normalize(planeNormal);
	DirectX::XMFLOAT3 planeNormalStorage{};
	XMStoreFloat3(&planeNormalStorage, planeNormal);
	outPlaneWS->x = planeNormalStorage.x;
	outPlaneWS->y = planeNormalStorage.y;
	outPlaneWS->z = planeNormalStorage.z;
	outPlaneWS->w = -XMVectorGetX(XMVector3Dot(origin, planeNormal));
	return true;
}

bool SkeletonEditorTool::ResolveHandleByRay(
	const DirectX::XMVECTOR& rayWorldPos,
	const DirectX::XMVECTOR& rayWorldDir,
	GizmoHandle* outHandle,
	DirectX::XMFLOAT3* outHitPointWS) const
{
	if (!IsGizmoVisible() || outHandle == nullptr)
		return false;

	const XMFLOAT3 originWS = m_joints[static_cast<size_t>(m_selectedJointIndex)].Position;
	const float drawScale = 0.6f;
	constexpr float axisLengthFactor = 1.10f;
	constexpr float axisPickRadius = 0.12f;

	GizmoHandle bestHandle = GizmoHandle::None;
	float bestDistanceAlongRay = FLT_MAX;
	XMFLOAT3 bestHit{};

	const XMVECTOR origin = XMLoadFloat3(&originWS);
	const struct { GizmoHandle Handle; XMFLOAT3 Axis; } handles[] =
	{
		{ GizmoHandle::AxisX, XMFLOAT3(1.0f, 0.0f, 0.0f) },
		{ GizmoHandle::AxisY, XMFLOAT3(0.0f, 1.0f, 0.0f) },
		{ GizmoHandle::AxisZ, XMFLOAT3(0.0f, 0.0f, 1.0f) }
	};

	for (const auto& axisInfo : handles)
	{
		const XMVECTOR axis = XMLoadFloat3(&axisInfo.Axis);
		const XMVECTOR segmentEnd = origin + axis * (axisLengthFactor * drawScale);

		float rayT = 0.0f;
		float segmentT = 0.0f;
		const float distance = ComputeRaySegmentDistance(rayWorldPos, rayWorldDir, origin, segmentEnd, &rayT, &segmentT);
		if (rayT < 0.0f || segmentT < 0.0f || segmentT > 1.0f)
			continue;
		if (distance > axisPickRadius * drawScale || rayT >= bestDistanceAlongRay)
			continue;

		bestDistanceAlongRay = rayT;
		bestHandle = axisInfo.Handle;
		XMStoreFloat3(&bestHit, rayWorldPos + rayWorldDir * rayT);
	}

	*outHandle = bestHandle;
	if (bestHandle != GizmoHandle::None && outHitPointWS != nullptr)
		*outHitPointWS = bestHit;
	return bestHandle != GizmoHandle::None;
}

bool SkeletonEditorTool::TryBeginGizmoDrag(
	Engine* engine,
	D3DWindow* dx,
	HWND hwnd,
	const DirectX::XMVECTOR& rayWorldPos,
	const DirectX::XMVECTOR& rayWorldDir)
{
	if (!IsGizmoVisible() || dx == nullptr || engine == nullptr || hwnd == nullptr)
		return false;
	if (m_interactionMode == SkeletonInteractionMode::AddChild)
		return false;

	GizmoHandle handle = GizmoHandle::None;
	DirectX::XMFLOAT3 hitPointWS{};
	if (!ResolveHandleByRay(rayWorldPos, rayWorldDir, &handle, &hitPointWS))
		return false;

	const DirectX::XMFLOAT3 originWS = m_joints[static_cast<size_t>(m_selectedJointIndex)].Position;
	DirectX::XMFLOAT3 axisWS(1.0f, 0.0f, 0.0f);
	if (handle == GizmoHandle::AxisY)
		axisWS = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
	else if (handle == GizmoHandle::AxisZ)
		axisWS = DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f);

	DirectX::XMFLOAT4 dragPlaneWS{};
	if (m_gizmoMode == GizmoMode::Rotate)
	{
		dragPlaneWS = DirectX::XMFLOAT4(
			axisWS.x,
			axisWS.y,
			axisWS.z,
			-(originWS.x * axisWS.x + originWS.y * axisWS.y + originWS.z * axisWS.z));
	}
	else
	{
		if (!BuildAxisDragPlane(axisWS, originWS, dx->GetPosition3f(), &dragPlaneWS))
			return false;
	}

	m_gizmoDragging = true;
	m_gizmoActiveHandle = handle;
	m_gizmoHoverHandle = GizmoHandle::None;
	m_gizmoDragAxisWS = axisWS;
	m_gizmoDragPlaneWS = dragPlaneWS;
	m_gizmoDragStartHitWS = hitPointWS;
	m_gizmoDragStartTargetPositionWS = originWS;
	m_gizmoDragRotatePivotWS = originWS;
	m_gizmoDragStartTargetRotation = m_joints[static_cast<size_t>(m_selectedJointIndex)].Rotation;
	m_gizmoDragJointIndices.clear();
	m_gizmoDragOriginalPositions.clear();

	if (m_gizmoMode == GizmoMode::Translate)
		GatherSubtreeJointIndices(m_selectedJointIndex, true, &m_gizmoDragJointIndices);
	else
		GatherSubtreeJointIndices(m_selectedJointIndex, false, &m_gizmoDragJointIndices);

	m_gizmoDragOriginalPositions.reserve(m_gizmoDragJointIndices.size());
	for (int jointIndex : m_gizmoDragJointIndices)
		m_gizmoDragOriginalPositions.push_back(m_joints[static_cast<size_t>(jointIndex)].Position);

	return true;
}

bool SkeletonEditorTool::UpdateGizmoDrag(const DirectX::XMVECTOR& rayWorldPos, const DirectX::XMVECTOR& rayWorldDir)
{
	if (!m_gizmoDragging || !IsJointIndexValid(m_selectedJointIndex))
		return false;

	XMFLOAT3 currentHitWS{};
	if (!IntersectRayWithPlaneEquation(rayWorldPos, rayWorldDir, m_gizmoDragPlaneWS, &currentHitWS))
		return false;

	const XMVECTOR axis = XMLoadFloat3(&m_gizmoDragAxisWS);
	const XMVECTOR start = XMLoadFloat3(&m_gizmoDragStartHitWS);
	const XMVECTOR current = XMLoadFloat3(&currentHitWS);

	if (m_gizmoMode == GizmoMode::Translate)
	{
		const float axisDelta = XMVectorGetX(XMVector3Dot(current - start, axis));
		XMFLOAT3 delta{};
		XMStoreFloat3(&delta, axis * axisDelta);

		for (size_t i = 0; i < m_gizmoDragJointIndices.size(); ++i)
		{
			const int jointIndex = m_gizmoDragJointIndices[i];
			const XMFLOAT3& origin = m_gizmoDragOriginalPositions[i];
			m_joints[static_cast<size_t>(jointIndex)].Position = XMFLOAT3(
				origin.x + delta.x,
				origin.y + delta.y,
				origin.z + delta.z);
		}
		m_hasPendingChanges = true;
		return true;
	}

	const XMVECTOR pivot = XMLoadFloat3(&m_gizmoDragRotatePivotWS);
	const XMVECTOR startVector = start - pivot;
	const XMVECTOR currentVector = current - pivot;
	if (XMVectorGetX(XMVector3LengthSq(startVector)) < 1e-6f ||
		XMVectorGetX(XMVector3LengthSq(currentVector)) < 1e-6f)
	{
		return false;
	}

	const float signedAngleRadians = ComputeSignedAngleAroundAxis(startVector, currentVector, axis);
	const XMMATRIX rotation = XMMatrixRotationAxis(axis, signedAngleRadians);
	const XMVECTOR deltaRotationQ = XMQuaternionNormalize(XMQuaternionRotationAxis(axis, signedAngleRadians));
	const XMVECTOR originalTargetRotationQ = XMQuaternionNormalize(XMLoadFloat4(&m_gizmoDragStartTargetRotation));
	XMFLOAT4 targetRotation{};
	XMStoreFloat4(&targetRotation, XMQuaternionNormalize(XMQuaternionMultiply(deltaRotationQ, originalTargetRotationQ)));
	m_joints[static_cast<size_t>(m_selectedJointIndex)].Rotation = targetRotation;

	for (size_t i = 0; i < m_gizmoDragJointIndices.size(); ++i)
	{
		const int jointIndex = m_gizmoDragJointIndices[i];
		if (!IsJointIndexValid(jointIndex))
			continue;
		const XMVECTOR originalPositionV = XMLoadFloat3(&m_gizmoDragOriginalPositions[i]);
		XMFLOAT3 rotatedPosition{};
		XMStoreFloat3(&rotatedPosition, pivot + XMVector3TransformNormal(originalPositionV - pivot, rotation));
		m_joints[static_cast<size_t>(jointIndex)].Position = rotatedPosition;
	}

	m_hasPendingChanges = true;
	return true;
}

void SkeletonEditorTool::UpdateGizmoHover(const DirectX::XMVECTOR& rayWorldPos, const DirectX::XMVECTOR& rayWorldDir)
{
	if (m_gizmoDragging || !IsGizmoVisible())
		return;

	GizmoHandle handle = GizmoHandle::None;
	ResolveHandleByRay(rayWorldPos, rayWorldDir, &handle, nullptr);
	m_gizmoHoverHandle = handle;
}

void SkeletonEditorTool::EndGizmoDrag()
{
	m_gizmoDragging = false;
	m_gizmoActiveHandle = GizmoHandle::None;
	m_gizmoDragJointIndices.clear();
	m_gizmoDragOriginalPositions.clear();
}

GizmoRenderData SkeletonEditorTool::BuildGizmoRenderData(D3DWindow* dx) const
{
	GizmoRenderData result;
	if (!IsGizmoVisible() || dx == nullptr)
		return result;

	result.Visible = true;
	result.Mode = m_gizmoMode;
	result.HoverHandle = m_gizmoDragging ? GizmoHandle::None : m_gizmoHoverHandle;
	result.ActiveHandle = m_gizmoDragging ? m_gizmoActiveHandle : GizmoHandle::None;
	result.OriginWS = m_joints[static_cast<size_t>(m_selectedJointIndex)].Position;
	const DirectX::XMFLOAT3 cameraPosition = dx->GetPosition3f();
	const DirectX::XMVECTOR cameraPos = DirectX::XMLoadFloat3(&cameraPosition);
	const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&result.OriginWS);
	const float distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(origin - cameraPos));
	result.DrawScale = (std::max)(0.26f, distance * 0.10f);
	return result;
}

bool SkeletonEditorTool::HandleMouse(const MouseEvent& me, MouseClass* mouse, Engine* engine, D3DWindow* dx, HWND hwnd)
{
	if (!m_enabled || dx == nullptr || mouse == nullptr)
		return false;

	if (!WantsMouseCapture(me, mouse))
		return false;

	const MouseEvent::EventType mouseEventType = me.GetType();
	if (mouse->IsRightDown() || mouse->IsMiddleDown())
	{
		if (mouseEventType == MouseEvent::EventType::LRelease)
			CancelActiveDrag();
		return false;
	}

	POINT point = { me.GetPosX(), me.GetPosY() };
	DirectX::XMVECTOR rayWorldPos = DirectX::XMVectorZero();
	DirectX::XMVECTOR rayWorldDir = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	BuildWorldRayFromScreenPoint(hwnd, dx, point, &rayWorldPos, &rayWorldDir);

	WitchcraECS* ecs = engine != nullptr ? engine->GetECS() : nullptr;
	constexpr float kJointPickRadius = 0.12f;

	auto pickClosestJoint = [&]() -> int
	{
		float nearestDistance = FLT_MAX;
		int nearestJointIndex = -1;
		for (size_t jointIndex = 0; jointIndex < m_joints.size(); ++jointIndex)
		{
			float hitDistance = 0.0f;
			if (!TryIntersectRayWithSphere(
				rayWorldPos,
				rayWorldDir,
				m_joints[jointIndex].Position,
				kJointPickRadius,
				&hitDistance))
			{
				continue;
			}

			if (hitDistance >= nearestDistance)
				continue;

			nearestDistance = hitDistance;
			nearestJointIndex = static_cast<int>(jointIndex);
		}

		return nearestJointIndex;
	};

	const int hoveredJointIndex = pickClosestJoint();

	auto resolvePlaneAnchor = [&]() -> DirectX::XMFLOAT3
	{
		if (IsJointIndexValid(m_dragParentJointIndex))
			return m_joints[static_cast<size_t>(m_dragParentJointIndex)].Position;
		if (IsJointIndexValid(m_dragTargetJointIndex))
			return m_joints[static_cast<size_t>(m_dragTargetJointIndex)].Position;
		if (IsJointIndexValid(m_selectedJointIndex))
			return m_joints[static_cast<size_t>(m_selectedJointIndex)].Position;

		if (ecs != nullptr && ecs->GetSelectedEntity() != nullptr)
		{
			if (TransformComponent* transformComponent = ecs->GetComponent<TransformComponent>(ecs->GetSelectedEntity()))
				return transformComponent->GetPosition();
		}

		const DirectX::XMFLOAT3 cameraPosition = dx->GetPosition3f();
		DirectX::XMFLOAT3 rayDirection{};
		DirectX::XMStoreFloat3(&rayDirection, DirectX::XMVector3Normalize(rayWorldDir));
		return DirectX::XMFLOAT3(
			cameraPosition.x + rayDirection.x * 3.0f,
			cameraPosition.y + rayDirection.y * 3.0f,
			cameraPosition.z + rayDirection.z * 3.0f);
	};

	auto resolvePlaneNormal = [&](const DirectX::XMFLOAT3& planePoint) -> DirectX::XMFLOAT3
	{
		const DirectX::XMFLOAT3 cameraPosition = dx->GetPosition3f();
		return NormalizeOrFallback(
			DirectX::XMFLOAT3(
				planePoint.x - cameraPosition.x,
				planePoint.y - cameraPosition.y,
				planePoint.z - cameraPosition.z),
			DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));
	};

	if (mouseEventType == MouseEvent::EventType::Move && !m_isDragging)
	{
		if (m_interactionMode != SkeletonInteractionMode::BrushWeight)
			m_hoveredJointIndex = hoveredJointIndex;
		return false;
	}

if (m_interactionMode == SkeletonInteractionMode::BrushWeight)
	{
		if (mouseEventType == MouseEvent::EventType::LPress ||
			(mouseEventType == MouseEvent::EventType::Move && m_isDragging && m_dragMode == SkeletonDragMode::BrushWeight))
		{
			// 限制刷子重采样频率，避免密集的鼠标移动事件每次都全量扫描整个模型。
			constexpr LONG kBrushSampleMinPixelDelta = 2;
			constexpr auto kBrushSampleMinInterval = std::chrono::milliseconds(12);
			const POINT currentBrushSamplePos = { me.GetPosX(), me.GetPosY() };
			const auto currentBrushSampleTime = std::chrono::steady_clock::now();
			const bool isBrushDragMove =
				mouseEventType == MouseEvent::EventType::Move &&
				m_isDragging &&
				m_dragMode == SkeletonDragMode::BrushWeight;
			if (isBrushDragMove && m_hasLastBrushSample)
			{
				const LONG deltaX = currentBrushSamplePos.x - m_lastBrushSampleScreenPos.x;
				const LONG deltaY = currentBrushSamplePos.y - m_lastBrushSampleScreenPos.y;
				const bool movedEnough =
					std::abs(deltaX) >= kBrushSampleMinPixelDelta ||
					std::abs(deltaY) >= kBrushSampleMinPixelDelta;
				const bool waitedEnough =
					(currentBrushSampleTime - m_lastBrushSampleTime) >= kBrushSampleMinInterval;
				if (!movedEnough || !waitedEnough)
					return true;
			}
			m_lastBrushSampleScreenPos = currentBrushSamplePos;
			m_lastBrushSampleTime = currentBrushSampleTime;
			m_hasLastBrushSample = true;

			m_isDragging = true;
			m_dragMode = SkeletonDragMode::BrushWeight;
			m_dragTargetJointIndex = IsJointIndexValid(m_selectedJointIndex) ? m_selectedJointIndex : hoveredJointIndex;
			if (!IsJointIndexValid(m_dragTargetJointIndex))
				return false;

			// 直接用鼠标射线做刷子，不依赖锚点平面。
			// ApplyBrushWeight 在内部算每个顶点到射线的垂距。
			DirectX::XMFLOAT3 rayOriginWS{};
			DirectX::XMStoreFloat3(&rayOriginWS, rayWorldPos);
			DirectX::XMFLOAT3 rayDirWS{};
			DirectX::XMStoreFloat3(&rayDirWS, rayWorldDir);

			if (engine != nullptr)
				ApplyBrushWeight(rayOriginWS, rayDirWS, dx);
			return true;  // 刷笔拖拽中始终消费事件，避免被 ImGui 拦截
		}

		if (mouseEventType == MouseEvent::EventType::LRelease && m_dragMode == SkeletonDragMode::BrushWeight)
		{
			CancelActiveDrag();
			return true;
		}
	}
	if (mouseEventType == MouseEvent::EventType::LPress)
	{
		if (m_interactionMode != SkeletonInteractionMode::BrushWeight && IsJointIndexValid(hoveredJointIndex))
			m_selectedJointIndex = hoveredJointIndex;

		if (m_interactionMode == SkeletonInteractionMode::MoveJoint)
		{
			if (!IsJointIndexValid(hoveredJointIndex))
				return false;

			CancelActiveDrag();
			m_dragTargetJointIndex = m_selectedJointIndex;
			m_dragPlanePointWS = resolvePlaneAnchor();
			m_dragPlaneNormalWS = resolvePlaneNormal(m_dragPlanePointWS);

			DirectX::XMFLOAT3 hitPoint = m_dragPlanePointWS;
			IntersectRayWithPlane(
				rayWorldPos,
				rayWorldDir,
				m_dragPlanePointWS,
				m_dragPlaneNormalWS,
				&hitPoint);

			m_moveDragStartHitWS = hitPoint;
			GatherSubtreeJointIndices(m_dragTargetJointIndex, true, &m_moveDragJointIndices);
			m_moveDragOriginalPositions.clear();
			m_moveDragOriginalPositions.reserve(m_moveDragJointIndices.size());
			for (const int jointIndex : m_moveDragJointIndices)
			{
				if (!IsJointIndexValid(jointIndex))
					continue;
				m_moveDragOriginalPositions.push_back(m_joints[static_cast<size_t>(jointIndex)].Position);
			}

			m_dragMode = SkeletonDragMode::MoveJoint;
			m_isDragging = true;
			return true;
		}

		if (m_interactionMode == SkeletonInteractionMode::RotateJoint)
		{
			if (!IsJointIndexValid(hoveredJointIndex))
				return false;

			CancelActiveDrag();
			m_dragTargetJointIndex = m_selectedJointIndex;
			m_rotatePivotWS = m_joints[static_cast<size_t>(m_dragTargetJointIndex)].Position;
			m_dragPlanePointWS = m_rotatePivotWS;
			m_dragPlaneNormalWS = resolvePlaneNormal(m_dragPlanePointWS);

			DirectX::XMFLOAT3 hitPoint = m_rotatePivotWS;
			if (!IntersectRayWithPlane(
				rayWorldPos,
				rayWorldDir,
				m_dragPlanePointWS,
				m_dragPlaneNormalWS,
				&hitPoint))
			{
				return false;
			}

			m_rotateDragStartHitWS = hitPoint;
			GatherSubtreeJointIndices(m_dragTargetJointIndex, false, &m_rotateDragJointIndices);
			m_rotateDragOriginalPositions.clear();
			m_rotateDragOriginalPositions.reserve(m_rotateDragJointIndices.size());
			for (const int jointIndex : m_rotateDragJointIndices)
			{
				if (!IsJointIndexValid(jointIndex))
					continue;

				m_rotateDragOriginalPositions.push_back(m_joints[static_cast<size_t>(jointIndex)].Position);
			}
			m_rotateDragOriginalTargetRotation = m_joints[static_cast<size_t>(m_dragTargetJointIndex)].Rotation;

			m_dragMode = SkeletonDragMode::RotateJoint;
			m_isDragging = true;
			return true;
		}

		CancelActiveDrag();
		m_dragParentJointIndex = m_selectedJointIndex;
		m_dragPlanePointWS = resolvePlaneAnchor();
		m_dragPlaneNormalWS = resolvePlaneNormal(m_dragPlanePointWS);

		DirectX::XMFLOAT3 hitPoint = m_dragPlanePointWS;
		IntersectRayWithPlane(
			rayWorldPos,
			rayWorldDir,
			m_dragPlanePointWS,
			m_dragPlaneNormalWS,
			&hitPoint);

		if (IsJointIndexValid(m_dragParentJointIndex))
			m_boneStartWS = m_joints[static_cast<size_t>(m_dragParentJointIndex)].Position;
		else
			m_boneStartWS = hitPoint;
		m_boneEndWS = m_boneStartWS;
		m_dragMode = SkeletonDragMode::AddChild;
		m_isDragging = true;
		return true;
	}

	if (!m_isDragging)
		return false;

	if (mouseEventType == MouseEvent::EventType::Move)
	{
		if (!mouse->IsLeftDown())
			return false;

		if (m_dragMode == SkeletonDragMode::MoveJoint)
		{
			DirectX::XMFLOAT3 hitPoint = m_moveDragStartHitWS;
			if (!IntersectRayWithPlane(
				rayWorldPos,
				rayWorldDir,
				m_dragPlanePointWS,
				m_dragPlaneNormalWS,
				&hitPoint))
			{
				return true;
			}

			const DirectX::XMVECTOR dragStartV = DirectX::XMLoadFloat3(&m_moveDragStartHitWS);
			const DirectX::XMVECTOR hitPointV = DirectX::XMLoadFloat3(&hitPoint);
			DirectX::XMFLOAT3 delta{};
			DirectX::XMStoreFloat3(&delta, hitPointV - dragStartV);

			for (size_t jointArrayIndex = 0; jointArrayIndex < m_moveDragJointIndices.size(); ++jointArrayIndex)
			{
				const int jointIndex = m_moveDragJointIndices[jointArrayIndex];
				if (!IsJointIndexValid(jointIndex))
					continue;

				const DirectX::XMFLOAT3& originalPosition = m_moveDragOriginalPositions[jointArrayIndex];
				m_joints[static_cast<size_t>(jointIndex)].Position = DirectX::XMFLOAT3(
					originalPosition.x + delta.x,
					originalPosition.y + delta.y,
					originalPosition.z + delta.z);
			}

			m_hasPendingChanges = true;
			return true;
		}

		if (m_dragMode == SkeletonDragMode::RotateJoint)
		{
			if (!IsJointIndexValid(m_dragTargetJointIndex))
				return true;

			DirectX::XMFLOAT3 hitPoint = m_rotateDragStartHitWS;
			if (!IntersectRayWithPlane(
				rayWorldPos,
				rayWorldDir,
				m_dragPlanePointWS,
				m_dragPlaneNormalWS,
				&hitPoint))
			{
				return true;
			}

			const DirectX::XMVECTOR pivotV = DirectX::XMLoadFloat3(&m_rotatePivotWS);
			const DirectX::XMVECTOR startOffsetV = DirectX::XMLoadFloat3(&m_rotateDragStartHitWS) - pivotV;
			const DirectX::XMVECTOR currentOffsetV = DirectX::XMLoadFloat3(&hitPoint) - pivotV;
			const float startLengthSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(startOffsetV));
			const float currentLengthSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(currentOffsetV));
			if (!std::isfinite(startLengthSq) || !std::isfinite(currentLengthSq) ||
				startLengthSq <= 1.0e-8f || currentLengthSq <= 1.0e-8f)
			{
				return true;
			}

			const DirectX::XMVECTOR startDirectionV = DirectX::XMVector3Normalize(startOffsetV);
			const DirectX::XMVECTOR currentDirectionV = DirectX::XMVector3Normalize(currentOffsetV);
			const DirectX::XMVECTOR planeNormalV = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&m_dragPlaneNormalWS));
			const float crossDot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(
				DirectX::XMVector3Cross(startDirectionV, currentDirectionV),
				planeNormalV));
			const float dot = std::clamp(
				DirectX::XMVectorGetX(DirectX::XMVector3Dot(startDirectionV, currentDirectionV)),
				-1.0f,
				1.0f);
			const float angleRadians = std::atan2(crossDot, dot);
			const DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationAxis(planeNormalV, angleRadians);
			const DirectX::XMVECTOR deltaRotationQ = DirectX::XMQuaternionRotationAxis(planeNormalV, angleRadians);
			const DirectX::XMVECTOR originalTargetRotationQ = DirectX::XMQuaternionNormalize(
				DirectX::XMLoadFloat4(&m_rotateDragOriginalTargetRotation));
			DirectX::XMFLOAT4 targetRotation{};
			DirectX::XMStoreFloat4(
				&targetRotation,
				DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(deltaRotationQ, originalTargetRotationQ)));
			m_joints[static_cast<size_t>(m_dragTargetJointIndex)].Rotation = targetRotation;

			for (size_t jointArrayIndex = 0; jointArrayIndex < m_rotateDragJointIndices.size(); ++jointArrayIndex)
			{
				const int jointIndex = m_rotateDragJointIndices[jointArrayIndex];
				if (!IsJointIndexValid(jointIndex))
					continue;

				const DirectX::XMVECTOR originalPositionV = DirectX::XMLoadFloat3(&m_rotateDragOriginalPositions[jointArrayIndex]);
				DirectX::XMFLOAT3 rotatedPosition{};
				DirectX::XMStoreFloat3(
					&rotatedPosition,
					pivotV + DirectX::XMVector3TransformNormal(originalPositionV - pivotV, rotation));
				m_joints[static_cast<size_t>(jointIndex)].Position = rotatedPosition;
			}

			m_hasPendingChanges = true;
			return true;
		}

		DirectX::XMFLOAT3 hitPoint = m_boneEndWS;
		if (IntersectRayWithPlane(
			rayWorldPos,
			rayWorldDir,
			m_dragPlanePointWS,
			m_dragPlaneNormalWS,
			&hitPoint))
		{
			m_boneEndWS = hitPoint;
		}
		return true;
	}

	if (mouseEventType == MouseEvent::EventType::LRelease)
	{
		if (m_dragMode == SkeletonDragMode::MoveJoint || m_dragMode == SkeletonDragMode::RotateJoint)
		{
			CancelActiveDrag();
			return true;
		}

		DirectX::XMFLOAT3 hitPoint = m_boneEndWS;
		if (IntersectRayWithPlane(
			rayWorldPos,
			rayWorldDir,
			m_dragPlanePointWS,
			m_dragPlaneNormalWS,
			&hitPoint))
		{
			m_boneEndWS = hitPoint;
		}

		const DirectX::XMVECTOR startV = DirectX::XMLoadFloat3(&m_boneStartWS);
		const DirectX::XMVECTOR endV = DirectX::XMLoadFloat3(&m_boneEndWS);
		const float segmentLengthSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(endV - startV));
		if (std::isfinite(segmentLengthSq) && segmentLengthSq > 1.0e-8f)
		{
			if (IsJointIndexValid(m_dragParentJointIndex))
			{
				SkeletonJoint childJoint;
				childJoint.Name = MakeDefaultJointName();
				childJoint.Position = m_boneEndWS;
				childJoint.ParentIndex = m_dragParentJointIndex;
				m_joints.push_back(childJoint);
				m_selectedJointIndex = static_cast<int>(m_joints.size()) - 1;
			}
			else
			{
				SkeletonJoint rootJoint;
				rootJoint.Name = MakeDefaultJointName();
				rootJoint.Position = m_boneStartWS;
				rootJoint.ParentIndex = -1;
				m_joints.push_back(rootJoint);

				SkeletonJoint childJoint;
				childJoint.Name = MakeDefaultJointName();
				childJoint.Position = m_boneEndWS;
				childJoint.ParentIndex = static_cast<int>(m_joints.size()) - 1;
				m_joints.push_back(childJoint);
				m_selectedJointIndex = static_cast<int>(m_joints.size()) - 1;
			}

			m_hasPendingChanges = true;
		}

		CancelActiveDrag();
		return true;
	}

	return false;
}

void SkeletonEditorTool::UpdateOverlay(D3DWindow* dx)
{
	if (dx == nullptr)
		return;

	if (!m_enabled)
	{
		CancelActiveDrag();
		if (m_hasUploadedOverlay)
			dx->ClearSkeletonOverlayRenderData();
		m_hasUploadedOverlay = false;
		dx->SetSkinWeightVisualizationTarget(nullptr);
		return;
	}

	if (m_joints.empty() && !m_isDragging)
	{
		if (m_hasUploadedOverlay)
			dx->ClearSkeletonOverlayRenderData();
		m_hasUploadedOverlay = false;
		dx->SetSkinWeightVisualizationTarget(nullptr);
		return;
	}

	// 生成球体和骨段的 CPU 几何代价远高于比较少量关节状态。
	// 静止 bind pose 不应每帧重新生成并 Map 上传一次。
	auto mix = [](std::uint64_t seed, std::uint64_t value)
	{
		return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
	};
	auto floatBits = [](float value)
	{
		std::uint32_t bits = 0;
		static_assert(sizeof(bits) == sizeof(value));
		std::memcpy(&bits, &value, sizeof(bits));
		return static_cast<std::uint64_t>(bits);
	};
	std::uint64_t overlayStateHash = 0xcbf29ce484222325ull;
	overlayStateHash = mix(overlayStateHash, static_cast<std::uint64_t>(m_selectedJointIndex + 1));
	overlayStateHash = mix(overlayStateHash, static_cast<std::uint64_t>(m_hoveredJointIndex + 1));
	overlayStateHash = mix(overlayStateHash, static_cast<std::uint64_t>(m_isDragging));
	for (const SkeletonJoint& joint : m_joints)
	{
		overlayStateHash = mix(overlayStateHash, floatBits(joint.Position.x));
		overlayStateHash = mix(overlayStateHash, floatBits(joint.Position.y));
		overlayStateHash = mix(overlayStateHash, floatBits(joint.Position.z));
		overlayStateHash = mix(overlayStateHash, static_cast<std::uint64_t>(joint.ParentIndex + 1));
	}
	if (m_isDragging)
	{
		overlayStateHash = mix(overlayStateHash, floatBits(m_boneStartWS.x));
		overlayStateHash = mix(overlayStateHash, floatBits(m_boneStartWS.y));
		overlayStateHash = mix(overlayStateHash, floatBits(m_boneStartWS.z));
		overlayStateHash = mix(overlayStateHash, floatBits(m_boneEndWS.x));
		overlayStateHash = mix(overlayStateHash, floatBits(m_boneEndWS.y));
		overlayStateHash = mix(overlayStateHash, floatBits(m_boneEndWS.z));
	}
	if (m_hasUploadedOverlay && overlayStateHash == m_lastOverlayStateHash)
		return;

	SkeletonOverlayRenderData renderData;
	renderData.Visible = true;
	renderData.XRay = true;
	// 调试覆盖层使用固定上传缓冲；预留足够空间并采用低面数球体，避免频繁扩容及索引截断。
	renderData.Vertices.reserve((m_joints.size() + 2u) * 64u);
	renderData.Indices.reserve((m_joints.size() + 2u) * 320u);

	const DirectX::XMFLOAT4 rootColor(1.0f, 0.85f, 0.25f, 1.0f);
	const DirectX::XMFLOAT4 boneColor(0.20f, 0.90f, 1.00f, 1.0f);
	const DirectX::XMFLOAT4 previewColor(1.0f, 0.55f, 0.20f, 1.0f);
	const DirectX::XMFLOAT4 selectedColor(1.0f, 1.0f, 1.0f, 1.0f);
	const DirectX::XMFLOAT4 hoveredColor(0.65f, 1.0f, 0.75f, 1.0f);
	const float boneHalfThickness = 0.045f;
	const float jointHalfExtent = 0.075f;

	for (size_t jointIndex = 0; jointIndex < m_joints.size(); ++jointIndex)
	{
		const SkeletonJoint& joint = m_joints[jointIndex];
		DirectX::XMFLOAT4 jointColor = joint.ParentIndex < 0 ? rootColor : boneColor;
		if (static_cast<int>(jointIndex) == m_selectedJointIndex)
			jointColor = selectedColor;
		else if (static_cast<int>(jointIndex) == m_hoveredJointIndex)
			jointColor = hoveredColor;

		AppendSkeletonOverlaySphere(&renderData, joint.Position, jointHalfExtent, jointColor);
		if (joint.ParentIndex >= 0 && IsJointIndexValid(joint.ParentIndex))
		{
			const SkeletonJoint& parentJoint = m_joints[static_cast<size_t>(joint.ParentIndex)];
			AppendSkeletonOverlayBoneBody(
				&renderData,
				parentJoint.Position,
				joint.Position,
				boneHalfThickness,
				boneColor);
		}
	}

	if (m_isDragging && m_dragMode == SkeletonDragMode::AddChild)
	{
		AppendSkeletonOverlayBoneBody(
			&renderData,
			m_boneStartWS,
			m_boneEndWS,
			boneHalfThickness,
			previewColor);

		if (!IsJointIndexValid(m_dragParentJointIndex))
			AppendSkeletonOverlaySphere(&renderData, m_boneStartWS, jointHalfExtent, rootColor);
		AppendSkeletonOverlaySphere(&renderData, m_boneEndWS, jointHalfExtent, previewColor);
	}

	dx->SetSkeletonOverlayRenderData(renderData);
	m_lastOverlayStateHash = overlayStateHash;
	m_hasUploadedOverlay = true;
}

void SkeletonEditorTool::SetEnabled(bool enabled)
{
	if (m_enabled == enabled)
		return;

	m_enabled = enabled;
	m_lastOverlayStateHash = 0;
	if (!m_enabled)
	{
		CancelActiveDrag();
		m_hoveredJointIndex = -1;
		m_brushWeightModeEnabled = false;
	}
}

void SkeletonEditorTool::RenderWindow(float imguiDpiScale, bool* open)
{
	if (open == nullptr || !*open)
		return;

	ImGui::SetNextWindowSize(ImVec2(380.0f * imguiDpiScale, 0.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("骨骼编辑器", nullptr, ImGuiWindowFlags_NoDocking))
	{
		const float btnSmallSz = 28.0f * imguiDpiScale;
		const float availWidth = ImGui::GetContentRegionAvail().x;
		const float fieldWidth = 96.0f * imguiDpiScale;

		// ── 工具栏：模式按钮 + 当前模式标签 ──
		{
			const float toolbarBtnCount = 4.0f;
			const float toolbarBtnTotal = toolbarBtnCount * btnSmallSz;
			const float toolbarSpacing = (toolbarBtnCount - 1.0f) * ImGui::GetStyle().ItemSpacing.x;
			const float labelWidth = availWidth - toolbarBtnTotal - toolbarSpacing;

			auto drawModeButton = [&](const char* id, const char* icon, SkeletonInteractionMode mode, const char* tooltip)
			{
				const bool isActive = (m_interactionMode == mode);
				if (isActive)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 0.85f));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
				}

				ImGui::PushID(id);
				if (ImGui::Button(icon, ImVec2(btnSmallSz, btnSmallSz)))
				{
					m_interactionMode = mode;
					if (mode == SkeletonInteractionMode::AddChild)
						SetGizmoMode(GizmoMode::None);
					else if (mode == SkeletonInteractionMode::MoveJoint)
						SetGizmoMode(GizmoMode::Translate);
					else if (mode == SkeletonInteractionMode::RotateJoint)
						SetGizmoMode(GizmoMode::Rotate);
				}
				ImGui::PopID();

				if (isActive)
				{
					ImGui::PopStyleColor(2);
				}

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tooltip != nullptr)
					ImGui::SetTooltip("%s", tooltip);
			};

			drawModeButton("SkeletonAddChild", ICON_FA_VECTOR_SQUARE, SkeletonInteractionMode::AddChild, "添加一条骨骼");
			ImGui::SameLine();
			drawModeButton("SkeletonMoveNode", ICON_FA_ARROWS_ALT, SkeletonInteractionMode::MoveJoint, "移动骨骼节点");
			ImGui::SameLine();
			drawModeButton("SkeletonRotateNode", ICON_FA_SYNC_ALT, SkeletonInteractionMode::RotateJoint, "旋转骨骼节点");
			ImGui::SameLine();
			drawModeButton("SkeletonBrushWeight", ICON_FA_PAINT_BRUSH, SkeletonInteractionMode::BrushWeight, "刷权重");

			ImGui::SameLine();
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f * imguiDpiScale);
			const char* modeLabel = "";
			switch (m_interactionMode)
			{
			case SkeletonInteractionMode::AddChild:    modeLabel = "添加骨骼"; break;
			case SkeletonInteractionMode::MoveJoint:   modeLabel = "移动节点"; break;
			case SkeletonInteractionMode::RotateJoint: modeLabel = "旋转节点"; break;
			case SkeletonInteractionMode::BrushWeight: modeLabel = "刷权重";   break;
			default: break;
			}
			ImGui::TextColored(ImVec4(0.65f, 0.78f, 0.95f, 1.0f), "%s", modeLabel);
		}

		ImGui::Spacing();

		// ── 骨架概览条 ──
		{
			int segmentCount = 0;
			for (const SkeletonJoint& joint : m_joints)
				if (joint.ParentIndex >= 0)
					++segmentCount;
			const int jointCount = static_cast<int>(m_joints.size());
			ImGui::Text("节点 %d  ·  骨段 %d", jointCount, segmentCount);

			ImGui::SameLine();
			const float statusCursorX = availWidth - 112.0f * imguiDpiScale;
			if (statusCursorX > ImGui::GetCursorPosX() + 20.0f)
				ImGui::SetCursorPosX(statusCursorX);

			if (ImGui::SmallButton("清空"))
				ResetChain();
			ImGui::SameLine();
			if (ImGui::SmallButton("保存"))
				m_saveToModelRequested = true;
		}

		ImGui::Separator();

		// ── 刷权重参数（仅刷权重模式展开且有内容） ──
		const bool isBrushMode = (m_interactionMode == SkeletonInteractionMode::BrushWeight);
		if (isBrushMode)
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("刷权重参数", isBrushMode ? ImGuiTreeNodeFlags_DefaultOpen : 0))
		{
			ImGui::BeginDisabled(!isBrushMode);
			ImGui::SetNextItemWidth(fieldWidth);
			ImGui::DragFloat("半径", &m_brushRadius, 0.01f, 0.01f, 10.0f, "%.3f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(fieldWidth);
			ImGui::DragFloat("强度", &m_brushStrength, 0.01f, 0.0f, 1.0f, "%.3f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(fieldWidth);
			ImGui::DragFloat("衰减", &m_brushFalloff, 0.05f, 0.1f, 8.0f, "%.3f");
			ImGui::Checkbox("影响子层级", &m_brushIncludeChildren);
			ImGui::EndDisabled();

			if (!isBrushMode)
				ImGui::TextDisabled("切换到刷权重模式以启用参数。");
		}

		// ── 选中节点属性 ──
		bool hasSelection = IsJointIndexValid(m_selectedJointIndex);
		if (!hasSelection)
			ImGui::SetNextItemOpen(false, ImGuiCond_Appearing);
		if (ImGui::CollapsingHeader("选中节点", hasSelection ? ImGuiTreeNodeFlags_DefaultOpen : 0))
		{
			if (hasSelection)
			{
				SkeletonJoint& selectedJoint = m_joints[static_cast<size_t>(m_selectedJointIndex)];
				const std::string jointNameUtf8 = SString::WstringToUTF8(selectedJoint.Name);

				ImGui::AlignTextToFramePadding();
				ImGui::Text("名称");
				ImGui::SameLine(fieldWidth + 8.0f * imguiDpiScale);
				ImGui::TextUnformatted(jointNameUtf8.c_str());

				ImGui::Spacing();

				// 使用表格替代原来的随机 SameLine 排列
				if (ImGui::BeginTable("##JointTransformTable", 4, ImGuiTableFlags_SizingStretchSame))
				{
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 20.0f);
					ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_None);
					ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_None);
					ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_None);
					ImGui::TableHeadersRow();

					// 位置行
					float pos[3] = { selectedJoint.Position.x, selectedJoint.Position.y, selectedJoint.Position.z };
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "P");
					for (int col = 0; col < 3; ++col)
					{
						ImGui::TableSetColumnIndex(col + 1);
						ImGui::PushID(col);
						ImGui::SetNextItemWidth(-FLT_MIN);
						if (ImGui::DragFloat("##Pos", &pos[col], 0.01f, 0.0f, 0.0f, "%.2f"))
						{
							if (col == 0) selectedJoint.Position.x = pos[0];
							else if (col == 1) selectedJoint.Position.y = pos[1];
							else selectedJoint.Position.z = pos[2];
							m_hasPendingChanges = true;
						}
						ImGui::PopID();
					}

					// 旋转行
					float rot[4] = { selectedJoint.Rotation.x, selectedJoint.Rotation.y, selectedJoint.Rotation.z, selectedJoint.Rotation.w };
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "R");
					for (int col = 0; col < 3; ++col)
					{
						ImGui::TableSetColumnIndex(col + 1);
						ImGui::PushID(col + 10);
						ImGui::SetNextItemWidth(-FLT_MIN);
						if (ImGui::DragFloat("##Rot", &rot[col], 0.01f, 0.0f, 0.0f, "%.2f"))
						{
							if (col == 0) selectedJoint.Rotation.x = rot[0];
							else if (col == 1) selectedJoint.Rotation.y = rot[1];
							else selectedJoint.Rotation.z = rot[2];
							m_hasPendingChanges = true;
						}
						ImGui::PopID();
					}

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.2f, 1.0f), "W");
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (ImGui::DragFloat("##RotW", &rot[3], 0.01f, 0.0f, 0.0f, "%.2f"))
					{
						selectedJoint.Rotation.w = rot[3];
						m_hasPendingChanges = true;
					}

					ImGui::EndTable();
				}

				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.25f, 0.25f, 0.70f));
				if (ImGui::Button("删除此节点及子树", ImVec2(availWidth, 0.0f)))
				{
					CancelActiveDrag();
					if (!m_joints.empty())
					{
						const int removeJointIndex = m_selectedJointIndex;
						std::vector<int> subtreeJointIndices;
						GatherSubtreeJointIndices(removeJointIndex, true, &subtreeJointIndices);
						std::vector<bool> removeFlags(m_joints.size(), false);
						for (const int jointIndex : subtreeJointIndices)
							removeFlags[static_cast<size_t>(jointIndex)] = true;
						std::vector<int> oldToNewIndex(m_joints.size(), -1);
						std::vector<SkeletonJoint> keptJoints;
						keptJoints.reserve(m_joints.size());
						for (size_t jointIndex = 0; jointIndex < m_joints.size(); ++jointIndex)
						{
							if (removeFlags[jointIndex])
								continue;
							oldToNewIndex[jointIndex] = static_cast<int>(keptJoints.size());
							keptJoints.push_back(m_joints[jointIndex]);
						}
						for (SkeletonJoint& joint : keptJoints)
						{
							if (joint.ParentIndex >= 0)
								joint.ParentIndex = oldToNewIndex[static_cast<size_t>(joint.ParentIndex)];
						}
						m_joints = std::move(keptJoints);
						m_hasPendingChanges = true;
					}
					m_selectedJointIndex = m_joints.empty() ? -1 : static_cast<int>(m_joints.size()) - 1;
					m_hoveredJointIndex = -1;
				}
				ImGui::PopStyleColor();

			}
			else
			{
				ImGui::TextDisabled("点击骨骼层级中的节点或视口中的关节来选中。");
			}
		}

		// ── 骨骼层级树 ──
		if (ImGui::CollapsingHeader("骨骼层级", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (m_joints.empty())
			{
				ImGui::TextDisabled("尚无骨骼节点。使用‘添加骨骼’模式在视口中拖拽来创建第一根骨骼。");
			}
			else
			{
				std::vector<std::vector<int>> children(m_joints.size());
				for (int i = 0; i < static_cast<int>(m_joints.size()); ++i)
				{
					const int parentIdx = m_joints[i].ParentIndex;
					if (parentIdx >= 0 && parentIdx < static_cast<int>(m_joints.size()))
						children[static_cast<size_t>(parentIdx)].push_back(i);
				}

				const float hierarchyHeight = (std::min)(
					static_cast<float>(m_joints.size()) * ImGui::GetTextLineHeightWithSpacing() + 12.0f,
					280.0f * imguiDpiScale);
				if (ImGui::BeginChild("##SkeletonBoneHierarchy", ImVec2(0.0f, hierarchyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
				{
					std::function<void(int, int)> renderJointNode = [&](int jointIndex, int depth)
					{
						if (jointIndex < 0 || jointIndex >= static_cast<int>(m_joints.size()))
							return;

						const SkeletonJoint& joint = m_joints[static_cast<size_t>(jointIndex)];
						const std::string jointNameUtf8 = SString::WstringToUTF8(joint.Name);
						const int childCount = static_cast<int>(children[static_cast<size_t>(jointIndex)].size());

						ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
						if (childCount == 0)
							flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
						if (jointIndex == m_selectedJointIndex)
							flags |= ImGuiTreeNodeFlags_Selected;

						ImGui::PushID(jointIndex);
						DirectX::XMFLOAT4 bc = GetBoneDisplayColor(jointIndex);
						ImVec2 cursor = ImGui::GetCursorScreenPos();
						const float sz = ImGui::GetTextLineHeight() * 0.60f;
						const float pad = (ImGui::GetTextLineHeight() - sz) * 0.5f;
						ImGui::GetWindowDrawList()->AddRectFilled(
							ImVec2(cursor.x + pad, cursor.y + pad),
							ImVec2(cursor.x + pad + sz, cursor.y + pad + sz),
							IM_COL32((int)(bc.x*255), (int)(bc.y*255), (int)(bc.z*255), 255));
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + sz + pad * 2.0f);
						const bool nodeOpen = ImGui::TreeNodeEx(jointNameUtf8.c_str(), flags);

						if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
							m_selectedJointIndex = jointIndex;
						if (ImGui::IsItemHovered())
							m_hoveredJointIndex = jointIndex;

						if (ImGui::BeginDragDropSource())
						{
							ImGui::SetDragDropPayload("SKELETON_BONE", &jointIndex, sizeof(jointIndex));
							ImGui::TextUnformatted(jointNameUtf8.c_str());
							ImGui::EndDragDropSource();
						}

						if (ImGui::BeginDragDropTarget())
						{
							if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SKELETON_BONE"))
							{
								if (payload->DataSize == sizeof(int))
								{
									const int draggedIndex = *static_cast<const int*>(payload->Data);
									if (draggedIndex >= 0 && draggedIndex < static_cast<int>(m_joints.size()) &&
										jointIndex >= 0 && jointIndex < static_cast<int>(m_joints.size()) &&
										draggedIndex != jointIndex &&
										m_joints[static_cast<size_t>(draggedIndex)].ParentIndex != jointIndex)
									{
										m_joints[static_cast<size_t>(draggedIndex)].ParentIndex = jointIndex;
										m_hasPendingChanges = true;
									}
								}
							}
							ImGui::EndDragDropTarget();
						}

						if (nodeOpen && childCount > 0)
						{
							for (int childIdx : children[static_cast<size_t>(jointIndex)])
								renderJointNode(childIdx, depth + 1);
							ImGui::TreePop();
						}
						ImGui::PopID();
					};

					int rootCount = 0;
					for (int i = 0; i < static_cast<int>(m_joints.size()); ++i)
					{
						if (m_joints[static_cast<size_t>(i)].ParentIndex < 0)
						{
							renderJointNode(i, 0);
							++rootCount;
						}
					}

					if (rootCount == 0 && !m_joints.empty())
						renderJointNode(0, 0);
				}
				ImGui::EndChild();
			}
		}

		// ── 操作提示 ──
		if (ImGui::CollapsingHeader("操作提示"))
		{
			ImGui::BulletText("左键拖拽视口：添加 / 移动 / 旋转骨骼节点。");
			ImGui::BulletText("骨骼层级中可拖拽节点来改变父子关系。");
		}
	}
	ImGui::End();
}

void SkeletonEditorTool::CancelActiveDrag()
{
	m_isDragging = false;
	m_dragMode = SkeletonDragMode::None;
	m_dragParentJointIndex = -1;
	m_dragTargetJointIndex = -1;
	m_dragPlanePointWS = { 0.0f, 0.0f, 0.0f };
	m_dragPlaneNormalWS = { 0.0f, 0.0f, 1.0f };
	m_boneStartWS = { 0.0f, 0.0f, 0.0f };
	m_boneEndWS = { 0.0f, 0.0f, 0.0f };
	m_moveDragStartHitWS = { 0.0f, 0.0f, 0.0f };
	m_moveDragJointIndices.clear();
	m_moveDragOriginalPositions.clear();
	m_rotateDragStartHitWS = { 0.0f, 0.0f, 0.0f };
	m_rotatePivotWS = { 0.0f, 0.0f, 0.0f };
	m_rotateDragJointIndices.clear();
	m_rotateDragOriginalPositions.clear();
	m_rotateDragOriginalTargetRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_lastBrushSampleScreenPos = { 0, 0 };
	m_lastBrushSampleTime = std::chrono::steady_clock::time_point{};
	m_hasLastBrushSample = false;
}
void SkeletonEditorTool::GatherSubtreeJointIndices(int rootJointIndex, bool includeRoot, std::vector<int>* outJointIndices) const
{
	if (outJointIndices == nullptr)
		return;

	outJointIndices->clear();
	if (!IsJointIndexValid(rootJointIndex))
		return;

	std::vector<int> pendingJointIndices = { rootJointIndex };
	while (!pendingJointIndices.empty())
	{
		const int jointIndex = pendingJointIndices.back();
		pendingJointIndices.pop_back();
		if (!IsJointIndexValid(jointIndex))
			continue;

		if (std::find(outJointIndices->begin(), outJointIndices->end(), jointIndex) != outJointIndices->end())
			continue;

		if (includeRoot || jointIndex != rootJointIndex)
			outJointIndices->push_back(jointIndex);

		for (size_t childIndex = 0; childIndex < m_joints.size(); ++childIndex)
		{
			if (m_joints[childIndex].ParentIndex == jointIndex)
				pendingJointIndices.push_back(static_cast<int>(childIndex));
		}
	}
}

void SkeletonEditorTool::ResetChain()
{
	CancelActiveDrag();
	m_joints.clear();
	m_hoveredJointIndex = -1;
	m_selectedJointIndex = -1;
	m_hasPendingChanges = true;
}

bool SkeletonEditorTool::IsJointIndexValid(int jointIndex) const
{
	return jointIndex >= 0 && jointIndex < static_cast<int>(m_joints.size());
}

std::wstring SkeletonEditorTool::MakeDefaultJointName() const
{
	return L"骨骼" + std::to_wstring(m_joints.size());
}

DirectX::XMFLOAT4 SkeletonEditorTool::GetBoneDisplayColor(int boneIndex) const
{
	const float hue = std::fmod(static_cast<float>(boneIndex) * 0.61803398875f + 0.13f, 1.0f);
	auto hueToRgb = [](float h) -> DirectX::XMFLOAT4 {
		h = h - std::floor(h);
		float r = std::abs(h * 6.0f - 3.0f) - 1.0f;
		float g = 2.0f - std::abs(h * 6.0f - 2.0f);
		float b = 2.0f - std::abs(h * 6.0f - 4.0f);
		r = std::clamp(r, 0.0f, 1.0f);
		g = std::clamp(g, 0.0f, 1.0f);
		b = std::clamp(b, 0.0f, 1.0f);
		float gray = 0.55f;
		return DirectX::XMFLOAT4(
			gray + (r - gray) * 0.85f,
			gray + (g - gray) * 0.85f,
			gray + (b - gray) * 0.85f, 1.0f);
	};
	return hueToRgb(hue);
}
