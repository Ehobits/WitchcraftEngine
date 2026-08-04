#include "PhysicsSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <unordered_set>
#include <vector>

#include "D3DWindow/D3DWindow.h"
#include "ECS/COMPONENT/PhysicsComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"
#include "ECS/WitchcraECS.h"
#include "HELPERS/Helpers.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

namespace PhysicsSystemDetail
{
	namespace PhysicsLayers
	{
		static constexpr JPH::ObjectLayer NonMoving = 0;
		static constexpr JPH::ObjectLayer Moving = 1;
		static constexpr JPH::ObjectLayer Count = 2;
	}

	namespace PhysicsBroadPhaseLayers
	{
		static constexpr JPH::BroadPhaseLayer NonMoving(0);
		static constexpr JPH::BroadPhaseLayer Moving(1);
		static constexpr uint32_t Count = 2;
	}

	class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
		{
			if (inObject1 == PhysicsLayers::NonMoving)
				return inObject2 == PhysicsLayers::Moving;
			if (inObject1 == PhysicsLayers::Moving)
				return true;
			return false;
		}
	};

	class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:
		BPLayerInterfaceImpl()
		{
			mObjectToBroadPhase[PhysicsLayers::NonMoving] = PhysicsBroadPhaseLayers::NonMoving;
			mObjectToBroadPhase[PhysicsLayers::Moving] = PhysicsBroadPhaseLayers::Moving;
		}

		JPH::uint GetNumBroadPhaseLayers() const override
		{
			return PhysicsBroadPhaseLayers::Count;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
		{
			return mObjectToBroadPhase[inLayer];
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
		{
			switch ((JPH::BroadPhaseLayer::Type)inLayer)
			{
			case (JPH::BroadPhaseLayer::Type)PhysicsBroadPhaseLayers::NonMoving:
				return "NonMoving";
			case (JPH::BroadPhaseLayer::Type)PhysicsBroadPhaseLayers::Moving:
				return "Moving";
			default:
				return "Unknown";
			}
		}
#endif

	private:
		JPH::BroadPhaseLayer mObjectToBroadPhase[PhysicsLayers::Count];
	};

	class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
		{
			if (inLayer1 == PhysicsLayers::NonMoving)
				return inLayer2 == PhysicsBroadPhaseLayers::Moving;
			if (inLayer1 == PhysicsLayers::Moving)
				return true;
			return false;
		}
	};

	template<typename T>
	void HashCombine(std::uint64_t& seed, const T& value)
	{
		std::uint64_t valueHash = static_cast<std::uint64_t>(std::hash<T>{}(value));
		seed ^= valueHash + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
	}

	void HashFloat(std::uint64_t& seed, float value)
	{
		std::uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		HashCombine(seed, bits);
	}

	PhysicsColliderType ResolveColliderType(std::uint32_t colliderTypeValue)
	{
		if (colliderTypeValue == static_cast<std::uint32_t>(PhysicsColliderType::Plane))
			return PhysicsColliderType::Plane;
		return PhysicsColliderType::Box;
	}

	bool IsColliderShapeValid(const EntityPhysicsComponentData::ColliderSnapshot& collider)
	{
		const auto isFiniteFloat = [](float value)
		{
			return std::isfinite(value);
		};
		const auto isFiniteFloat3 = [&](const DirectX::XMFLOAT3& value)
		{
			return isFiniteFloat(value.x) && isFiniteFloat(value.y) && isFiniteFloat(value.z);
		};

		if (!isFiniteFloat(collider.staticFriction) ||
			!isFiniteFloat(collider.dynamicFriction) ||
			!isFiniteFloat(collider.restitution) ||
			!isFiniteFloat3(collider.center) ||
			!isFiniteFloat3(collider.size))
		{
			return false;
		}

		const PhysicsColliderType colliderType = ResolveColliderType(collider.colliderType);
		if (colliderType == PhysicsColliderType::Plane)
			return collider.size.x > 0.0f || collider.size.z > 0.0f;

		return collider.size.x > 0.0f && collider.size.y > 0.0f && collider.size.z > 0.0f;
	}

	std::uint64_t BuildColliderHash(const std::vector<EntityPhysicsComponentData::ColliderSnapshot>& colliders)
	{
		// 通过配置哈希判断是否需要重建 Body，避免每帧重复销毁/创建。
		std::uint64_t hash = 0;
		for (const EntityPhysicsComponentData::ColliderSnapshot& collider : colliders)
		{
			HashCombine(hash, collider.colliderType);
			HashCombine(hash, collider.activeComponent);
			HashFloat(hash, collider.staticFriction);
			HashFloat(hash, collider.dynamicFriction);
			HashFloat(hash, collider.restitution);
			HashFloat(hash, collider.center.x);
			HashFloat(hash, collider.center.y);
			HashFloat(hash, collider.center.z);
			HashFloat(hash, collider.size.x);
			HashFloat(hash, collider.size.y);
			HashFloat(hash, collider.size.z);
		}
		return hash;
	}

	std::uint64_t BuildRigidBodyHash(const EntityRigidBodyComponentData* rigidBodySnapshot)
	{
		if (rigidBodySnapshot == nullptr)
			return 0;

		std::uint64_t hash = 0;
		HashFloat(hash, rigidBodySnapshot->mass);
		HashFloat(hash, rigidBodySnapshot->linearDamping);
		HashFloat(hash, rigidBodySnapshot->angularDamping);
		HashCombine(hash, rigidBodySnapshot->useGravity);
		HashCombine(hash, rigidBodySnapshot->kinematic);
		HashCombine(hash, rigidBodySnapshot->linearLockX);
		HashCombine(hash, rigidBodySnapshot->linearLockY);
		HashCombine(hash, rigidBodySnapshot->linearLockZ);
		HashCombine(hash, rigidBodySnapshot->angularLockX);
		HashCombine(hash, rigidBodySnapshot->angularLockY);
		HashCombine(hash, rigidBodySnapshot->angularLockZ);
		return hash;
	}

	DirectX::XMVECTOR ToDxQuaternion(JPH::QuatArg quaternion)
	{
		return DirectX::XMVectorSet(quaternion.GetX(), quaternion.GetY(), quaternion.GetZ(), quaternion.GetW());
	}

	JPH::Quat ToJoltQuaternion(const DirectX::XMFLOAT3& eulerDegrees)
	{
		const DirectX::XMVECTOR quaternion = DirectX::XMQuaternionRotationRollPitchYaw(
			DirectX::XMConvertToRadians(eulerDegrees.x),
			DirectX::XMConvertToRadians(eulerDegrees.y),
			DirectX::XMConvertToRadians(eulerDegrees.z));

		DirectX::XMFLOAT4 q{};
		DirectX::XMStoreFloat4(&q, quaternion);
		return JPH::Quat(q.x, q.y, q.z, q.w);
	}

	float NormalizeDegrees(float value)
	{
		while (value > 180.0f)
			value -= 360.0f;
		while (value < -180.0f)
			value += 360.0f;
		return value;
	}

	bool NearlyEqual(float a, float b, float epsilon = 0.0001f)
	{
		return std::fabs(a - b) <= epsilon;
	}

	DirectX::XMFLOAT3 ToEulerDegrees(JPH::QuatArg quaternion)
	{
		const DirectX::XMVECTOR normalized = DirectX::XMQuaternionNormalize(ToDxQuaternion(quaternion));
		DirectX::XMFLOAT4 q{};
		DirectX::XMStoreFloat4(&q, normalized);

		const float sinrCosp = 2.0f * (q.w * q.x + q.y * q.z);
		const float cosrCosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
		const float pitch = std::atan2(sinrCosp, cosrCosp);

		float sinp = 2.0f * (q.w * q.y - q.z * q.x);
		sinp = (std::max)(-1.0f, (std::min)(1.0f, sinp));
		const float yaw = std::asin(sinp);

		const float sinyCosp = 2.0f * (q.w * q.z + q.x * q.y);
		const float cosyCosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
		const float roll = std::atan2(sinyCosp, cosyCosp);

		return DirectX::XMFLOAT3(
			NormalizeDegrees(DirectX::XMConvertToDegrees(pitch)),
			NormalizeDegrees(DirectX::XMConvertToDegrees(yaw)),
			NormalizeDegrees(DirectX::XMConvertToDegrees(roll)));
	}

	DirectX::XMMATRIX BuildTransformMatrix(const Transform& transform)
	{
		return DirectX::XMMatrixAffineTransformation(
			DirectX::XMLoadFloat3(&transform.scale),
			DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
			DirectX::XMQuaternionRotationRollPitchYaw(
				DirectX::XMConvertToRadians(transform.rotation.x),
				DirectX::XMConvertToRadians(transform.rotation.y),
				DirectX::XMConvertToRadians(transform.rotation.z)),
			DirectX::XMLoadFloat3(&transform.position));
	}

	bool DecomposeMatrixToTransform(DirectX::CXMMATRIX matrix, Transform* outTransform, const DirectX::XMFLOAT3& fallbackScale)
	{
		if (outTransform == nullptr)
			return false;

		DirectX::XMVECTOR scale = DirectX::XMVectorZero();
		DirectX::XMVECTOR rotation = DirectX::XMQuaternionIdentity();
		DirectX::XMVECTOR translation = DirectX::XMVectorZero();
		if (!DirectX::XMMatrixDecompose(&scale, &rotation, &translation, matrix))
			return false;

		DirectX::XMStoreFloat3(&outTransform->position, translation);
		outTransform->rotation = ToEulerDegrees(JPH::Quat(
			DirectX::XMVectorGetX(rotation),
			DirectX::XMVectorGetY(rotation),
			DirectX::XMVectorGetZ(rotation),
			DirectX::XMVectorGetW(rotation)));
		outTransform->scale = fallbackScale;
		return true;
	}

	bool IsTransformNearlyEqual(const Transform& lhs, const Transform& rhs)
	{
		return NearlyEqual(lhs.position.x, rhs.position.x) &&
			NearlyEqual(lhs.position.y, rhs.position.y) &&
			NearlyEqual(lhs.position.z, rhs.position.z) &&
			NearlyEqual(lhs.rotation.x, rhs.rotation.x, 0.01f) &&
			NearlyEqual(lhs.rotation.y, rhs.rotation.y, 0.01f) &&
			NearlyEqual(lhs.rotation.z, rhs.rotation.z, 0.01f) &&
			NearlyEqual(lhs.scale.x, rhs.scale.x) &&
			NearlyEqual(lhs.scale.y, rhs.scale.y) &&
			NearlyEqual(lhs.scale.z, rhs.scale.z);
	}

	JPH::EAllowedDOFs BuildAllowedDOFs(const EntityRigidBodyComponentData& snapshot)
	{
		// ECS 的“锁定轴”语义与 Jolt 的 AllowedDOFs 对齐。
		JPH::EAllowedDOFs dofs = JPH::EAllowedDOFs::None;
		if (!snapshot.linearLockX) dofs |= JPH::EAllowedDOFs::TranslationX;
		if (!snapshot.linearLockY) dofs |= JPH::EAllowedDOFs::TranslationY;
		if (!snapshot.linearLockZ) dofs |= JPH::EAllowedDOFs::TranslationZ;
		if (!snapshot.angularLockX) dofs |= JPH::EAllowedDOFs::RotationX;
		if (!snapshot.angularLockY) dofs |= JPH::EAllowedDOFs::RotationY;
		if (!snapshot.angularLockZ) dofs |= JPH::EAllowedDOFs::RotationZ;
		// None 表示所有自由度都被锁定（即“全锁”），不能回退成 All。
		return dofs;
	}
}

