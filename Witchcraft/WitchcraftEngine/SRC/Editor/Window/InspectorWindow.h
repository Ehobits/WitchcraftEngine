#pragma once

#include <filesystem>
#include <vector>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "AssetsWindow.h"
#include "SYSTEM/ScriptingSystem.h"
#include "SYSTEM/PhysicsSystem.h"

class D3DWindow;
class WitchcraECS;
class SceneEntityBase;
struct EntityComponentView;

class InspectorWindow
{
public:
	void Init(D3DWindow* dx, AssetsWindow* assetsWindow, PhysicsSystem* physicsSystem, WitchcraECS* ecs);
	void Render();

	void NeedRender(bool render);

private:
	struct MaterialFileEntry
	{
		std::wstring path;
		std::wstring displayName;
		std::wstring relativePath;
	};

	bool renderInspector = true;

	bool _Static = false;

	D3DWindow* m_dx = nullptr;
	AssetsWindow* m_assetsWindow = nullptr;
	PhysicsSystem* m_physicsSystem = nullptr;
	WitchcraECS* m_ecs = nullptr;
	std::vector<MaterialFileEntry> m_materialFileCache;            // ImportedAssets 下的 .wmat 缓存
	bool m_materialFileCacheDirty = true;
	std::vector<std::wstring> m_skyTextureFileCache;               // DATA/HDRIs 下可选天空贴图缓存
	bool m_skyTextureFileCacheDirty = true;
	bool m_syncMaterialChangesToFile = false;
private:
	void RefreshMaterialFileCache();
	void RefreshSkyTextureFileCache();
	void RenderAdd();

	void UpdateComponent();
	void RenderComponent(const EntityComponentView& context);
};
