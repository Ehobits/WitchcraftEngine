#include "MeshComponent.h"

#include "Engine/Engine.h"
#include "D3DWindow/D3DWindow.h"
#include "HELPERS/Helpers.h"
#include "ECS/COMPONENT/TransformComponent.h"

void MeshComponent::AddVertices(Vertex vertice)
{
	vertices.push_back(vertice);
}

void MeshComponent::AddIndices(UINT quantity)
{
	indices.push_back(quantity);
}

UINT MeshComponent::GetNumVertices()
{
	return static_cast<UINT>(vertices.size());
}

UINT MeshComponent::GetNumFaces()
{
	return static_cast<UINT>(indices.size() / 3);
}

// ClearCache清理MeshComponent本身管理的数据
void MeshComponent::ClearCache()
{
	vertices.clear();
	indices.clear();
}

void MeshComponent::SetupMesh(TransformComponent* transformComponent, D3DWindow* dx, UINT indexCount, UINT vertexCount)
{
	const bool needImmediateUploadSubmit = dx->IsCommandListClose();
	if (needImmediateUploadSubmit)
		dx->ResetCommandList();

	CreateBoundingBox(transformComponent);

	if (geometryName.empty())
		geometryName = meshName + L" Geo";

	AggrObject.IndexCount = indexCount;
	AggrObject.StartIndexLocation = 0;
	AggrObject.BaseVertexLocation = 0;
	Obj.AggrObject = &AggrObject;

	if (vertexCount == 0 || indexCount == 0 || vertices.empty() || indices.empty())
		return;

	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
	const UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

	geo = MeshGeometry();
	geo.Name = geometryName;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
	CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
	CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo.VertexBufferGPU = D3DWindow::CreateDefaultBuffer(
		dx->GetDevice(),
		dx->GetCommandList(),
		vertices.data(),
		vbByteSize,
		geo.VertexBufferUploader);

	geo.IndexBufferGPU = D3DWindow::CreateDefaultBuffer(
		dx->GetDevice(),
		dx->GetCommandList(),
		indices.data(),
		ibByteSize,
		geo.IndexBufferUploader);

	geo.vertexBufferView.BufferLocation = geo.VertexBufferGPU->GetGPUVirtualAddress();
	geo.vertexBufferView.StrideInBytes = sizeof(Vertex);
	geo.vertexBufferView.SizeInBytes = vbByteSize;

	geo.indexBufferView.BufferLocation = geo.IndexBufferGPU->GetGPUVirtualAddress();
	geo.indexBufferView.Format = dx->GetIndexBufferFormat();
	geo.indexBufferView.SizeInBytes = ibByteSize;

	dx->AddShapeGeometry(&geo);

	if (needImmediateUploadSubmit)
		dx->CloseCommandListAndSynchronize();
}

void MeshComponent::BuildRenderItems(D3DWindow* dx, UINT renderLayerIndex)
{
	m_renderLayerIndex = renderLayerIndex;

	if (geometryName.empty())
		geometryName = meshName + L" Geo";

	if (Obj.AggrObject == nullptr)
		Obj.AggrObject = &AggrObject;

	const std::wstring* materialNamePtr = material_name.empty() ? nullptr : &material_name;
	dx->AddRenderItem(meshName, &Obj, geometryName, m_renderLayerIndex, nullptr, nullptr, materialNamePtr);
}

void MeshComponent::SetRenderLayerIndex(UINT renderLayerIndex)
{
	m_renderLayerIndex = renderLayerIndex;
}

UINT MeshComponent::GetRenderLayerIndex() const
{
	return m_renderLayerIndex;
}

void MeshComponent::SetGeometryName(const std::wstring& name)
{
	geometryName = name;
}

std::wstring MeshComponent::GetGeometryName() const
{
	return geometryName;
}

bool MeshComponent::OwnsGeometry() const
{
	return ownsGeometry;
}

void MeshComponent::SetExternalRenderGeometry(const std::wstring& name, AggregateGraphicObj* aggregateGraphicObj)
{
	geometryName = name;
	ownsGeometry = false;

	if (aggregateGraphicObj != nullptr)
	{
		AggrObject = *aggregateGraphicObj;
		Obj.AggrObject = &AggrObject;
	}
	else
	{
		Obj.AggrObject = nullptr;
	}
}

ObjectCollection* MeshComponent::GetObjectCollection()
{
	return &Obj;
}

void MeshComponent::SetDefaultMaterialName(const std::wstring& name)
{
	material_name = name;
}

MeshComponent::MeshComponent()
{
	fileName = L"未设置";
	meshName = L"未设置";
	geometryName.clear();
	material_name.clear();
	m_renderLayerIndex = 不透明物体渲染项目;
	Obj.AggrObject = &AggrObject;
}

MeshComponent::~MeshComponent()
{
}

void MeshComponent::SetEngine(Engine* engine)
{
	m_engine = engine;
}

Engine* MeshComponent::GetEngine() const
{
	return m_engine;
}

