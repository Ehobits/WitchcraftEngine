#pragma once

#include <filesystem>
#include <vector>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "Editor/EditorAssetCache.h"
#include "AssetsWindow.h"
#include "SYSTEM/ScriptingSystem.h"
#include "SYSTEM/PhysicsSystem.h"
#include "ECS/COMPONENT/GeneralComponent.h"
#include "ECS/COMPONENT/TransformComponent.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "ECS/COMPONENT/SkeletonComponent.h"
#include "ECS/COMPONENT/AnimatorComponent.h"
#include "ECS/COMPONENT/SkinnedMeshComponent.h"
#include "ECS/COMPONENT/SkinningRuntimeComponent.h"
#include "ECS/COMPONENT/LightComponent.h"
#include "ECS/COMPONENT/CameraComponent.h"
#include "ECS/COMPONENT/BillboardComponent.h"
#include "ECS/COMPONENT/ScriptingComponent.h"
#include "ECS/COMPONENT/PhysicsComponent.h"
#include "ECS/COMPONENT/RigidbodyComponent.h"

class D3DWindow;
class WitchcraECS;
class SceneEntityBase;
struct EntityComponentView;
enum class LightKind : unsigned int;

class InspectorWindow
{
public:
	void Init(D3DWindow* dx, AssetsWindow* assetsWindow, PhysicsSystem* physicsSystem, WitchcraECS* ecs);
	void Render();

	void NeedRender(bool render);

private:
	bool renderInspector = true;

	bool _Static = false;

	D3DWindow* m_dx = nullptr;
	AssetsWindow* m_assetsWindow = nullptr;
	PhysicsSystem* m_physicsSystem = nullptr;
	WitchcraECS* m_ecs = nullptr;
	bool m_syncMaterialChangesToFile = false;

	float kDirectionalShaderLightType = 0.0f;
	float kPointShaderLightType = 1.0f;
	float kSpotShaderLightType = 2.0f;

private:
	void RenderAdd();

	void UpdateComponent();
	void RenderComponent(const EntityComponentView& context);

	bool IsSkyMesh(const MeshComponent* meshComponent);
	//std::wstring GetInspectorEntityTypeLabel(
	//	WitchcraECS* ecs,
	//	SceneEntityBase* entity,
	//	const MeshComponent* meshComponent,
	//	const CameraComponent* cameraComponent,
	//	const TransformComponent* transformComponent);
	const char* GetInspectorLightKindLabel(LightKind kind);
	float ResolveInspectorLightShaderType(LightKind kind, float fallbackType);
	bool SaveMaterialToMaterialFile(const std::filesystem::path& materialFilePath, Material& material);
	std::wstring NormalizeToGenericPathString(const std::wstring& pathText);
	std::wstring ToProjectRelativePath(const std::filesystem::path& sourcePath);
	std::wstring ResolveTextureDisplayPath(const std::wstring& storedPath, const std::filesystem::path& materialFilePath);

	bool AcceptTextureAssetDrop(std::string* targetPathUtf8, const std::filesystem::path& materialFilePath);

	bool RenderReadonlyComponentPopup();

	template<typename TComponent>
	bool BeginInspectorComponentHeader(const char* label, const TComponent* component);

	template<typename OnRebuild, typename OnRemove>
	bool RenderReplaceableComponentPopup(OnRebuild&& onRebuild, OnRemove&& onRemove);

};
