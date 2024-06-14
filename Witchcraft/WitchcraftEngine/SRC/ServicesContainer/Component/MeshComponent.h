#pragma once

#include <vector>

#include "ServicesContainer/ServicesContainer.h"
#include "BaseComponent.h"
#include "Engine/EngineUtils.h"
#include "System/ModelSystem.h"

#include <wrl/client.h>

class D3DWindow;

struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexC;
	DirectX::XMFLOAT3 TangentU;
};

struct MaterialBuffer
{
	std::wstring DiffusePath;
};

struct MeshComponent : public BaseComponent
{
public:
	MeshComponent();
	~MeshComponent();

	void SetupMesh(ServicesContainer* ComponentServices, D3DWindow* dx, UINT indexCount, UINT vertexCount);
	void BuildRenderItems(D3DWindow* dx, UINT renderLayerIndex);
	void UpdateMesh(ServicesContainer* ComponentServices, D3DWindow* dx, Transform WorldTransform, DirectX::XMFLOAT3 texTransform);

	UINT GetNumVertices();
	UINT GetNumFaces();

	void SetFileName(std::wstring name);
	std::wstring GetFileName();
	void SetMeshName(std::wstring name);
	std::wstring GetMeshName();
	void SetMaterial(std::wstring name);
	std::wstring GetMaterialName();

	// 添加顶点
	void AddVertices(Vertex vertice);
	// 添加索引
	void AddIndices(UINT quantity);

	const std::vector<std::uint32_t>& GetIndices();
	const std::vector<Vertex>& GetVertices();
	UINT GetIndexCount();
	UINT GetVertexCount();

	void BindingMaterial(D3DWindow* dx, std::wstring path);
	std::wstring material_name = L"默认";

	void ClearCache();
	void CreateBoundingBox(ServicesContainer* ComponentServices);

	void Destroy();
private:
	DXGI_FORMAT indexBufferViewFormat = DXGI_FORMAT_R32_UINT;
	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;

	std::wstring fileName = L"";
	std::wstring meshName = L"";

	D3DWindow* m_dx = nullptr;
	ID3D12Resource* diffuse_texture = nullptr;

	ObjectCollection Obj;
	AggregateGraphicObj AggrObject;
	MeshGeometry geo;
	UINT m_renderLayerIndex;

private:
	ComponentType mComponentType = ComponentType::Co_Mesh;
};