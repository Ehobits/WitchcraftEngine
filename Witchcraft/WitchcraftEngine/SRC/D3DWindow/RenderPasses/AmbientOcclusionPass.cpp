#include "AmbientOcclusionPass.h"
#include <algorithm>
#include <array>

AmbientOcclusionPass::AmbientOcclusionPass()
{
}

AmbientOcclusionPass::~AmbientOcclusionPass()
{
}

UINT AmbientOcclusionPass::ComputeScaledDimension(UINT sourceDimension)
{
	const float scaled = std::ceil(static_cast<float>(sourceDimension) * ResolutionScale);
	return (std::max)(1u, static_cast<UINT>(scaled));
}

void AmbientOcclusionPass::Initialize(ID3D12Device* device)
{
	md3dDevice = device;
}

void AmbientOcclusionPass::OnResize(UINT newWidth, UINT newHeight)
{
	mRenderWidth = ComputeScaledDimension(newWidth);
	mRenderHeight = ComputeScaledDimension(newHeight);

	m_Viewport = CD3DX12_VIEWPORT{ 0.0f, 0.0f,
		static_cast<float>(mRenderWidth),
		static_cast<float>(mRenderHeight),
		0.0f,1.0f };
	m_Rect = CD3DX12_RECT{ 0, 0,
		static_cast<LONG>(mRenderWidth),
		static_cast<LONG>(mRenderHeight) };

	BuildResources();
}

void AmbientOcclusionPass::CreateRootSignature()
{
	{
		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] =
		{
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0}
		};

		CD3DX12_ROOT_PARAMETER1 slotRootParameter[3];
		slotRootParameter[0].InitAsConstantBufferView(0);
		slotRootParameter[1].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[2].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);

		const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
			0,
			D3D12_FILTER_MIN_MAG_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
			1,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
			2,
			D3D12_FILTER_MIN_MAG_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			0.0f,
			0,
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

		const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
			3,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP);

		std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
		{
			pointClamp, linearClamp, depthMapSam, linearWrap
		};

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
		rootSigDesc.Init_1_1(
			_countof(slotRootParameter),
			slotRootParameter,
			(UINT)staticSamplers.size(),
			staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3DX12SerializeVersionedRootSignature(
			&rootSigDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		ThrowIfFailed(hr);

		ThrowIfFailed(md3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mSsaoRootSignature.GetAddressOf())));
	}

	{
		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] =
		{
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0}
		};

		CD3DX12_ROOT_PARAMETER1 slotRootParameter[4];
		slotRootParameter[0].InitAsConstantBufferView(0);
		slotRootParameter[1].InitAsConstants(1, 1);
		slotRootParameter[2].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[3].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);

		const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
			0,
			D3D12_FILTER_MIN_MAG_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
			1,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
			2,
			D3D12_FILTER_MIN_MAG_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			0.0f,
			0,
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

		const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
			3,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP);

		std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
		{
			pointClamp, linearClamp, depthMapSam, linearWrap
		};

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
		rootSigDesc.Init_1_1(
			_countof(slotRootParameter),
			slotRootParameter,
			(UINT)staticSamplers.size(),
			staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3DX12SerializeVersionedRootSignature(
			&rootSigDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		ThrowIfFailed(hr);

		ThrowIfFailed(md3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mBlurRootSignature.GetAddressOf())));
	}
}

