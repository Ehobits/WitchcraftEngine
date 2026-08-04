#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <unordered_map>

class TransformComponent;
class WitchcraECS;
class SceneEntityBase;
class D3DWindow;

namespace PhysicsDefaultMaterial
{
	// 默认物理材质参数（用于新建碰撞器快照）。
	constexpr float StaticFriction = 0.5f;
	constexpr float DynamicFriction = 0.5f;
	constexpr float Restitution = 0.1f;
}

// 当前支持的碰撞器类型。
enum class PhysicsColliderType : std::uint32_t
{
	Box = 0,
	Plane = 1
};

// 负责 Jolt 物理世界生命周期、ECS 同步与变换回写。
class PhysicsSystem
{
public:
	PhysicsSystem();
	~PhysicsSystem();
	PhysicsSystem(const PhysicsSystem&) = delete;
	PhysicsSystem& operator=(const PhysicsSystem&) = delete;

	bool Init(D3DWindow* dx);
	void Shutdown();
	void Update(float deltaTime, WitchcraECS* ecs);
	bool IsInitialized() const;
	void SetLinearVelocity(SceneEntityBase* entity, const DirectX::XMFLOAT3& velocity);

private:
	struct PhysicsData;
	struct BodyState
	{
		std::uint32_t bodyIdIndex = 0;
		std::uint32_t bodyIdSequence = 0;
		std::uint64_t configHash = 0;
		bool dynamic = false;
		bool kinematic = false;
	};

private:
	// 类级物理配置常量，不是运行期全局状态。
	// 修改这些值会影响 PhysicsSystem 的初始化、固定步长推进和 Body 重建判断。
	static constexpr float kFixedPhysicsStep = 1.0f / 120.0f;
	static constexpr int kMaxPhysicsStepsPerFrame = 12;
	static constexpr int kCollisionStepsPerPhysicsUpdate = 4;
	static constexpr float kMaxAccumulatedTime = kFixedPhysicsStep * static_cast<float>(kMaxPhysicsStepsPerFrame);
	static constexpr float kMaxAcceptedDeltaTime = 0.25f;
	static constexpr std::uint32_t kPhysicsBodyBuildVersion = 2;
	static constexpr std::uint32_t kMaxBodies = 32768;
	static constexpr std::uint32_t kNumBodyMutexes = 0;
	static constexpr std::uint32_t kMaxBodyPairs = 65536;
	static constexpr std::uint32_t kMaxContactConstraints = 10240;

	bool SyncBodiesFromECS(WitchcraECS* ecs);
	bool RecreateBodyForEntity(WitchcraECS* ecs, SceneEntityBase* entity);
	bool UpdateBodyTransformFromEntity(WitchcraECS* ecs, SceneEntityBase* entity, const BodyState& bodyState);
	bool SyncEntityTransformFromBody(WitchcraECS* ecs, SceneEntityBase* entity, const BodyState& bodyState);
	void ApplyPendingLinearVelocity(SceneEntityBase* entity, const BodyState& bodyState);
	void RemoveBodyForEntity(SceneEntityBase* entity);
	void RemoveAllBodies();

private:
	D3DWindow* mDx = nullptr;
	std::unique_ptr<PhysicsData> mPhysicsData;
	std::unordered_map<SceneEntityBase*, BodyState> mBodies;
	std::unordered_map<SceneEntityBase*, DirectX::XMFLOAT3> mPendingLinearVelocities;
	float mAccumulator = 0.0f;
	bool mTypesRegistered = false;
};

// Inspector/ECS 侧使用的轻量碰撞器缓存。
// 真实 Jolt Shape 在 PhysicsSystem::RecreateBodyForEntity 中按快照重建。
struct BoxColliderBuffer
{
public:
	PhysicsColliderType colliderType = PhysicsColliderType::Box;
	bool activeComponent = true;
	float staticFriction = PhysicsDefaultMaterial::StaticFriction;
	float dynamicFriction = PhysicsDefaultMaterial::DynamicFriction;
	float restitution = PhysicsDefaultMaterial::Restitution;
	DirectX::XMFLOAT3 center = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT3 size = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);

public:
	void SetStaticFriction(float value);
	void SetDynamicFriction(float value);
	void SetRestitution(float value);
	void SetCenter(DirectX::XMFLOAT3 value);
	void SetSize(DirectX::XMFLOAT3 value);

public:
	void CreateShape(TransformComponent* transformComponent);
	void CreateMaterial();
};
