#include "MeshComponent.h"
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

void MeshComponent::ClearCache()
{
	// 仅清理 CPU 侧缓存；GPU 资源释放由 Destroy / RemoveShapeGeometry 处理。
	vertices.clear();
	indices.clear();
}

void MeshComponent::SetupMesh(ServicesContainer* ComponentServices, D3DWindow* dx, UINT indexCount, UINT vertexCount)
{
	m_dx = dx;
	if (m_dx == nullptr)
		return;

	// 运行时导入模型时，主命令列表通常已经处于 Close 状态。
	// CreateDefaultBuffer 需要在可录制的命令列表上写入上传命令，
	// 因此这里在需要时临时重置并在末尾立即提交。
	const bool needImmediateUploadSubmit = m_dx->IsCommandListClose();
	if (needImmediateUploadSubmit)
		m_dx->ResetCommandList();

	CreateBoundingBox(ComponentServices);

	if (geometryName.empty())
		geometryName = meshName + L" Geo";

	// 当前组件默认只维护一个子网格范围。
	AggrObject.IndexCount = indexCount;
	AggrObject.StartIndexLocation = 0;
	AggrObject.BaseVertexLocation = 0;
	Obj.AggrObject = &AggrObject;

	if (vertexCount == 0 || indexCount == 0 || vertices.empty() || indices.empty())
		return;

	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
	const UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

	// 重新构建本地 MeshGeometry 缓存，并提交给 D3DWindow 管理。
	geo = MeshGeometry();
	geo.Name = geometryName;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
	CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
	CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo.VertexBufferGPU = D3DWindow::CreateDefaultBuffer(
		m_dx->GetDevice(),
		m_dx->GetCommandList(),
		vertices.data(),
		vbByteSize,
		geo.VertexBufferUploader);

	geo.IndexBufferGPU = D3DWindow::CreateDefaultBuffer(
		m_dx->GetDevice(),
		m_dx->GetCommandList(),
		indices.data(),
		ibByteSize,
		geo.IndexBufferUploader);

	geo.vertexBufferView.BufferLocation = geo.VertexBufferGPU->GetGPUVirtualAddress();
	geo.vertexBufferView.StrideInBytes = sizeof(Vertex);
	geo.vertexBufferView.SizeInBytes = vbByteSize;

	geo.indexBufferView.BufferLocation = geo.IndexBufferGPU->GetGPUVirtualAddress();
	geo.indexBufferView.Format = m_dx->GetIndexBufferFormat();
	geo.indexBufferView.SizeInBytes = ibByteSize;

	m_dx->AddShapeGeometry(&geo);

	if (needImmediateUploadSubmit)
		m_dx->CloseCommandListAndSynchronize();
}

void MeshComponent::BuildRenderItems(D3DWindow* dx, UINT renderLayerIndex)
{
	m_dx = dx;
	m_renderLayerIndex = renderLayerIndex;

	if (m_dx == nullptr)
		return;

	if (geometryName.empty())
		geometryName = meshName + L" Geo";

	if (Obj.AggrObject == nullptr)
		Obj.AggrObject = &AggrObject;

	// RenderItem 名称与 meshName 保持一致，便于后续按实体名回查。
	const std::wstring* materialNamePtr = material_name.empty() ? nullptr : &material_name;
	m_dx->AddRenderItem(meshName, &Obj, geometryName, m_renderLayerIndex, nullptr, nullptr, materialNamePtr);
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

void MeshComponent::SetExternalRenderGeometry(const std::wstring& name, AggregateGraphicObj* aggregateGraphicObj)
{
	geometryName = name;
	ownsGeometry = false;

	// 外部几何通常来自全局共享资源，这里只复制绘制范围，不接管原资源。
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

void MeshComponent::UpdateMesh(ServicesContainer* ComponentServices, D3DWindow* dx, Transform WorldTransform, DirectX::XMFLOAT3 texTransform)
{
	if (dx == nullptr)
		return;

	RenderItem* ri = dx->GetRenderItems(meshName);
	if (ri == nullptr)
		return;

	XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	if (m_renderLayerIndex == 天空渲染项目)
	{
		// 天空始终围绕相机绘制：
		// 1. 忽略位置
		// 2. 缩放控制天空球半径
		// 3. 旋转写入 TexTransform，用于在 shader 中旋转采样方向
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
		// 普通网格使用 ECS 变换生成世界矩阵。
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

	// RenderItem 内容发生变化后，需要标记对象/材质常量缓冲重新同步。
	dx->FreshenObjectCBs();
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

void MeshComponent::CreateBoundingBox(ServicesContainer* ComponentServices)
{
	if (ComponentServices == nullptr || vertices.empty())
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

	TransformComponent* tmp = ComponentServices->FindServiceAs<TransformComponent>(L"TransformComponent");
	if (tmp != nullptr)
	{
		// 这里使用局部顶点范围生成 AABB，供编辑器选择/显示使用。
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
		tmp->SetBoundingBox(boundingBox);
	}
}

void MeshComponent::Destroy()
{
	ClearCache();
	if (m_dx == nullptr)
		return;

	// 先移除渲染项，再按 ownsGeometry 决定是否释放对应几何。
	m_dx->RemoveRenderItem(meshName, m_renderLayerIndex);

	if (ownsGeometry)
		m_dx->RemoveShapeGeometry(GetGeometryName());
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

void MeshComponent::SetMaterial(std::wstring name)
{
	material_name = name;

	// 如果渲染项已存在，则立即把材质切换到运行时渲染系统。
	if (m_dx != nullptr)
		m_dx->SetMaterial(meshName, name);
}

std::wstring MeshComponent::GetMaterialName()
{
	return material_name;
}

std::wstring MeshComponent::GetDefaultMaterialName() const
{
	return material_name;
}