void AmbientOcclusionPass::CreatePipesAndShaders(std::vector<ComPtr<ID3DBlob>>& vertexShader, std::vector<ComPtr<ID3DBlob>>& pixelShader, D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc, std::vector<ComPtr<ID3D12PipelineState>>& PipelineState)
{
	vertexShader[0] = CompileShader(L"DATA/Shaders/DrawNormals", nullptr, "VS", "vs_5_1");
	pixelShader[0] = CompileShader(L"DATA/Shaders/DrawNormals", nullptr, "PS", "ps_5_1");

	vertexShader[1] = CompileShader(L"DATA/Shaders/Ssao", nullptr, "VS", "vs_5_1");
	pixelShader[1] = CompileShader(L"DATA/Shaders/Ssao", nullptr, "PS", "ps_5_1");

	vertexShader[2] = CompileShader(L"DATA/Shaders/SsaoBlur", nullptr, "VS", "vs_5_1");
	pixelShader[2] = CompileShader(L"DATA/Shaders/SsaoBlur", nullptr, "PS", "ps_5_1");

	std::vector<D3D12_INPUT_ELEMENT_DESC> InputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 60, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC drawNormalsPsoDesc = basePsoDesc;
	drawNormalsPsoDesc.InputLayout = { InputLayout.data(), static_cast<UINT>(InputLayout.size()) };
	drawNormalsPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[0].Get());
	drawNormalsPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[0].Get());
	drawNormalsPsoDesc.RTVFormats[0] = NormalMapFormat;
	drawNormalsPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	drawNormalsPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	drawNormalsPsoDesc.SampleDesc.Count = 1;
	drawNormalsPsoDesc.SampleDesc.Quality = 0;
	drawNormalsPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawNormalsPsoDesc,
		IID_PPV_ARGS(&PipelineState[0])));
	SetD3DObjectName(PipelineState[0].Get(), L"管线_法线预通道_AO子系统");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC aoPsoDesc = basePsoDesc;
	aoPsoDesc.InputLayout = { nullptr, 0 };
	aoPsoDesc.pRootSignature = mSsaoRootSignature.Get();
	aoPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[1].Get());
	aoPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[1].Get());
	aoPsoDesc.RTVFormats[0] = AmbientMapFormat;
	aoPsoDesc.DepthStencilState.DepthEnable = false;
	aoPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	aoPsoDesc.SampleDesc.Count = 1;
	aoPsoDesc.SampleDesc.Quality = 0;
	aoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&aoPsoDesc,
		IID_PPV_ARGS(&PipelineState[1])));
	SetD3DObjectName(PipelineState[1].Get(), L"管线_环境遮蔽AO_全屏");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC blurPsoDesc = aoPsoDesc;
	blurPsoDesc.pRootSignature = mBlurRootSignature.Get();
	blurPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[2].Get());
	blurPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[2].Get());
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&blurPsoDesc,
		IID_PPV_ARGS(&PipelineState[2])));
	SetD3DObjectName(PipelineState[2].Get(), L"管线_环境遮蔽AO_模糊");
}

void AmbientOcclusionPass::BuildResources()
{
	mAmbientMap0 = nullptr;
	mAmbientMap1 = nullptr;

	D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = static_cast<UINT64>(mRenderWidth);
	texDesc.Height = mRenderHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = NormalMapFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	texDesc.Format = AmbientMapFormat;
	float ambientClearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	CD3DX12_CLEAR_VALUE optClear(AmbientMapFormat, ambientClearColor);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		&optClear,
		IID_PPV_ARGS(&mAmbientMap0)));
	mAmbientMap0State = D3D12_RESOURCE_STATE_GENERIC_READ;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		&optClear,
		IID_PPV_ARGS(&mAmbientMap1)));
	mAmbientMap1State = D3D12_RESOURCE_STATE_GENERIC_READ;

	BuildDescriptors();
}

void AmbientOcclusionPass::BuildRandomVectorTexture(ID3D12GraphicsCommandList* cmdList)
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = 256;
	texDesc.Height = 256;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mRandomVectorMap)));

	const UINT num2DSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mRandomVectorMap.Get(), 0, num2DSubresources);

	HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&Desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(mRandomVectorMapUploadBuffer.GetAddressOf())));

	XMCOLOR initData[256 * 256];
	for (int i = 0; i < 256; ++i)
	{
		for (int j = 0; j < 256; ++j)
		{
			XMFLOAT3 v(MathHelps::RandF(), MathHelps::RandF(), MathHelps::RandF());
			initData[i * 256 + j] = XMCOLOR(v.x, v.y, v.z, 0.0f);
		}
	}

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = 256 * sizeof(XMCOLOR);
	subResourceData.SlicePitch = subResourceData.RowPitch * 256;

	D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(mRandomVectorMap.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &Barriers);
	UpdateSubresources(cmdList, mRandomVectorMap.Get(), mRandomVectorMapUploadBuffer.Get(),
		0, 0, num2DSubresources, &subResourceData);
	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(mRandomVectorMap.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &Barriers);
}

