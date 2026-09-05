#include "D3DWindowGeometry.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "Common/SkinningSharedTypes.h"

void D3DWindowGeometryProvider::AddShapeGeometry(D3DWindow* window)
{
	AssimpLoader assimpLoader;
	std::vector<Mesh> model = assimpLoader.LoadRawModel(L"DATA\\Models\\Sphere.obj");
	if (model.empty())
		return;

	window->AggrObject[L"shapeGeo"].IndexCount = static_cast<UINT>(model[0].indices32.size());
	window->AggrObject[L"shapeGeo"].StartIndexLocation = 0;
	window->AggrObject[L"shapeGeo"].BaseVertexLocation = 0;

	std::vector<Vertex> vertices(model[0].vertices.size());
	for (size_t i = 0; i < model[0].vertices.size(); ++i)
	{
		vertices[i].Pos = model[0].vertices[i].Pos;
		vertices[i].Color = model[0].vertices[i].Color;
		vertices[i].Normal = model[0].vertices[i].Normal;
		vertices[i].TexC = model[0].vertices[i].TexC;
		vertices[i].Tangent = model[0].vertices[i].Tangent;
		vertices[i].Bitangent = model[0].vertices[i].Bitangent;
	}

	std::vector<std::uint32_t> indices;
	indices.insert(indices.end(), std::begin(model[0].GetIndices16()), std::end(model[0].GetIndices16()));

	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
	const UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

	MeshGeometry geo;
	geo.Name = L"shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
	CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
	CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo.VertexBufferGPU = D3DWindow::CreateDefaultBuffer(window->d3dDevice.Get(),
		window->MainCommandList.Get(), vertices.data(), vbByteSize, geo.VertexBufferUploader);

	geo.IndexBufferGPU = D3DWindow::CreateDefaultBuffer(window->d3dDevice.Get(),
		window->MainCommandList.Get(), indices.data(), ibByteSize, geo.IndexBufferUploader);

	geo.vertexBufferView.BufferLocation = geo.VertexBufferGPU->GetGPUVirtualAddress();
	geo.vertexBufferView.StrideInBytes = sizeof(Vertex);
	geo.vertexBufferView.SizeInBytes = vbByteSize;
	geo.VertexByteStride = sizeof(Vertex);
	geo.indexBufferView.BufferLocation = geo.IndexBufferGPU->GetGPUVirtualAddress();
	geo.indexBufferView.Format = window->IndexBufferFormat;
	geo.indexBufferView.SizeInBytes = ibByteSize;

	window->Geometries[geo.Name] = geo;
}

void D3DWindowGeometryProvider::AddShapeGeometry(D3DWindow* window, MeshGeometry* geo)
{
	if (geo == nullptr || geo->Name.empty())
		return;

	auto existingIt = window->Geometries.find(geo->Name);
	if (existingIt != window->Geometries.end())
	{
		// 同名几何替换时保持 map 节点地址不变，避免 RenderItem::Geo 指针失效；
		// 旧 GPU 资源放到延迟释放列表，按 fence 延后释放，避免仍被已提交命令引用。
		window->DeferredReleaseGeometries.push_back({ std::move(existingIt->second), window->ComputeDeferredReleaseFence() });
		existingIt->second = *geo;
		return;
	}

	window->Geometries[geo->Name] = *geo;
}