using namespace PhysicsSystemDetail;

struct PhysicsSystem::PhysicsData
{
	// Jolt 运行期对象集中放在 PhysicsData，便于 Init/Shutdown 成对管理。
	PhysicsData(uint32_t workerThreadCount)
		: tempAllocator(10 * 1024 * 1024),
		jobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerThreadCount)
	{
	}

	ObjectLayerPairFilterImpl objectLayerPairFilter;
	BPLayerInterfaceImpl broadPhaseLayerInterface;
	ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
	JPH::TempAllocatorImpl tempAllocator;
	JPH::JobSystemThreadPool jobSystem;
	JPH::PhysicsSystem physicsSystem;
};

PhysicsSystem::PhysicsSystem() = default;

PhysicsSystem::~PhysicsSystem()
{
	Shutdown();
}

bool PhysicsSystem::Init(D3DWindow* dx)
{
	mDx = dx;
	mAccumulator = 0.0f;

	if (mPhysicsData != nullptr)
		return true;

	if (!mTypesRegistered)
	{
		// RegisterTypes 需要在分配器与工厂初始化后调用。
		JPH::RegisterDefaultAllocator();
		if (JPH::Factory::sInstance == nullptr)
			JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
		mTypesRegistered = true;
	}

	const uint32_t hardwareThreadCount = (std::max)(1u, std::thread::hardware_concurrency());
	const uint32_t workerThreadCount = hardwareThreadCount > 1 ? (hardwareThreadCount - 1) : 1;

	mPhysicsData = std::make_unique<PhysicsData>(workerThreadCount);
	mPhysicsData->physicsSystem.Init(
		kMaxBodies,
		kNumBodyMutexes,
		kMaxBodyPairs,
		kMaxContactConstraints,
		mPhysicsData->broadPhaseLayerInterface,
		mPhysicsData->objectVsBroadPhaseLayerFilter,
		mPhysicsData->objectLayerPairFilter);
	mPhysicsData->physicsSystem.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

	EngineHelpers::AddLog(L"[Physics] JoltPhysics 初始化完成。");
	return true;
}