void AmbientOcclusionPass::BuildOffsetVectors()
{
	mOffsets[0] = XMFLOAT4(+1.0f, +1.0f, +1.0f, 0.0f);
	mOffsets[1] = XMFLOAT4(-1.0f, -1.0f, -1.0f, 0.0f);
	mOffsets[2] = XMFLOAT4(-1.0f, +1.0f, +1.0f, 0.0f);
	mOffsets[3] = XMFLOAT4(+1.0f, -1.0f, -1.0f, 0.0f);
	mOffsets[4] = XMFLOAT4(+1.0f, +1.0f, -1.0f, 0.0f);
	mOffsets[5] = XMFLOAT4(-1.0f, -1.0f, +1.0f, 0.0f);
	mOffsets[6] = XMFLOAT4(-1.0f, +1.0f, -1.0f, 0.0f);
	mOffsets[7] = XMFLOAT4(+1.0f, -1.0f, +1.0f, 0.0f);
	mOffsets[8] = XMFLOAT4(-1.0f, 0.0f, 0.0f, 0.0f);
	mOffsets[9] = XMFLOAT4(+1.0f, 0.0f, 0.0f, 0.0f);
	mOffsets[10] = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);
	mOffsets[11] = XMFLOAT4(0.0f, +1.0f, 0.0f, 0.0f);
	mOffsets[12] = XMFLOAT4(0.0f, 0.0f, -1.0f, 0.0f);
	mOffsets[13] = XMFLOAT4(0.0f, 0.0f, +1.0f, 0.0f);

	for (UINT i = 0; i < SsaoSampleCount; ++i)
	{
		const float scale = MathHelps::RandF(0.25f, 1.0f);
		XMVECTOR v = scale * XMVector4Normalize(XMLoadFloat4(&mOffsets[i]));
		XMStoreFloat4(&mOffsets[i], v);
	}
}

void AmbientOcclusionPass::BuildDescriptors(
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
	UINT cbvSrvUavDescriptorSize,
	UINT rtvDescriptorSize)
{
	mhAmbientMap0CpuSrv = hCpuSrv;
	mhAmbientMap1CpuSrv = hCpuSrv;
	mhAmbientMap1CpuSrv.Offset(1, cbvSrvUavDescriptorSize);
	mhRandomVectorMapCpuSrv = hCpuSrv;
	mhRandomVectorMapCpuSrv.Offset(2, cbvSrvUavDescriptorSize);

	mhAmbientMap0GpuSrv = hGpuSrv;
	mhAmbientMap1GpuSrv = hGpuSrv;
	mhAmbientMap1GpuSrv.Offset(1, cbvSrvUavDescriptorSize);
	mhRandomVectorMapGpuSrv = hGpuSrv;
	mhRandomVectorMapGpuSrv.Offset(2, cbvSrvUavDescriptorSize);

	mhAmbientMap0CpuRtv = hCpuRtv;
	mhAmbientMap1CpuRtv = hCpuRtv;
	mhAmbientMap1CpuRtv.Offset(1, rtvDescriptorSize);

	m_setHandles = true;
	BuildDescriptors();
}

void AmbientOcclusionPass::BuildDescriptors()
{
	if (!m_setHandles)
		return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	md3dDevice->CreateShaderResourceView(mRandomVectorMap.Get(), &srvDesc, mhRandomVectorMapCpuSrv);

	srvDesc.Format = AmbientMapFormat;
	md3dDevice->CreateShaderResourceView(mAmbientMap0.Get(), &srvDesc, mhAmbientMap0CpuSrv);
	md3dDevice->CreateShaderResourceView(mAmbientMap1.Get(), &srvDesc, mhAmbientMap1CpuSrv);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = AmbientMapFormat;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	md3dDevice->CreateRenderTargetView(mAmbientMap0.Get(), &rtvDesc, mhAmbientMap0CpuRtv);
	md3dDevice->CreateRenderTargetView(mAmbientMap1.Get(), &rtvDesc, mhAmbientMap1CpuRtv);
	mAmbientMap0->SetName(L"AmbientOcclusionMap0");
	mAmbientMap1->SetName(L"AmbientOcclusionMap1");
}

ComPtr<ID3D12Resource> AmbientOcclusionPass::AmbientMap()
{
	return mAmbientMap0;
}

