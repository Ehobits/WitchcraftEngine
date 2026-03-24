#pragma once

#include <vector>

#include "BaseComponent.h"
#include "Common/MeshSharedTypes.h"
#include "Common/TransformSharedTypes.h"
#include "Engine/EngineUtils.h"
#include "System/ModelSystem.h"

#include <wrl/client.h>

class D3DWindow;
class Engine;
class TransformComponent;

// MeshComponent 保存运行时可修改的网格数据，
// 同时负责把本地顶点/索引数据上传为 D3D 可渲染资源。
class MeshComponent : public BaseComponent
{
public:
	MeshComponent();
	~MeshComponent();
	void SetEngine(Engine* engine);
	Engine* GetEngine() const;

	// 根据当前 vertices / indices 创建 GPU 侧几何资源。
	void SetupMesh(TransformComponent* transformComponent, D3DWindow* dx, UINT indexCount, UINT vertexCount);
	// 使用已准备好的几何与材质信息向 D3DWindow 注册渲染项。
	void BuildRenderItems(D3DWindow* dx, UINT renderLayerIndex);
	// 将 ECS 中的变换同步到对应 RenderItem。
	void UpdateMesh(D3DWindow* dx, Transform WorldTransform, DirectX::XMFLOAT3 texTransform);
	void SetRenderLayerIndex(UINT renderLayerIndex);
	UINT GetRenderLayerIndex() const;
	void SetGeometryName(const std::wstring& name);
	std::wstring GetGeometryName() const;
	bool OwnsGeometry() const;
	// 绑定外部共享几何（例如天空球），当前组件不拥有其生命周期。
	void SetExternalRenderGeometry(const std::wstring& name, AggregateGraphicObj* aggregateGraphicObj);
	ObjectCollection* GetObjectCollection();
	void SetDefaultMaterialName(const std::wstring& name);

	UINT GetNumVertices();
	UINT GetNumFaces();

	void SetFileName(std::wstring name);
	std::wstring GetFileName();
	void SetMeshName(std::wstring name);
	std::wstring GetMeshName();
	// 仅复制轻量配置，不复制 GPU 资源指针。
	void CopySettingsFrom(const MeshComponent& other);
	// 复制 CPU 侧网格缓存，供 replace / 重建流程复用。
	void CopyCpuGeometryFrom(const MeshComponent& other);
	// 修改默认材质，并同步更新已存在的渲染项。
	void SetMaterial(std::wstring name);
	std::wstring GetMaterialName();
	std::wstring GetDefaultMaterialName() const;

	// 添加顶点
	void AddVertices(Vertex vertice);
	// 添加索引
	void AddIndices(UINT quantity);

	const std::vector<std::uint32_t>& GetIndices();
	const std::vector<Vertex>& GetVertices();
	UINT GetIndexCount();
	UINT GetVertexCount();

	void ClearCache();
	// 根据当前顶点数据重建包围盒。
	void CreateBoundingBox(TransformComponent* transformComponent);
	// 仅释放运行时渲染资源，不改动组件对象本身。
	void ReleaseRuntimeResources();

	// 从渲染系统中移除自身，并在需要时释放自有几何。
	void Destroy() override;

	virtual ComponentType GetComponentType() { return mComponentType; }
private:
	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;

	std::wstring fileName = L"";
	std::wstring meshName = L"";
	std::wstring geometryName = L"";
	// true 表示 geo 由当前组件创建并负责释放；false 表示引用外部共享几何。
	bool ownsGeometry = true;

	Engine* m_engine = nullptr;

	ObjectCollection Obj;
	AggregateGraphicObj AggrObject;
	MeshGeometry geo;
	UINT m_renderLayerIndex;
	std::wstring material_name = L"默认";

private:
	ComponentType mComponentType = ComponentType::Co_Mesh;
};