void D3DWindowGeometryProvider::AddBillboardGeometry(D3DWindow* window)
{
	AggregateGraphicObj billboardAggrObject{};
	billboardAggrObject.IndexCount = 6;
	billboardAggrObject.StartIndexLocation = 0;
	billboardAggrObject.BaseVertexLocation = 0;
	window->AggrObject[D3DWindow::DefaultBillboardGeometryName] = billboardAggrObject;

	std::array<Vertex, 4> vertices =
	{
		Vertex{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		Vertex{ { -0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		Vertex{ {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		Vertex{ {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } }
	};
	const std::array<std::uint32_t, 6> indices = { 0u, 1u, 2u, 0u, 2u, 3u };

	MeshGeometry geo;
	geo.Name = D3DWindow::DefaultBillboardGeometryName;
	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
	const UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
	CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
	CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo.VertexBufferGPU = D3DWindow::CreateDefaultBuffer(
		window->d3dDevice.Get(),
		window->MainCommandList.Get(),
		vertices.data(),
		vbByteSize,
		geo.VertexBufferUploader);
	geo.IndexBufferGPU = D3DWindow::CreateDefaultBuffer(
		window->d3dDevice.Get(),
		window->MainCommandList.Get(),
		indices.data(),
		ibByteSize,
		geo.IndexBufferUploader);

	geo.vertexBufferView.BufferLocation = geo.VertexBufferGPU->GetGPUVirtualAddress();
	geo.vertexBufferView.StrideInBytes = sizeof(Vertex);
	geo.vertexBufferView.SizeInBytes = vbByteSize;
	geo.VertexByteStride = sizeof(Vertex);
	geo.indexBufferView.BufferLocation = geo.IndexBufferGPU->GetGPUVirtualAddress();
	geo.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	geo.indexBufferView.SizeInBytes = ibByteSize;

	D3DWindowGeometryProvider::AddShapeGeometry(window, &geo);
}

void D3DWindowGeometryProvider::AddTransformGizmoGeometry(D3DWindow* window)
{
	constexpr UINT sliceCount = 24u;

	const auto buildAndRegisterGeometry =
		[&](const std::wstring& geometryName,
			const std::vector<GizmoGpuVertex>& vertices,
			const std::vector<std::uint32_t>& indices,
			const std::vector<GizmoSubmeshDesc>& submeshes,
			GizmoPickMeshData* outPickMesh)
		{
			MeshGeometry geo;
			geo.Name = geometryName;
			const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(GizmoGpuVertex));
			const UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
			ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
			CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
			ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
			CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

			geo.VertexBufferGPU = D3DWindow::CreateDefaultBuffer(
				window->d3dDevice.Get(),
				window->MainCommandList.Get(),
				vertices.data(),
				vbByteSize,
				geo.VertexBufferUploader);
			geo.IndexBufferGPU = D3DWindow::CreateDefaultBuffer(
				window->d3dDevice.Get(),
				window->MainCommandList.Get(),
				indices.data(),
				ibByteSize,
				geo.IndexBufferUploader);

			geo.vertexBufferView.BufferLocation = geo.VertexBufferGPU->GetGPUVirtualAddress();
			geo.vertexBufferView.StrideInBytes = sizeof(GizmoGpuVertex);
			geo.vertexBufferView.SizeInBytes = vbByteSize;
			geo.VertexByteStride = sizeof(GizmoGpuVertex);
			geo.indexBufferView.BufferLocation = geo.IndexBufferGPU->GetGPUVirtualAddress();
			geo.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
			geo.indexBufferView.SizeInBytes = ibByteSize;

			AggregateGraphicObj gizmoAggrObject{};
			gizmoAggrObject.IndexCount = static_cast<UINT>(indices.size());
			gizmoAggrObject.StartIndexLocation = 0;
			gizmoAggrObject.BaseVertexLocation = 0;
			window->AggrObject[geometryName] = gizmoAggrObject;

			if (outPickMesh != nullptr)
			{
				outPickMesh->Vertices.clear();
				outPickMesh->Indices = indices;
				outPickMesh->Submeshes = submeshes;
				outPickMesh->Vertices.reserve(vertices.size());
				for (const GizmoGpuVertex& vertex : vertices)
				{
					GizmoVertex pickVertex{};
					pickVertex.Position = vertex.Position;
					pickVertex.Color = vertex.Color;
					pickVertex.HandleId = vertex.HandleId;
					outPickMesh->Vertices.push_back(pickVertex);
				}
			}

			D3DWindowGeometryProvider::AddShapeGeometry(window, &geo);
		};

	const auto appendCylinder =
		[&](std::vector<GizmoGpuVertex>& vertices,
			std::vector<std::uint32_t>& indices,
			GizmoHandle handle,
			const XMFLOAT3& axis,
			const XMFLOAT4& color,
			float startDistance,
			float endDistance,
			float radius)
		{
			XMVECTOR axisVec = XMVector3Normalize(XMLoadFloat3(&axis));
			XMVECTOR helper = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			if (std::abs(XMVectorGetX(XMVector3Dot(axisVec, helper))) > 0.95f)
				helper = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);

			const XMVECTOR basisU = XMVector3Normalize(XMVector3Cross(axisVec, helper));
			const XMVECTOR basisV = XMVector3Normalize(XMVector3Cross(axisVec, basisU));
			const std::uint32_t baseIndex = static_cast<std::uint32_t>(vertices.size());

			for (UINT i = 0; i < sliceCount; ++i)
			{
				const float angle = DirectX::XM_2PI * static_cast<float>(i) / static_cast<float>(sliceCount);
				const float c = std::cos(angle);
				const float s = std::sin(angle);
				const XMVECTOR radial = basisU * c + basisV * s;
				XMFLOAT3 startPos{};
				XMFLOAT3 endPos{};
				XMStoreFloat3(&startPos, axisVec * startDistance + radial * radius);
				XMStoreFloat3(&endPos, axisVec * endDistance + radial * radius);
				vertices.push_back({ startPos, color, static_cast<float>(static_cast<std::uint32_t>(handle)) });
				vertices.push_back({ endPos, color, static_cast<float>(static_cast<std::uint32_t>(handle)) });
			}

			for (UINT i = 0; i < sliceCount; ++i)
			{
				const UINT next = (i + 1u) % sliceCount;
				const std::uint32_t s0 = baseIndex + i * 2u;
				const std::uint32_t e0 = s0 + 1u;
				const std::uint32_t s1 = baseIndex + next * 2u;
				const std::uint32_t e1 = s1 + 1u;
				indices.push_back(s0); indices.push_back(e0); indices.push_back(s1);
				indices.push_back(e0); indices.push_back(e1); indices.push_back(s1);
			}
		};

	const auto appendCone =
		[&](std::vector<GizmoGpuVertex>& vertices,
			std::vector<std::uint32_t>& indices,
			GizmoHandle handle,
			const XMFLOAT3& axis,
			const XMFLOAT4& color,
			float baseDistance,
			float tipDistance,
			float baseRadius)
		{
			XMVECTOR axisVec = XMVector3Normalize(XMLoadFloat3(&axis));
			XMVECTOR helper = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			if (std::abs(XMVectorGetX(XMVector3Dot(axisVec, helper))) > 0.95f)
				helper = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);

			const XMVECTOR basisU = XMVector3Normalize(XMVector3Cross(axisVec, helper));
			const XMVECTOR basisV = XMVector3Normalize(XMVector3Cross(axisVec, basisU));
			const std::uint32_t baseIndex = static_cast<std::uint32_t>(vertices.size());

			XMFLOAT3 tipPos{};
			XMStoreFloat3(&tipPos, axisVec * tipDistance);
			vertices.push_back({ tipPos, color, static_cast<float>(static_cast<std::uint32_t>(handle)) });
			for (UINT i = 0; i < sliceCount; ++i)
			{
				const float angle = DirectX::XM_2PI * static_cast<float>(i) / static_cast<float>(sliceCount);
				const float c = std::cos(angle);
				const float s = std::sin(angle);
				const XMVECTOR radial = basisU * c + basisV * s;
				XMFLOAT3 ringPos{};
				XMStoreFloat3(&ringPos, axisVec * baseDistance + radial * baseRadius);
				vertices.push_back({ ringPos, color, static_cast<float>(static_cast<std::uint32_t>(handle)) });
			}

			for (UINT i = 0; i < sliceCount; ++i)
			{
				const UINT next = (i + 1u) % sliceCount;
				indices.push_back(baseIndex + 0u);
				indices.push_back(baseIndex + 1u + i);
				indices.push_back(baseIndex + 1u + next);
			}
		};

	const auto appendCube =
		[&](std::vector<GizmoGpuVertex>& vertices,
			std::vector<std::uint32_t>& indices,
			GizmoHandle handle,
			const XMFLOAT3& center,
			const XMFLOAT4& color,
			float halfExtent)
		{
			const std::uint32_t baseIndex = static_cast<std::uint32_t>(vertices.size());
			const float x = center.x;
			const float y = center.y;
			const float z = center.z;
			const float h = halfExtent;
			const XMFLOAT3 positions[8] =
			{
				{ x - h, y - h, z - h }, { x + h, y - h, z - h }, { x + h, y + h, z - h }, { x - h, y + h, z - h },
				{ x - h, y - h, z + h }, { x + h, y - h, z + h }, { x + h, y + h, z + h }, { x - h, y + h, z + h }
			};
			for (const XMFLOAT3& position : positions)
				vertices.push_back({ position, color, static_cast<float>(static_cast<std::uint32_t>(handle)) });
			const std::uint32_t localIndices[] =
			{
				0,1,2, 0,2,3, 4,6,5, 4,7,6,
				0,4,5, 0,5,1, 1,5,6, 1,6,2,
				2,6,7, 2,7,3, 3,7,4, 3,4,0
			};
			for (std::uint32_t index : localIndices)
				indices.push_back(baseIndex + index);
		};

	const auto appendRing =
		[&](std::vector<GizmoGpuVertex>& vertices,
			std::vector<std::uint32_t>& indices,
			GizmoHandle handle,
			const XMFLOAT3& axis,
			const XMFLOAT4& color,
			float radius,
			float thickness)
		{
			XMVECTOR axisVec = XMVector3Normalize(XMLoadFloat3(&axis));
			XMVECTOR helper = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			if (std::abs(XMVectorGetX(XMVector3Dot(axisVec, helper))) > 0.95f)
				helper = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
			const XMVECTOR basisU = XMVector3Normalize(XMVector3Cross(axisVec, helper));
			const XMVECTOR basisV = XMVector3Normalize(XMVector3Cross(axisVec, basisU));
			const std::uint32_t baseIndex = static_cast<std::uint32_t>(vertices.size());
			for (UINT i = 0; i < sliceCount; ++i)
			{
				const float angle = DirectX::XM_2PI * static_cast<float>(i) / static_cast<float>(sliceCount);
				const float c = std::cos(angle);
				const float s = std::sin(angle);
				const XMVECTOR radial = basisU * c + basisV * s;
				XMFLOAT3 outerPos{};
				XMFLOAT3 innerPos{};
				XMStoreFloat3(&outerPos, radial * (radius + thickness * 0.5f));
				XMStoreFloat3(&innerPos, radial * (radius - thickness * 0.5f));
				vertices.push_back({ outerPos, color, static_cast<float>(static_cast<std::uint32_t>(handle)) });
				vertices.push_back({ innerPos, color, static_cast<float>(static_cast<std::uint32_t>(handle)) });
			}
			for (UINT i = 0; i < sliceCount; ++i)
			{
				const UINT next = (i + 1u) % sliceCount;
				const std::uint32_t o0 = baseIndex + i * 2u;
				const std::uint32_t i0 = o0 + 1u;
				const std::uint32_t o1 = baseIndex + next * 2u;
				const std::uint32_t i1 = o1 + 1u;
				indices.push_back(o0); indices.push_back(i0); indices.push_back(o1);
				indices.push_back(i0); indices.push_back(i1); indices.push_back(o1);
			}
		};

	{
		std::vector<GizmoGpuVertex> vertices;
		std::vector<std::uint32_t> indices;
		std::vector<GizmoSubmeshDesc> submeshes;
		constexpr float shaftLength = 1.00f;
		constexpr float headLength = 0.28f;
		constexpr float shaftRadius = 0.035f;
		constexpr float headRadius = 0.085f;
		const auto appendAxis = [&](GizmoHandle handle, const XMFLOAT3& axis, const XMFLOAT4& color)
		{
			const std::uint32_t indexStart = static_cast<std::uint32_t>(indices.size());
			appendCylinder(vertices, indices, handle, axis, color, 0.0f, shaftLength, shaftRadius);
			appendCone(vertices, indices, handle, axis, color, shaftLength, shaftLength + headLength, headRadius);
			submeshes.push_back({ handle, indexStart, static_cast<std::uint32_t>(indices.size()) - indexStart, 0u });
		};
		appendAxis(GizmoHandle::AxisX, XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f));
		appendAxis(GizmoHandle::AxisY, XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT4(0.2f, 1.0f, 0.2f, 1.0f));
		appendAxis(GizmoHandle::AxisZ, XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT4(0.2f, 0.6f, 1.0f, 1.0f));
		buildAndRegisterGeometry(TranslateGizmoGeometryName, vertices, indices, submeshes, &window->TranslateGizmoPickMesh);
	}

	{
		std::vector<GizmoGpuVertex> vertices;
		std::vector<std::uint32_t> indices;
		std::vector<GizmoSubmeshDesc> submeshes;
		const auto appendScaleAxis = [&](GizmoHandle handle, const XMFLOAT3& axis, const XMFLOAT4& color)
		{
			const std::uint32_t indexStart = static_cast<std::uint32_t>(indices.size());
			appendCylinder(vertices, indices, handle, axis, color, 0.0f, 0.92f, 0.03f);
			XMFLOAT3 boxCenter = { axis.x * 1.10f, axis.y * 1.10f, axis.z * 1.10f };
			appendCube(vertices, indices, handle, boxCenter, color, 0.09f);
			submeshes.push_back({ handle, indexStart, static_cast<std::uint32_t>(indices.size()) - indexStart, 0u });
		};
		appendScaleAxis(GizmoHandle::AxisX, XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f));
		appendScaleAxis(GizmoHandle::AxisY, XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT4(0.2f, 1.0f, 0.2f, 1.0f));
		appendScaleAxis(GizmoHandle::AxisZ, XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT4(0.2f, 0.6f, 1.0f, 1.0f));
		buildAndRegisterGeometry(ScaleGizmoGeometryName, vertices, indices, submeshes, &window->ScaleGizmoPickMesh);
	}

	{
		std::vector<GizmoGpuVertex> vertices;
		std::vector<std::uint32_t> indices;
		std::vector<GizmoSubmeshDesc> submeshes;
		const auto appendRotateAxis = [&](GizmoHandle handle, const XMFLOAT3& axis, const XMFLOAT4& color)
		{
			const std::uint32_t indexStart = static_cast<std::uint32_t>(indices.size());
			appendRing(vertices, indices, handle, axis, color, 1.15f, 0.08f);
			submeshes.push_back({ handle, indexStart, static_cast<std::uint32_t>(indices.size()) - indexStart, 0u });
		};
		appendRotateAxis(GizmoHandle::AxisX, XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f));
		appendRotateAxis(GizmoHandle::AxisY, XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT4(0.2f, 1.0f, 0.2f, 1.0f));
		appendRotateAxis(GizmoHandle::AxisZ, XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT4(0.2f, 0.6f, 1.0f, 1.0f));
		buildAndRegisterGeometry(RotateGizmoGeometryName, vertices, indices, submeshes, &window->RotateGizmoPickMesh);
	}
}