void PhysicsSystem::Shutdown()
{
	RemoveAllBodies();
	mPhysicsData.reset();

	if (mTypesRegistered)
	{
		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
		mTypesRegistered = false;
	}
}

bool PhysicsSystem::IsInitialized() const
{
	return mPhysicsData != nullptr;
}

void PhysicsSystem::SetLinearVelocity(SceneEntityBase* entity, const DirectX::XMFLOAT3& velocity)
{
	if (entity == nullptr)
		return;
	if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
		return;

	mPendingLinearVelocities[entity] = velocity;
	if (mPhysicsData == nullptr)
		return;

	auto bodyIt = mBodies.find(entity);
	if (bodyIt == mBodies.end())
		return;

	ApplyPendingLinearVelocity(entity, bodyIt->second);
}

void PhysicsSystem::Update(float deltaTime, WitchcraECS* ecs)
{
	if (mPhysicsData == nullptr || ecs == nullptr)
		return;

	if (!SyncBodiesFromECS(ecs))
		return;

	if (mBodies.empty())
	{
		mAccumulator = 0.0f;
		return;
	}

	float safeDeltaTime = deltaTime;
	if (!std::isfinite(safeDeltaTime))
		safeDeltaTime = 0.0f;
	safeDeltaTime = (std::max)(0.0f, (std::min)(safeDeltaTime, kMaxAcceptedDeltaTime));

	mAccumulator += safeDeltaTime;
	mAccumulator = (std::min)(mAccumulator, kMaxAccumulatedTime);

	// 累加器消费固定步长，最多执行 kMaxPhysicsStepsPerFrame 次，防止低帧率螺旋。
	int subStepCount = 0;
	while (mAccumulator >= kFixedPhysicsStep && subStepCount < kMaxPhysicsStepsPerFrame)
	{
		const JPH::EPhysicsUpdateError updateResult = mPhysicsData->physicsSystem.Update(
			kFixedPhysicsStep,
			kCollisionStepsPerPhysicsUpdate,
			&mPhysicsData->tempAllocator,
			&mPhysicsData->jobSystem);
		if (updateResult != JPH::EPhysicsUpdateError::None)
		{
			mAccumulator = 0.0f;
			break;
		}

		mAccumulator -= kFixedPhysicsStep;
		++subStepCount;
	}

	if (subStepCount == 0)
		return;

	std::vector<SceneEntityBase*> changedEntities;
	changedEntities.reserve(mBodies.size());
	for (const auto& bodyPair : mBodies)
	{
		SceneEntityBase* entity = bodyPair.first;
		const BodyState& bodyState = bodyPair.second;
		if (!bodyState.dynamic || bodyState.kinematic)
			continue;

		if (SyncEntityTransformFromBody(ecs, entity, bodyState))
			changedEntities.push_back(entity);
	}

	if (!changedEntities.empty())
		ecs->SyncTransformsToFlecs();

	for (SceneEntityBase* entity : changedEntities)
	{
		if (mDx != nullptr)
			mDx->UpdateRenderItemsTransformFromEntity(entity, ecs);
	}
}

