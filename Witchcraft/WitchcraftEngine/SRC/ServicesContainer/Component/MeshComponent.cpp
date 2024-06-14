#include "MeshComponent.h"
#include "D3DWindow/D3DWindow.h"
#include "HELPERS/Helpers.h"
#include "ServicesContainer/COMPONENT/TransformComponent.h"
#include "System/Assets.h"
#include "String/SStringUtils.h"
#include "ENGINE/EngineUtils.h"

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
	return (UINT)vertices.size();
}
UINT MeshComponent::GetNumFaces()
{
	return (UINT)indices.size() / 3;
}
void MeshComponent::ClearCache()
{
	vertices.clear();
	indices.clear();
}

void MeshComponent::SetupMesh(ServicesContainer* ComponentServices, D3DWindow* dx, UINT indexCount, UINT vertexCount)
{
	m_dx = dx;
	CreateBoundingBox(ComponentServices);

	// 创建并向D3DWindow提交资源
	{
		AggrObject.IndexCount = indexCount;
		AggrObject.StartIndexLocation = 0;// dx->GetStartIndexLocation();
		AggrObject.BaseVertexLocation = 0;// dx->GetBaseVertexLocation();

		//dx->SetStartIndexLocation(AggrObject.StartIndexLocation + indexCount);
		//dx->SetBaseVertexLocation(AggrObject.BaseVertexLocation + vertexCount);

		const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
		const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);
		
		geo.Name = meshName + L" Geo";

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
		CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
		CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		bool p = false;
		if (dx->IsCommandListClose())
		{
			dx->ResetCommandList();
			p = true;
		}

		geo.VertexBufferGPU = dx->CreateDefaultBuffer(dx->GetDevice(),
			dx->GetCommandList(), vertices.data(), vbByteSize, geo.VertexBufferUploader);

		geo.IndexBufferGPU = dx->CreateDefaultBuffer(dx->GetDevice(),
			dx->GetCommandList(), indices.data(), ibByteSize, geo.IndexBufferUploader);

		if (p)
			ThrowIfFailed(dx->GetCommandList()->Close());

		geo.vertexBufferView.BufferLocation = geo.VertexBufferGPU->GetGPUVirtualAddress();
		geo.vertexBufferView.StrideInBytes = sizeof(Vertex);
		geo.vertexBufferView.SizeInBytes = vbByteSize;
		geo.indexBufferView.BufferLocation = geo.IndexBufferGPU->GetGPUVirtualAddress();
		geo.indexBufferView.Format = indexBufferViewFormat;
		geo.indexBufferView.SizeInBytes = ibByteSize;

		dx->AddShapeGeometry(&geo);
	}
}

void MeshComponent::BuildRenderItems(D3DWindow* dx, UINT renderLayerIndex)
{
	if (m_dx != dx)
	{
		EngineHelpers::AddLog(L"[Engine] -> dx设备中途被更改！");
		return;
	}
	
	m_renderLayerIndex = renderLayerIndex;
	Obj.AggrObject = &AggrObject;

	dx->AddRenderItem(meshName, &Obj, m_renderLayerIndex);

	dx->UpdateFrameResources();
}

MeshComponent::MeshComponent()
{
	fileName = L"未设置";
	meshName = L"未设置";
	indexBufferViewFormat = DXGI_FORMAT_R32_UINT;
}

MeshComponent::~MeshComponent()
{
	if (diffuse_texture)
		diffuse_texture = nullptr;
}

void MeshComponent::UpdateMesh(ServicesContainer* ComponentServices, D3DWindow* dx, Transform WorldTransform, DirectX::XMFLOAT3 texTransform)
{
	RenderItem* ri = dx->GetRenderItems(meshName);
		
	XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	if (m_renderLayerIndex == 天空渲染项目)
	{
		XMStoreFloat4x4(&ri->WorldTransform, 
			XMMatrixScaling(8000.0f, 8000.0f, 8000.0f));
		XMStoreFloat4x4(&ri->TexTransform,
			XMMatrixScaling(1.0f, 1.0f, 1.0f));

	}
	else
	{
		// 将变化过程转为矩阵
		XMStoreFloat4x4(&ri->WorldTransform,
			XMMatrixAffineTransformation(
				XMLoadFloat3(&WorldTransform.scale),
				zero,
				XMQuaternionRotationRollPitchYaw(WorldTransform.rotation.x * MathHelps::Pi / 45.0f / 4.0f, WorldTransform.rotation.y * MathHelps::Pi / 45.0f / 4.0f, WorldTransform.rotation.z * MathHelps::Pi / 45.0f / 4.0f),
				XMLoadFloat3(&WorldTransform.position)));
		
		XMStoreFloat4x4(&ri->TexTransform,
			XMMatrixScaling(texTransform.x, texTransform.y, texTransform.z));
	}
	dx->FreshenObjectCBs();
	m_dx->FreshenMaterialCBs();
}