void MeshComponent::UpdateMesh(D3DWindow* dx, Transform WorldTransform, DirectX::XMFLOAT3 texTransform)
{
	if (dx == nullptr)
		return;

	RenderItem* ri = dx->GetRenderItems(meshName);
	if (ri == nullptr)
		return;

	XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	if (m_renderLayerIndex == 天空渲染项目)
	{
		XMStoreFloat4x4(&ri->WorldTransform,
			XMMatrixScaling(
				8000.0f * WorldTransform.scale.x,
				8000.0f * WorldTransform.scale.y,
				8000.0f * WorldTransform.scale.z));
		XMStoreFloat4x4(&ri->TexTransform,
			XMMatrixRotationRollPitchYaw(
				WorldTransform.rotation.x * MathHelps::Pi / 45.0f / 4.0f,
				WorldTransform.rotation.y * MathHelps::Pi / 45.0f / 4.0f,
				WorldTransform.rotation.z * MathHelps::Pi / 45.0f / 4.0f));
	}
	else
	{
		XMStoreFloat4x4(&ri->WorldTransform,
			XMMatrixAffineTransformation(
				XMLoadFloat3(&WorldTransform.scale),
				zero,
				XMQuaternionRotationRollPitchYaw(
					WorldTransform.rotation.x * MathHelps::Pi / 45.0f / 4.0f,
					WorldTransform.rotation.y * MathHelps::Pi / 45.0f / 4.0f,
					WorldTransform.rotation.z * MathHelps::Pi / 45.0f / 4.0f),
				XMLoadFloat3(&WorldTransform.position)));

		XMStoreFloat4x4(&ri->TexTransform,
			XMMatrixScaling(texTransform.x, texTransform.y, texTransform.z));
	}

	dx->FreshenObjectCBs(meshName);
	dx->FreshenMaterialCBs();
}

const std::vector<std::uint32_t>& MeshComponent::GetIndices()
{
	return indices;
}

const std::vector<Vertex>& MeshComponent::GetVertices()
{
	return vertices;
}

UINT MeshComponent::GetIndexCount()
{
	return static_cast<UINT>(indices.size());
}

UINT MeshComponent::GetVertexCount()
{
	return static_cast<UINT>(vertices.size());
}

void MeshComponent::CreateBoundingBox(TransformComponent* transformComponent)
{
	if (transformComponent == nullptr || vertices.empty())
		return;

	float min_x = vertices[0].Pos.x;
	float min_y = vertices[0].Pos.y;
	float min_z = vertices[0].Pos.z;
	float max_x = vertices[0].Pos.x;
	float max_y = vertices[0].Pos.y;
	float max_z = vertices[0].Pos.z;

	for (size_t i = 0; i < vertices.size(); i++)
	{
		if (vertices[i].Pos.x < min_x)
			min_x = vertices[i].Pos.x;
		if (vertices[i].Pos.x > max_x)
			max_x = vertices[i].Pos.x;
		if (vertices[i].Pos.y < min_y)
			min_y = vertices[i].Pos.y;
		if (vertices[i].Pos.y > max_y)
			max_y = vertices[i].Pos.y;
		if (vertices[i].Pos.z < min_z)
			min_z = vertices[i].Pos.z;
		if (vertices[i].Pos.z > max_z)
			max_z = vertices[i].Pos.z;
	}

	DirectX::BoundingBox boundingBox(
		DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
		DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

	boundingBox.Center.x = (max_x - min_x) / 2.0f;
	boundingBox.Center.y = (max_y - min_y) / 2.0f;
	boundingBox.Center.z = (max_z - min_z) / 2.0f;
	boundingBox.Extents = boundingBox.Center;
	boundingBox.Extents.x = abs(boundingBox.Extents.x);
	boundingBox.Extents.y = abs(boundingBox.Extents.y);
	boundingBox.Extents.z = abs(boundingBox.Extents.z);
	transformComponent->SetBoundingBox(boundingBox);
}

// ReleaseRuntimeResources清理的D3D显然目标的数据
void MeshComponent::ReleaseRuntimeResources()
{
	D3DWindow* dx = m_engine != nullptr ? m_engine->GetD3DWindow() : nullptr;
	if (dx == nullptr)
		return;

	if (!meshName.empty() && dx->GetRenderItems(meshName) != nullptr)
		dx->RemoveRenderItem(meshName, m_renderLayerIndex);

	if (ownsGeometry && !geometryName.empty() && dx->HasShapeGeometry(geometryName))
		dx->RemoveShapeGeometry(geometryName);
}

void MeshComponent::Destroy()
{
	// 兼容旧调用入口：
	// 新代码更推荐显式区分 ReleaseRuntimeResources() 与 ClearCache()，
	// 这里保留组合行为，避免历史路径失效。
	ReleaseRuntimeResources();
	ClearCache();
}

void MeshComponent::SetFileName(std::wstring name)
{
	fileName = name;
}

std::wstring MeshComponent::GetFileName()
{
	return fileName;
}

void MeshComponent::SetMeshName(std::wstring name)
{
	meshName = name;
}

std::wstring MeshComponent::GetMeshName()
{
	return meshName;
}

void MeshComponent::CopySettingsFrom(const MeshComponent& other)
{
	m_engine = other.m_engine;
	fileName = other.fileName;
	meshName = other.meshName;
	geometryName = other.geometryName;
	m_renderLayerIndex = other.m_renderLayerIndex;
	material_name = other.material_name;
	ownsGeometry = other.ownsGeometry;
	AggrObject = other.AggrObject;
	Obj.AggrObject = other.Obj.AggrObject != nullptr ? &AggrObject : nullptr;
}

void MeshComponent::CopyCpuGeometryFrom(const MeshComponent& other)
{
	vertices = other.vertices;
	indices = other.indices;
}

void MeshComponent::SetMaterial(std::wstring name)
{
	material_name = name;

	D3DWindow* dx = m_engine != nullptr ? m_engine->GetD3DWindow() : nullptr;
	// 允许“先缓存材质名，后创建 RenderItem”。
	// 例如导入模型时，MeshComponent 可能会在 AddRenderItemsFromEntity 之前先绑定材质；
	// 这时只需要把材质名保存在组件里，后续重建 RenderItem 时会自动带上。
	if (dx != nullptr && dx->GetRenderItems(meshName) != nullptr)
		dx->SetMaterial(meshName, name);
}

std::wstring MeshComponent::GetMaterialName()
{
	return material_name;
}

std::wstring MeshComponent::GetDefaultMaterialName() const
{
	return material_name;
}