bool PhysicsSystem::SyncBodiesFromECS(WitchcraECS* ecs)
{
	if (mPhysicsData == nullptr || ecs == nullptr)
		return false;

	std::unordered_set<SceneEntityBase*> requiredEntities;
	std::vector<SceneEntityBase*> entityStack = ecs->GetSceneRootEntities();
	requiredEntities.reserve(entityStack.size() * 2 + 8);

	while (!entityStack.empty())
	{
		SceneEntityBase* entity = entityStack.back();
		entityStack.pop_back();
		if (entity == nullptr)
			continue;

		const std::vector<SceneEntityBase*>& children = ecs->GetSceneChildren(entity);
		entityStack.insert(entityStack.end(), children.begin(), children.end());

		PhysicsComponent* physicsComponent = ecs->GetComponent<PhysicsComponent>(entity);
		if (physicsComponent == nullptr)
			continue;

		bool hasActiveCollider = false;
		const std::vector<PhysicsBoxColliderSnapshot>& colliderSnapshots = physicsComponent->GetColliderSnapshots();
		for (const PhysicsBoxColliderSnapshot& colliderSnapshot : colliderSnapshots)
		{
			if (!colliderSnapshot.activeComponent)
				continue;
			EntityPhysicsComponentData::ColliderSnapshot colliderData{};
			colliderData.colliderType = colliderSnapshot.colliderType;
			colliderData.activeComponent = colliderSnapshot.activeComponent;
			colliderData.size = colliderSnapshot.size;
			if (!IsColliderShapeValid(colliderData))
				continue;
			hasActiveCollider = true;
			break;
		}

		if (!hasActiveCollider)
			continue;

		requiredEntities.insert(entity);
	}

	for (auto it = mBodies.begin(); it != mBodies.end();)
	{
		// ECS 已删除或不再需要物理的实体，在此回收 Body。
		SceneEntityBase* entity = it->first;
		if (requiredEntities.find(entity) != requiredEntities.end() && ecs->HasEntity(entity))
		{
			++it;
			continue;
		}

		RemoveBodyForEntity(entity);
		it = mBodies.erase(it);
	}

	for (SceneEntityBase* entity : requiredEntities)
	{
		if (!RecreateBodyForEntity(ecs, entity))
			continue;

		auto bodyIt = mBodies.find(entity);
		if (bodyIt == mBodies.end())
			continue;

		UpdateBodyTransformFromEntity(ecs, entity, bodyIt->second);
	}

	// 清理无对应 Body 的待设置速度，避免实体移除后残留悬空键。
	for (auto pendingIt = mPendingLinearVelocities.begin(); pendingIt != mPendingLinearVelocities.end();)
	{
		if (mBodies.find(pendingIt->first) == mBodies.end())
		{
			pendingIt = mPendingLinearVelocities.erase(pendingIt);
			continue;
		}

		++pendingIt;
	}

	return true;
}

