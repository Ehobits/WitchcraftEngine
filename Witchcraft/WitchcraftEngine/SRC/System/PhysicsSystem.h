#pragma once

#include <Windows.h>

#include <DirectXMath.h>

#include "Engine/EngineUtils.h"
#include "D3DWindow/D3DWindow.h"

class TransformComponent;

#define PVD_HOST L"127.0.0.1"
#define PVD_PORT 5425

/* GRAVITY TYPES */
#define EARTH_GRAVITY physx::PxVec3(0.0f, -9.81f, 0.0f)
#define MOON_GRAVITY physx::PxVec3(0.0f, -1.62f, 0.0f)
#define NONE_GRAVITY physx::PxVec3(0.0f, 0.0f, 0.0f)

namespace PhysicsDefaultMaterial
{
	constexpr float StaticFriction = 0.5f;
	constexpr float DynamicFriction = 0.5f;
	constexpr float Restitution = 0.1f;
}

/* LEGACY DEFAULT MATERIAL MACROS */
#define STATIC_FRICTION PhysicsDefaultMaterial::StaticFriction
#define DYNAMIC_FRICTION PhysicsDefaultMaterial::DynamicFriction
#define RESTITUTION PhysicsDefaultMaterial::Restitution

enum PhysicsProcesor : BYTE
{
	xUnknown = 0x00,
	xCPU = 0x10,
	xGPU = 0x20,
};

class PhysicsSystem
{
public:
	bool Init(D3DWindow* dx);
	void Update(float DeltaTime);

//public:
//	physx::PxPhysics* GetPhysics();
//	physx::PxScene* GetScene();
//
//private:
//	physx::PxDefaultAllocator	   gAllocator;
//	physx::PxDefaultErrorCallback  gErrorCallback;
//	physx::PxFoundation* gFoundation = NULL;
//	physx::PxPhysics* gPhysics = NULL;
//	physx::PxDefaultCpuDispatcher* gDispatcher = NULL;
//	physx::PxScene* gScene = NULL;
//	physx::PxPvd* gPvd = NULL;
//	physx::PxPvdTransport* transport = NULL;
//	physx::PxCudaContextManager* gCudaContextManager = NULL;

private:
	BYTE physicsProcesor = PhysicsProcesor::xUnknown;

};

struct BoxColliderBuffer
{
public:
	bool activeComponent = true;
	float staticFriction = PhysicsDefaultMaterial::StaticFriction;
	float dynamicFriction = PhysicsDefaultMaterial::DynamicFriction;
	float restitution = PhysicsDefaultMaterial::Restitution;
	DirectX::XMFLOAT3 center = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT3 size = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);

public:
	//physx::PxShape* GetShape() const;
	//physx::PxMaterial* GetMaterial() const;
	void SetStaticFriction(float value);
	//float GetStaticFriction();
	void SetDynamicFriction(float value);
	//float GetDynamicFriction();
	void SetRestitution(float value);
	//float GetRestitution();
	void SetCenter(DirectX::XMFLOAT3 value);
	//DirectX::XMFLOAT3 GetCenter() const;
	void SetSize(DirectX::XMFLOAT3 value);
	//DirectX::XMFLOAT3 GetSize() const;

public:
	void CreateShape(TransformComponent* transformComponent);
	void CreateMaterial();

private:
	//physx::PxShape* pxShape = nullptr;
	//physx::PxMaterial* pxMaterial = nullptr;
};
