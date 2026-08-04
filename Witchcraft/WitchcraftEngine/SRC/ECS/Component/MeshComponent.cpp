#include "MeshComponent.h"

#include "Engine/Engine.h"
#include "D3DWindow/D3DWindow.h"
#include "Editor/Window/ConsoleWindow.h"
#include "ECS/COMPONENT/TransformComponent.h"
#include <cstdarg>

void MeshComponent::LogDebugMessage(const wchar_t* format, ...) const
{
	if (m_engine == nullptr || format == nullptr || format[0] == L'\0')
		return;

	ConsoleWindow* consoleWindow = m_engine->GetConsoleWindow();
	if (consoleWindow == nullptr)
		return;

	wchar_t buffer[2048] = {};
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
	va_end(args);
	consoleWindow->AddDebugMessage(L"%s", buffer);
}

void MeshComponent::AddVertices(Vertex vertice)
{
	vertices.push_back(vertice);
}

bool MeshComponent::SetAllVertexColor(const DirectX::XMFLOAT4& color)
{
	if (vertices.empty())
		return false;

	for (Vertex& vertex : vertices)
		vertex.Color = color;

	return true;
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
	CreateBoundingBox(transformComponent);

	if (geometryName.empty())
		geometryName = meshName + L" Geo";

	AggrObject.IndexCount = indexCount;
	AggrObject.StartIndexLocation = 0;
	AggrObject.BaseVertexLocation = 0;
	Obj.AggrObject = &AggrObject;

	if (vertexCount == 0 || indexCount == 0 || vertices.empty() || indices.empty())
		return;

	// 使用“每网格独立上传命令列表”：
	// - 不复用 D3DWindow 主命令列表；
	// - 避免导入时与渲染主链路共享记录状态，降低状态污染风险。
	ComPtr<ID3D12CommandAllocator> uploadCommandAllocator = nullptr;
	ComPtr<ID3D12GraphicsCommandList> uploadCommandList = nullptr;
	ID3D12Device* device = dx->GetDevice();
	if (device == nullptr)
		return;
	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(uploadCommandAllocator.GetAddressOf())));
	ThrowIfFailed(device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		uploadCommandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(uploadCommandList.GetAddressOf())));

	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
	const UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

	geo = MeshGeometry();
	geo.Name = geometryName;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
	CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
	CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo.VertexBufferGPU = D3DWindow::CreateDefaultBuffer(
		device,
		uploadCommandList.Get(),
		vertices.data(),
		vbByteSize,
		geo.VertexBufferUploader);

	geo.IndexBufferGPU = D3DWindow::CreateDefaultBuffer(
		device,
		uploadCommandList.Get(),
		indices.data(),
		ibByteSize,
		geo.IndexBufferUploader);

	geo.vertexBufferView.BufferLocation = geo.VertexBufferGPU->GetGPUVirtualAddress();
	geo.vertexBufferView.StrideInBytes = sizeof(Vertex);
	geo.vertexBufferView.SizeInBytes = vbByteSize;
	geo.VertexByteStride = sizeof(Vertex);

	geo.indexBufferView.BufferLocation = geo.IndexBufferGPU->GetGPUVirtualAddress();
	geo.indexBufferView.Format = dx->GetIndexBufferFormat();
	geo.indexBufferView.SizeInBytes = ibByteSize;

	dx->AddShapeGeometry(&geo);

	ThrowIfFailed(uploadCommandList->Close());
	ID3D12CommandList* uploadCommandLists[] = { uploadCommandList.Get() };
	dx->GetCommandQueue()->ExecuteCommandLists(_countof(uploadCommandLists), uploadCommandLists);
	dx->FlushCommandQueue();

	LogDebugMessage(
		L"[MeshSetup][End] mesh=%s geo=%s vbGpu=%p ibGpu=%p",
		meshName.c_str(),
		geometryName.c_str(),
		geo.VertexBufferGPU.Get(),
		geo.IndexBufferGPU.Get());
}

void MeshComponent::BuildRenderItems(D3DWindow* dx, UINT renderLayerIndex)
{
	m_renderLayerIndex = renderLayerIndex;
	if (dx != nullptr)
	{
		const std::wstring resolvedMaterialName = material_name.empty() ? std::wstring(L"autoMat") : material_name;
		m_renderLayerIndex = dx->ResolveRenderLayerIndexByMaterial(m_renderLayerIndex, resolvedMaterialName);
	}

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

	RenderItem* ri = dx->GetRenderItem(meshName);
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

	DirectX::BoundingBox boundingBox{};
	DirectX::BoundingBox::CreateFromPoints(
		boundingBox,
		vertices.size(),
		&vertices[0].Pos,
		sizeof(Vertex));
	transformComponent->SetBoundingBox(boundingBox);
}

// ReleaseResources清理的D3D显然目标的数据
void MeshComponent::ReleaseResources()
{
	D3DWindow* dx = m_engine != nullptr ? m_engine->GetD3DWindow() : nullptr;
	if (dx == nullptr)
		return;

	if (!meshName.empty() && dx->GetRenderItem(meshName) != nullptr)
		dx->RemoveRenderItem(meshName, m_renderLayerIndex);

	if (ownsGeometry && !geometryName.empty() && dx->HasShapeGeometry(geometryName))
		dx->RemoveShapeGeometry(geometryName);
}

void MeshComponent::Destroy()
{
	// 兼容旧调用入口：
	// 新代码更推荐显式区分 ReleaseResources() 与 ClearCache()，
	// 这里保留组合行为，避免历史路径失效。
	ReleaseResources();
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
	if (dx == nullptr)
		return;

	RenderItem* renderItem = dx->GetRenderItem(meshName);
	if (renderItem != nullptr)
	{
		dx->SetMaterial(meshName, name);
		const std::wstring boundMaterialName = dx->GetMaterialName(meshName);
		m_renderLayerIndex = dx->ResolveRenderLayerIndexByMaterial(m_renderLayerIndex, boundMaterialName);
		return;
	}

	const std::wstring resolvedMaterialName = material_name.empty() ? std::wstring(L"autoMat") : material_name;
	m_renderLayerIndex = dx->ResolveRenderLayerIndexByMaterial(m_renderLayerIndex, resolvedMaterialName);
}

std::wstring MeshComponent::GetMaterialName()
{
	return material_name;
}

std::wstring MeshComponent::GetDefaultMaterialName() const
{
	return material_name;
}