bool PhysicsSystem::RecreateBodyForEntity(WitchcraECS* ecs, SceneEntityBase* entity)
{
	if (mPhysicsData == nullptr || ecs == nullptr || entity == nullptr)
		return false;

	PhysicsComponent* physicsComponent = ecs->GetComponent<PhysicsComponent>(entity);
	if (physicsComponent == nullptr)
		return false;

	std::vector<EntityPhysicsComponentData::ColliderSnapshot> colliders;
	colliders.reserve(physicsComponent->GetBoxColliderCount());

	EntityPhysicsComponentData::ColliderSnapshot primaryCollider{};
	bool hasPrimaryCollider = false;
	bool hasPlaneCollider = false;
	for (size_t colliderIndex = 0; colliderIndex < physicsComponent->GetBoxColliderCount(); ++colliderIndex)
	{
		EntityPhysicsComponentData::ColliderSnapshot colliderSnapshot{};
		if (!ecs->GetEntityPhysicsColliderSnapshot(entity, colliderIndex, &colliderSnapshot))
			continue;
		if (!colliderSnapshot.activeComponent)
			continue;
		if (!IsColliderShapeValid(colliderSnapshot))
			continue;

		if (!hasPrimaryCollider)
		{
			primaryCollider = colliderSnapshot;
			hasPrimaryCollider = true;
		}
		if (ResolveColliderType(colliderSnapshot.colliderType) == PhysicsColliderType::Plane)
			hasPlaneCollider = true;
		colliders.push_back(colliderSnapshot);
	}

	if (!hasPrimaryCollider || colliders.empty())
		return false;

	EntityRigidBodyComponentData rigidBodySnapshot{};
	const bool hasRigidBodyComponent = ecs->GetEntityRigidBodySnapshot(entity, &rigidBodySnapshot);
	const bool hasRigidBody = hasRigidBodyComponent && !hasPlaneCollider;
	const bool isKinematic = hasRigidBody && rigidBodySnapshot.kinematic;
	const bool isDynamic = hasRigidBody && !isKinematic;

	std::uint64_t configHash = BuildColliderHash(colliders);
	HashCombine(configHash, BuildRigidBodyHash(hasRigidBody ? &rigidBodySnapshot : nullptr));
	HashCombine(configHash, kPhysicsBodyBuildVersion);
	HashCombine(configHash, isDynamic);
	HashCombine(configHash, isKinematic);

	auto existingBodyIt = mBodies.find(entity);
	if (existingBodyIt != mBodies.end() && existingBodyIt->second.configHash == configHash)
	{
		ApplyPendingLinearVelocity(entity, existingBodyIt->second);
		return true;
	}

	// 配置变化时重建 Body（Shape/MotionType/材质参数都会在这里更新）。
	RemoveBodyForEntity(entity);
	mBodies.erase(entity);

	JPH::StaticCompoundShapeSettings compoundShapeSettings;
	for (const EntityPhysicsComponentData::ColliderSnapshot& collider : colliders)
	{
		if (ResolveColliderType(collider.colliderType) == PhysicsColliderType::Plane)
		{
			// 平面碰撞器：使用 PlaneShape，避免“超薄盒体”造成浮空感。
			const float planeHalfExtent = (std::max)(0.1f, (std::max)(std::fabs(collider.size.x), std::fabs(collider.size.z)));
			const float requestedHalfExtent = planeHalfExtent;
			const float planeCollisionHalfExtent = (std::max)(requestedHalfExtent, 2000.0f);
			const float planeHalfThickness = 0.2f;
			compoundShapeSettings.AddShape(
				JPH::Vec3(collider.center.x, collider.center.y - planeHalfThickness, collider.center.z),
				JPH::Quat::sIdentity(),
				new JPH::BoxShape(JPH::Vec3(planeCollisionHalfExtent, planeHalfThickness, planeCollisionHalfExtent)));
			continue;
		}

		const JPH::Vec3 halfExtent(
			(std::max)(0.001f, collider.size.x),
			(std::max)(0.001f, collider.size.y),
			(std::max)(0.001f, collider.size.z));
		compoundShapeSettings.AddShape(
			JPH::Vec3(collider.center.x, collider.center.y, collider.center.z),
			JPH::Quat::sIdentity(),
			new JPH::BoxShape(halfExtent));
	}

	JPH::Shape::ShapeResult shapeResult = compoundShapeSettings.Create();
	if (shapeResult.HasError() || shapeResult.Get() == nullptr)
		return false;

	Transform worldTransform{};
	if (!ecs->GetEntityWorldTransform(entity, &worldTransform))
	{
		if (!ecs->GetEntityEditableLocalTransform(entity, &worldTransform))
			worldTransform = Transform{};
	}

	const JPH::EMotionType motionType = isKinematic ? JPH::EMotionType::Kinematic : (isDynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static);
	const JPH::ObjectLayer objectLayer = isDynamic || isKinematic ? PhysicsLayers::Moving : PhysicsLayers::NonMoving;

	JPH::BodyCreationSettings creationSettings(
		shapeResult.Get(),
		JPH::RVec3(worldTransform.position.x, worldTransform.position.y, worldTransform.position.z),
		ToJoltQuaternion(worldTransform.rotation),
		motionType,
		objectLayer);
	// 动态刚体启用 CCD（LinearCast），减少高速/大位移时穿透平面碰撞器的问题。
	creationSettings.mMotionQuality = isDynamic ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
	// Jolt 只有一个摩擦参数：这里用静/动摩擦的较大值近似。
	creationSettings.mFriction = (std::max)(0.0f, (std::max)(primaryCollider.staticFriction, primaryCollider.dynamicFriction));
	creationSettings.mRestitution = (std::max)(0.0f, primaryCollider.restitution);
	creationSettings.mAllowSleeping = !isKinematic;
	creationSettings.mAllowedDOFs = hasRigidBody ? BuildAllowedDOFs(rigidBodySnapshot) : JPH::EAllowedDOFs::All;
	creationSettings.mUserData = reinterpret_cast<uint64_t>(entity);

	if (hasRigidBody)
	{
		creationSettings.mLinearDamping = (std::max)(0.0f, rigidBodySnapshot.linearDamping);
		creationSettings.mAngularDamping = (std::max)(0.0f, rigidBodySnapshot.angularDamping);
		creationSettings.mGravityFactor = rigidBodySnapshot.useGravity ? 1.0f : 0.0f;

		if (motionType != JPH::EMotionType::Static && rigidBodySnapshot.mass > 0.0f)
		{
			creationSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			creationSettings.mMassPropertiesOverride.mMass = rigidBodySnapshot.mass;
		}
	}

	JPH::BodyInterface& bodyInterface = mPhysicsData->physicsSystem.GetBodyInterface();
	const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
		creationSettings,
		(isDynamic || isKinematic) ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	if (bodyId.IsInvalid())
		return false;

	BodyState state{};
	state.bodyIdIndex = bodyId.GetIndex();
	state.bodyIdSequence = bodyId.GetSequenceNumber();
	state.configHash = configHash;
	state.dynamic = isDynamic;
	state.kinematic = isKinematic;
	mBodies[entity] = state;
	ApplyPendingLinearVelocity(entity, mBodies[entity]);
	return true;
}