void D3DWindowGeometryProvider::RemoveShapeGeometry(D3DWindow* window, std::wstring name)
{
	auto geometryIt = window->Geometries.find(name);
	if (geometryIt == window->Geometries.end())
		return;

	// 共享几何仍被其他渲染项引用时，不允许移除。
	for (const auto& renderItemPair : window->AllRitems)
	{
		const RenderItem& renderItem = renderItemPair.second;
		if (renderItem.Geo == nullptr)
			continue;
		if (renderItem.Geo == &geometryIt->second)
			return;
	}

	// 不要立刻释放 GPU 资源。
	// 命令列表/FrameResource 可能仍在引用它们；先把几何从活跃表里移除，
	// 到达安全 fence 后再释放。
	window->DeferredReleaseGeometries.push_back({ std::move(geometryIt->second), window->ComputeDeferredReleaseFence() });
	window->Geometries.erase(geometryIt);
}

bool D3DWindowGeometryProvider::HasShapeGeometry(const D3DWindow* window, const std::wstring& name)
{
	return window->Geometries.find(name) != window->Geometries.end();
}

bool D3DWindowGeometryProvider::SetGeometryVertexColor(
	D3DWindow* window,
	const std::wstring& name,
	const DirectX::XMFLOAT4& color)
{
	auto geometryIt = window->Geometries.find(name);
	if (geometryIt == window->Geometries.end())
		return false;

	MeshGeometry& geometry = geometryIt->second;
	if (geometry.VertexBufferCPU == nullptr ||
		geometry.vertexBufferView.SizeInBytes == 0 ||
		geometry.VertexByteStride == 0)
	{
		return false;
	}

	const UINT vertexCount = geometry.vertexBufferView.SizeInBytes / geometry.VertexByteStride;
	if (vertexCount == 0 ||
		vertexCount * geometry.VertexByteStride != geometry.vertexBufferView.SizeInBytes)
	{
		return false;
	}

	if (geometry.VertexByteStride == sizeof(Vertex))
	{
		Vertex* vertices = static_cast<Vertex*>(geometry.VertexBufferCPU->GetBufferPointer());
		for (UINT vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
			vertices[vertexIndex].Color = color;
	}
	else if (geometry.VertexByteStride == sizeof(Witchcraft::Animation::SkinnedVertex))
	{
		Witchcraft::Animation::SkinnedVertex* vertices =
			static_cast<Witchcraft::Animation::SkinnedVertex*>(geometry.VertexBufferCPU->GetBufferPointer());
		for (UINT vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
			vertices[vertexIndex].StaticVertex.Color = color;
	}
	else
	{
		return false;
	}

	ComPtr<ID3D12CommandAllocator> uploadCommandAllocator = nullptr;
	ComPtr<ID3D12GraphicsCommandList> uploadCommandList = nullptr;
	ID3D12Device* device = window->GetDevice();
	if (device == nullptr)
		return false;

	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(uploadCommandAllocator.GetAddressOf())));
	ThrowIfFailed(device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		uploadCommandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(uploadCommandList.GetAddressOf())));

	ComPtr<ID3D12Resource> newVertexUploader = nullptr;
	ComPtr<ID3D12Resource> newVertexBuffer = D3DWindow::CreateDefaultBuffer(
		device,
		uploadCommandList.Get(),
		geometry.VertexBufferCPU->GetBufferPointer(),
		geometry.vertexBufferView.SizeInBytes,
		newVertexUploader);
	if (newVertexBuffer == nullptr)
		return false;

	ThrowIfFailed(uploadCommandList->Close());
	ID3D12CommandList* uploadCommandLists[] = { uploadCommandList.Get() };
	window->GetCommandQueue()->ExecuteCommandLists(_countof(uploadCommandLists), uploadCommandLists);
	window->FlushCommandQueue();

	geometry.VertexBufferGPU = newVertexBuffer;
	geometry.VertexBufferUploader = newVertexUploader;
	geometry.vertexBufferView.BufferLocation = geometry.VertexBufferGPU->GetGPUVirtualAddress();
	geometry.vertexBufferView.StrideInBytes = geometry.VertexByteStride;
	geometry.vertexBufferView.SizeInBytes = vertexCount * geometry.VertexByteStride;
	return true;
}