///////////////////////////////////////////////////////////////

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
	return (UINT)indices.size();
}

UINT MeshComponent::GetVertexCount()
{
	return (UINT)vertices.size();
}


void MeshComponent::CreateBoundingBox(ServicesContainer* ComponentServices)
{
	float min_x = 0.0f; float min_y = 0.0f; float min_z = 0.0f;
	float max_x = 0.0f; float max_y = 0.0f; float max_z = 0.0f;

	min_x = max_x = vertices.at(0).Pos.x;
	min_y = max_y = vertices.at(0).Pos.y;
	min_z = max_z = vertices.at(0).Pos.z;

	for (size_t i = 0; i < vertices.size(); i++)
	{
		// x-axis
		if (vertices.at(i).Pos.x < min_x)
			min_x = vertices.at(i).Pos.x;
		if (vertices.at(i).Pos.x > max_x)
			max_x = vertices.at(i).Pos.x;

		// y-axis
		if (vertices.at(i).Pos.y < min_y)
			min_y = vertices.at(i).Pos.y;
		if (vertices.at(i).Pos.y > max_y)
			max_y = vertices.at(i).Pos.y;

		// z-axis
		if (vertices.at(i).Pos.z < min_z)
			min_z = vertices.at(i).Pos.z;
		if (vertices.at(i).Pos.z > max_z)
			max_z = vertices.at(i).Pos.z;
	}

	TransformComponent* tmp = (TransformComponent*)ComponentServices->FindService(L"BoundingBox");
	if (tmp)
	{
		DirectX::BoundingBox boundingBox = DirectX::BoundingBox(DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
		DirectX::XMFLOAT3 min = DirectX::XMFLOAT3(min_x, min_y, min_z);
		DirectX::XMFLOAT3 max = DirectX::XMFLOAT3(max_x, max_y, max_z);
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
	m_dx->RemoveRenderItem(meshName, m_renderLayerIndex);
	m_dx->RemoveShapeGeometry(meshName);
}

void MeshComponent::BindingMaterial(D3DWindow* dx, std::wstring path)
{
	MaterialBuffer mat_buff;
	//assetsWindow->OpenMaterialFile(path, mat_buff);
	material_name = std::filesystem::path(path).stem().wstring();
	path = std::filesystem::path(path).parent_path().wstring(); /* remove filename */

	while (true)
	{
		std::size_t found = mat_buff.DiffusePath.find(L"..\\");
		if (found != std::string::npos)
		{
			mat_buff.DiffusePath.erase(0, 3);
			path = std::filesystem::path(path).parent_path().wstring();
		}
		else
		{
			std::size_t found = mat_buff.DiffusePath.find(L"\\"); /* dir? */
			if (found != std::string::npos)
			{
				std::wstring dir = mat_buff.DiffusePath;
				dir.resize(found);
				path.append(L"\\");
				path.append(dir);
				mat_buff.DiffusePath.erase(0, (found + 1));
			}
			else
			{
				break;
			}
		}
	}

	path.append(L"\\");
	path.append(mat_buff.DiffusePath);

	size_t pos = path.find_last_of(L".");
	if (pos != -1)
	{
		std::wstring buffer = path.substr(pos);

		//if (buffer == PNG)
		//{
		//	if (diffuse_texture) diffuse_texture->Release();
		//	DirectX::LoadWICTextureFromFile(
		//		dx->GetDevice(),
		//		path.c_str(),
		//		&diffuse_texture,
		//		nullptr);
		//}

		//if (buffer == DDS)
		//{
		//	if (diffuse_texture) diffuse_texture->Release();
		//	DirectX::LoadDDSTextureFromFile(
		//		dx->GetDevice(),
		//		path.c_str(),
		//		&diffuse_texture,
		//		nullptr);
		//}
	}
	else
	{
		EngineHelpers::AddLog(L"Error!");
	}
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
	m_dx->SetMaterial(meshName, name);
	m_dx->FreshenObjectCBs();
	m_dx->FreshenMaterialCBs();
}

std::wstring MeshComponent::GetMaterialName()
{
	return m_dx->GetMaterialName(meshName);
}