bool PhysicsSystem::UpdateBodyTransformFromEntity(WitchcraECS* ecs, SceneEntityBase* entity, const BodyState& bodyState)
{
	if (mPhysicsData == nullptr || ecs == nullptr || entity == nullptr)
		return false;

	JPH::BodyID bodyId(bodyState.bodyIdIndex, static_cast<uint8_t>(bodyState.bodyIdSequence));
	JPH::BodyInterface& bodyInterface = mPhysicsData->physicsSystem.GetBodyInterface();
	if (!bodyInterface.IsAdded(bodyId))
		return false;

	if (bodyState.dynamic && !bodyState.kinematic)
		// 动态刚体由物理驱动，不覆盖其世界变换。
		return true;

	Transform worldTransform{};
	if (!ecs->GetEntityWorldTransform(entity, &worldTransform))
		return false;

	const JPH::RVec3 position(worldTransform.position.x, worldTransform.position.y, worldTransform.position.z);
	const JPH::Quat rotation = ToJoltQuaternion(worldTransform.rotation);
	bodyInterface.SetPositionAndRotationWhenChanged(
		bodyId,
		position,
		rotation,
		bodyState.kinematic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	return true;
}

bool PhysicsSystem::SyncEntityTransformFromBody(WitchcraECS* ecs, SceneEntityBase* entity, const BodyState& bodyState)
{
	if (mPhysicsData == nullptr || ecs == nullptr || entity == nullptr)
		return false;

	JPH::BodyID bodyId(bodyState.bodyIdIndex, static_cast<uint8_t>(bodyState.bodyIdSequence));
	JPH::BodyInterface& bodyInterface = mPhysicsData->physicsSystem.GetBodyInterface();
	if (!bodyInterface.IsAdded(bodyId))
		return false;

	Transform localTransform{};
	if (!ecs->GetEntityEditableLocalTransform(entity, &localTransform))
		localTransform = Transform{};

	const JPH::RVec3 position = bodyInterface.GetPosition(bodyId);
	const JPH::Quat rotation = bodyInterface.GetRotation(bodyId);

	SceneEntityBase* parentEntity = ecs->GetParentEntity(entity);
	if (parentEntity == nullptr)
	{
		// 根节点：直接写回 local == world。
		Transform updatedLocalTransform = localTransform;
		updatedLocalTransform.position = DirectX::XMFLOAT3(
			static_cast<float>(position.GetX()),
			static_cast<float>(position.GetY()),
			static_cast<float>(position.GetZ()));
		updatedLocalTransform.rotation = ToEulerDegrees(rotation);

		if (IsTransformNearlyEqual(updatedLocalTransform, localTransform))
			return false;

		return ecs->SetEntityEditableLocalTransform(entity, updatedLocalTransform, false);
	}

	DirectX::XMFLOAT4X4 parentWorldMatrix{};
	if (!ecs->GetEntityWorldMatrix(parentEntity, &parentWorldMatrix))
		return false;

	const DirectX::XMMATRIX parentMatrix = DirectX::XMLoadFloat4x4(&parentWorldMatrix);
	const DirectX::XMMATRIX parentInverse = DirectX::XMMatrixInverse(nullptr, parentMatrix);
	const DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixAffineTransformation(
		DirectX::XMLoadFloat3(&localTransform.scale),
		DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
		ToDxQuaternion(rotation),
		DirectX::XMVectorSet(
			static_cast<float>(position.GetX()),
			static_cast<float>(position.GetY()),
			static_cast<float>(position.GetZ()),
			1.0f));
	const DirectX::XMMATRIX localMatrix = worldMatrix * parentInverse;

	Transform updatedLocalTransform = localTransform;
	if (!DecomposeMatrixToTransform(localMatrix, &updatedLocalTransform, localTransform.scale))
		return false;

	if (IsTransformNearlyEqual(updatedLocalTransform, localTransform))
		return false;

	return ecs->SetEntityEditableLocalTransform(entity, updatedLocalTransform, false);
}

void PhysicsSystem::ApplyPendingLinearVelocity(SceneEntityBase* entity, const BodyState& bodyState)
{
	if (mPhysicsData == nullptr || entity == nullptr)
		return;

	auto pendingIt = mPendingLinearVelocities.find(entity);
	if (pendingIt == mPendingLinearVelocities.end())
		return;

	if (!bodyState.dynamic || bodyState.kinematic)
	{
		mPendingLinearVelocities.erase(pendingIt);
		return;
	}

	JPH::BodyID bodyId(bodyState.bodyIdIndex, static_cast<uint8_t>(bodyState.bodyIdSequence));
	JPH::BodyInterface& bodyInterface = mPhysicsData->physicsSystem.GetBodyInterface();
	if (!bodyInterface.IsAdded(bodyId))
		return;

	const DirectX::XMFLOAT3 velocity = pendingIt->second;
	bodyInterface.SetLinearVelocity(bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
	bodyInterface.ActivateBody(bodyId);
	mPendingLinearVelocities.erase(pendingIt);
}

void PhysicsSystem::RemoveBodyForEntity(SceneEntityBase* entity)
{
	if (mPhysicsData == nullptr || entity == nullptr)
		return;

	auto bodyIt = mBodies.find(entity);
	if (bodyIt == mBodies.end())
		return;

	JPH::BodyID bodyId(bodyIt->second.bodyIdIndex, static_cast<uint8_t>(bodyIt->second.bodyIdSequence));
	JPH::BodyInterface& bodyInterface = mPhysicsData->physicsSystem.GetBodyInterface();
	if (bodyInterface.IsAdded(bodyId))
		bodyInterface.RemoveBody(bodyId);
	bodyInterface.DestroyBody(bodyId);
	mPendingLinearVelocities.erase(entity);
}

void PhysicsSystem::RemoveAllBodies()
{
	if (mPhysicsData == nullptr)
	{
		mBodies.clear();
		mPendingLinearVelocities.clear();
		return;
	}

	std::vector<SceneEntityBase*> entities;
	entities.reserve(mBodies.size());
	for (const auto& bodyPair : mBodies)
		entities.push_back(bodyPair.first);

	for (SceneEntityBase* entity : entities)
		RemoveBodyForEntity(entity);

	mBodies.clear();
	mPendingLinearVelocities.clear();
}

void BoxColliderBuffer::SetStaticFriction(float value)
{
	if (!std::isfinite(value) || value < 0.0f)
		return;
	staticFriction = value;
}

void BoxColliderBuffer::SetDynamicFriction(float value)
{
	if (!std::isfinite(value) || value < 0.0f)
		return;
	dynamicFriction = value;
}

void BoxColliderBuffer::SetRestitution(float value)
{
	if (!std::isfinite(value) || value < 0.0f)
		return;
	restitution = value;
}

void BoxColliderBuffer::SetCenter(DirectX::XMFLOAT3 value)
{
	if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
		return;
	center = value;
}

void BoxColliderBuffer::SetSize(DirectX::XMFLOAT3 value)
{
	if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
		return;
	size = value;
}

void BoxColliderBuffer::CreateShape(TransformComponent* transformComponent)
{
	(void)transformComponent;
}

void BoxColliderBuffer::CreateMaterial()
{
}