AOConstants AmbientOcclusionPass::BuildConstants(
	const DirectX::XMMATRIX& projMatrix,
	const DirectX::XMFLOAT4X4& proj,
	const DirectX::XMFLOAT4X4& invProj,
	const DirectX::XMFLOAT4X4& invView,
	float renderTargetWidth,
	float renderTargetHeight,
	float blurSigma,
	float radius,
	float fadeStart,
	float fadeEnd,
	float surfaceEpsilon) const
{
	AOConstants aoConstants;

	const XMMATRIX texTransform(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	aoConstants.Proj = proj;
	aoConstants.InvProj = invProj;
	aoConstants.InvView = invView;
	XMStoreFloat4x4(&aoConstants.ProjTex, XMMatrixTranspose(projMatrix * texTransform));

	GetOffsetVectors(aoConstants.OffsetVectors);

	DirectX::XMFLOAT4X4 projRaw{};
	XMStoreFloat4x4(&projRaw, projMatrix);
	aoConstants.ProjScaleX = projRaw._11;
	aoConstants.ProjScaleY = projRaw._22;
	aoConstants.ProjDepthA = projRaw._33;
	aoConstants.ProjDepthB = projRaw._43;

	const float sigma = std::clamp(blurSigma, 0.1f, 2.5f);
	constexpr int MaxBlurRadius = 5;
	const float twoSigma2 = 2.0f * sigma * sigma;
	std::array<float, 12> blurWeights{};
	float weightSum = 0.0f;

	for (int i = -MaxBlurRadius; i <= MaxBlurRadius; ++i)
	{
		const float x = static_cast<float>(i);
		blurWeights[i + MaxBlurRadius] = expf(-x * x / twoSigma2);
		weightSum += blurWeights[i + MaxBlurRadius];
	}

	for (int i = 0; i <= 2 * MaxBlurRadius; ++i)
		blurWeights[i] /= weightSum;

	aoConstants.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
	aoConstants.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
	aoConstants.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

	const float safeWidth = (std::max)(renderTargetWidth, 1.0f);
	const float safeHeight = (std::max)(renderTargetHeight, 1.0f);
	aoConstants.RenderTargetSize = XMFLOAT2(safeWidth, safeHeight);
	aoConstants.OcclusionRadius = std::clamp(radius, 0.0f, 4.0f);
	aoConstants.OcclusionFadeStart = std::clamp(fadeStart, 0.0f, 100.0f);
	aoConstants.OcclusionFadeEnd = (std::max)(aoConstants.OcclusionFadeStart + 0.001f, fadeEnd);
	aoConstants.SurfaceEpsilon = std::clamp(surfaceEpsilon, 0.0001f, 1.0f);

	return aoConstants;
}

void AmbientOcclusionPass::ClearAmbientMapsToNeutral(ID3D12GraphicsCommandList* cmdList)
{
	const float neutralAoClearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	TransitionAmbientMap0(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
	TransitionAmbientMap1(cmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->ClearRenderTargetView(mhAmbientMap0CpuRtv, neutralAoClearColor, 0, nullptr);
	cmdList->ClearRenderTargetView(mhAmbientMap1CpuRtv, neutralAoClearColor, 0, nullptr);
	TransitionAmbientMap0(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
	TransitionAmbientMap1(cmdList, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void AmbientOcclusionPass::RecordSsaoPasses(
	const D3DPassContext& context,
	ID3D12PipelineState* ssaoPipelineState,
	ID3D12PipelineState* blurPipelineState)
{
	if (context.CommandList == nullptr || context.DescriptorHeaps == nullptr)
		return;
	if (context.DescriptorHeapCount == 0 || context.AoCBAddress == 0 || context.NormalDepthDescriptor.ptr == 0)
		return;
	if (ssaoPipelineState == nullptr || blurPipelineState == nullptr)
		return;

	const float neutralAoClearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };

	SetViewports(context.CommandList);
	TransitionAmbientMap0(context.CommandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
	context.CommandList->ClearRenderTargetView(mhAmbientMap0CpuRtv, neutralAoClearColor, 0, nullptr);
	context.CommandList->OMSetRenderTargets(1, &mhAmbientMap0CpuRtv, true, nullptr);
	context.CommandList->SetGraphicsRootSignature(mSsaoRootSignature.Get());
	context.CommandList->SetDescriptorHeaps(context.DescriptorHeapCount, context.DescriptorHeaps);
	context.CommandList->SetGraphicsRootConstantBufferView(0, context.AoCBAddress);
	context.CommandList->SetGraphicsRootDescriptorTable(1, context.NormalDepthDescriptor);
	context.CommandList->SetGraphicsRootDescriptorTable(2, mhRandomVectorMapGpuSrv);
	context.CommandList->SetPipelineState(ssaoPipelineState);
	context.CommandList->IASetVertexBuffers(0, 0, nullptr);
	context.CommandList->IASetIndexBuffer(nullptr);
	context.CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.CommandList->DrawInstanced(6, 1, 0, 0);
	TransitionAmbientMap0(context.CommandList, D3D12_RESOURCE_STATE_GENERIC_READ);

	BlurAmbientMap(context.CommandList, blurPipelineState, true, context.AoCBAddress, context.NormalDepthDescriptor);
	BlurAmbientMap(context.CommandList, blurPipelineState, false, context.AoCBAddress, context.NormalDepthDescriptor);
}

void AmbientOcclusionPass::SetViewports(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->RSSetViewports(1, &m_Viewport);
	cmdList->RSSetScissorRects(1, &m_Rect);
}

void AmbientOcclusionPass::SetRenderTargets(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->OMSetRenderTargets(1, &mhAmbientMap0CpuRtv, true, nullptr);
}

void AmbientOcclusionPass::TransitionAmbientMap0(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState)
{
	TransitionTrackedResourceState(cmdList, mAmbientMap0.Get(), mAmbientMap0State, targetState);
}

void AmbientOcclusionPass::TransitionAmbientMap1(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES targetState)
{
	TransitionTrackedResourceState(cmdList, mAmbientMap1.Get(), mAmbientMap1State, targetState);
}

void AmbientOcclusionPass::BlurAmbientMap(
	ID3D12GraphicsCommandList* cmdList,
	ID3D12PipelineState* blurPipelineState,
	bool horzBlur,
	D3D12_GPU_VIRTUAL_ADDRESS aoCBAddress,
	D3D12_GPU_DESCRIPTOR_HANDLE normalDepthSrvHandle)
{
	ID3D12Resource* input = horzBlur ? mAmbientMap0.Get() : mAmbientMap1.Get();
	ID3D12Resource* output = horzBlur ? mAmbientMap1.Get() : mAmbientMap0.Get();
	CD3DX12_GPU_DESCRIPTOR_HANDLE inputSrv = horzBlur ? mhAmbientMap0GpuSrv : mhAmbientMap1GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE outputRtv = horzBlur ? mhAmbientMap1CpuRtv : mhAmbientMap0CpuRtv;
	D3D12_RESOURCE_STATES* inputState = horzBlur ? &mAmbientMap0State : &mAmbientMap1State;
	D3D12_RESOURCE_STATES* outputState = horzBlur ? &mAmbientMap1State : &mAmbientMap0State;

	if (inputState != nullptr)
		TransitionTrackedResourceState(cmdList, input, *inputState, D3D12_RESOURCE_STATE_GENERIC_READ);
	if (outputState != nullptr)
		TransitionTrackedResourceState(cmdList, output, *outputState, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	cmdList->ClearRenderTargetView(outputRtv, clearColor, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &outputRtv, true, nullptr);
	cmdList->SetPipelineState(blurPipelineState);
	cmdList->SetGraphicsRootSignature(mBlurRootSignature.Get());
	cmdList->SetGraphicsRootConstantBufferView(0, aoCBAddress);
	cmdList->SetGraphicsRoot32BitConstant(1, horzBlur ? 1u : 0u, 0);
	cmdList->SetGraphicsRootDescriptorTable(2, normalDepthSrvHandle);
	cmdList->SetGraphicsRootDescriptorTable(3, inputSrv);
	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, 1, 0, 0);
	cmdList->OMSetRenderTargets(0, nullptr, false, nullptr);

	if (outputState != nullptr)
		TransitionTrackedResourceState(cmdList, output, *outputState, D3D12_RESOURCE_STATE_GENERIC_READ);
}

void AmbientOcclusionPass::GetOffsetVectors(DirectX::XMFLOAT4 offsets[SsaoSampleCount]) const
{
	std::copy(&mOffsets[0], &mOffsets[SsaoSampleCount], &offsets[0]);
}
