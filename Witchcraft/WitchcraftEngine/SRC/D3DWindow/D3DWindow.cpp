#include "D3DWindow.h"
#include "HELPERS/Helpers.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "ECS/WitchcraECS.h"
#include "ECS/COMPONENT/MeshComponent.h"
#include "System/WitchcraftFile/WMaterialFile.h"
#include "../String/SStringUtils.h"
#include "Editor/Editor.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>

bool D3DWindow::BuildEntityRenderTransforms(SceneEntityBase* entity, WitchcraECS* ecs, DirectX::XMFLOAT4X4* outWorldTransform, DirectX::XMFLOAT4X4* outTexTransform)
{
	if (entity == nullptr || ecs == nullptr || outWorldTransform == nullptr || outTexTransform == nullptr)
		return false;

	*outWorldTransform = MathHelps::Identity;
	*outTexTransform = MathHelps::Identity;

	EntityRenderView renderView;
	if (!ecs->BuildEntityRenderView(entity, &renderView) || renderView.meshComponent == nullptr)
		return false;

	if (renderView.renderLayerIndex == 天空渲染项目)
	{
		Transform skyTransform{};
		if (ecs->GetEntityRenderTransform(entity, &skyTransform))
			BuildSkyRenderTransforms(&skyTransform, outWorldTransform, outTexTransform);
		else
			BuildSkyRenderTransforms(nullptr, outWorldTransform, outTexTransform);

		return true;
	}

	BuildStandardEntityRenderTransforms(entity, ecs, outWorldTransform, outTexTransform);
	return true;
}

void D3DWindow::BuildStandardEntityRenderTransforms(SceneEntityBase* entity, WitchcraECS* ecs, DirectX::XMFLOAT4X4* outWorldTransform, DirectX::XMFLOAT4X4* outTexTransform)
{
	DirectX::XMStoreFloat4x4(outTexTransform, DirectX::XMMatrixScaling(1.0f, 1.0f, 1.0f));

	DirectX::XMFLOAT4X4 renderMatrix = MathHelps::Identity;
	if (ecs->GetEntityRenderMatrix(entity, &renderMatrix))
	{
		*outWorldTransform = renderMatrix;
		return;
	}

	Transform localTransform{};
	if (ecs->GetEntityEditableLocalTransform(entity, &localTransform))
		*outWorldTransform = ::BuildWorldMatrixFromTransformData(localTransform);
}

TextureType D3DWindow::ResolveTextureTypeFromPath(const std::wstring& path)
{
	std::wstring extension = std::filesystem::path(path).extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
	return extension == L".dds" ? TextureType::DDS : TextureType::PNG;
}

std::wstring D3DWindow::MakeUniqueName(const std::unordered_map<std::wstring, Material>& materials, const std::wstring& baseName)
{
	if (materials.find(baseName) == materials.end())
		return baseName;

	UINT suffix = 1;
	while (true)
	{
		std::wstring candidate = baseName + L"_" + std::to_wstring(suffix);
		if (materials.find(candidate) == materials.end())
			return candidate;
		++suffix;
	}
}

std::wstring D3DWindow::NormalizeAssetPath(const std::wstring& path)
{
	std::filesystem::path normalized(path);
	normalized.make_preferred();
	return normalized.wstring();
}

std::vector<RenderItem*> D3DWindow::CollectRenderItems(const std::map<std::wstring, RenderItem*>& renderItemMap)
{
	std::vector<RenderItem*> renderItems;
	renderItems.reserve(renderItemMap.size());
	for (const auto& pair : renderItemMap)
	{
		if (pair.second != nullptr)
			renderItems.push_back(pair.second);
	}

	return renderItems;
}

ImportedTextureSource D3DWindow::BuildImportedTextureSourceFromMaterialFile(
	const std::filesystem::path& materialFilePath,
	const std::wstring& textureName)
{
	ImportedTextureSource source;
	if (textureName.empty())
		return source;

	std::filesystem::path texturePath(textureName);
	if (texturePath.is_relative())
	{
		if (!texturePath.has_parent_path())
			texturePath = materialFilePath.parent_path().parent_path() / L"Textures" / texturePath;
		else
			texturePath = materialFilePath.parent_path() / texturePath;
	}

	source.Path = texturePath.lexically_normal().wstring();
	source.AssetName = std::filesystem::path(textureName).filename().wstring();
	return source;
}

ImportedMaterialInfo D3DWindow::ConvertMaterialFileDataToImportedInfo(
	const WMaterialFileData& materialData,
	const std::filesystem::path& materialFilePath)
{
	ImportedMaterialInfo info;
	info.Name = materialData.MaterialName.empty() ? materialFilePath.stem().wstring() : materialData.MaterialName;
	info.DiffuseColor = materialData.DiffuseColor;
	info.Emissive = materialData.Emissive;
	info.Metallic = materialData.Metallic;
	info.Roughness = materialData.Roughness;
	info.Opacity = materialData.Opacity;
	info.DiffuseColor.w = materialData.Opacity;
	info.DiffuseTexture = BuildImportedTextureSourceFromMaterialFile(materialFilePath, materialData.DiffuseTexture);
	info.NormalTexture = BuildImportedTextureSourceFromMaterialFile(materialFilePath, materialData.NormalTexture);
	info.MetallicTexture = BuildImportedTextureSourceFromMaterialFile(materialFilePath, materialData.MetallicTexture);
	info.RoughnessTexture = BuildImportedTextureSourceFromMaterialFile(materialFilePath, materialData.RoughnessTexture);
	return info;
}

FrameResource::FrameResource()
{

}

FrameResource::~FrameResource()
{

}

void FrameResource::Create(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	if(materialCount > 0)
		MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
	if (objectCount > 0)
		ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
	LightCB = std::make_unique<UploadBuffer<LightConstants>>(device, 1, true);
	AOCB = std::make_unique<UploadBuffer<AOConstants>>(device, 1, true);
}

// 单例对象，以便工作线程可以共享成员。
static D3DWindow* s_app;

static HANDLE CreateAutoResetEventHandle()
{
	return CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

static HANDLE CreateManualResetEventHandle()
{
	return CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

D3DWindow::D3DWindow()
{
	s_app = this;

	BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; //DXGI_FORMAT_R16G16B16A16_FLOAT;// 
	DepthStencilFormat = DXGI_FORMAT_D32_FLOAT;
	MainPassCB.ShadowSettings = { ShadowConfig.DefaultOpacity, ShadowConfig.DefaultSoftness };
}

D3DWindow::~D3DWindow()
{
	if (d3dDevice != nullptr)
		FlushCommandQueue();

	// 关闭线程事件和线程句柄。
	for (int i = 0; i < NumContexts; i++)
	{
		for (UINT passIndex = 0; passIndex < 工作阶段计数; ++passIndex)
		{
			CloseHandle(workerBeginRecordCommand[passIndex][i]);
		}
		CloseHandle(workerFinishedRecordCommand[i]);
		CloseHandle(threadHandles[i]);
	}

	if (fenceEvent != nullptr)
	{
		CloseHandle(fenceEvent);
		fenceEvent = nullptr;
	}

	s_app = nullptr;
}

bool D3DWindow::Create(HWND hWnd, Timer* timer, Editor* editor)
{
	m_hwnd = hWnd;
	mTimer = timer;
	mEditor = editor;

	// 先建立设备、交换链与描述符堆，后续资源创建都依赖这些基础对象。
	CreateDevice();
	CreateCommandQueueAndSwapChain();
	CreateDescriptorHeaps();

	// 从关闭状态开始。 
	// 这是因为第一次引用命令列表时，我们将其重置，并且需要在调用Reset之前将其关闭。
	CloseCommandList();

	//进行初始大小调整代码。
	ambientOcclusion.Create(d3dDevice.Get(), MainCommandList.Get(), Width, Height);
	OnResize();
	shadowMap.Create(d3dDevice.Get(), MainCommandList.Get(), ShadowConfig.ShadowMapSize, ShadowConfig.ShadowMapSize);
	ambientOcclusion.BuildOffsetVectors();
	ambientOcclusion.BuildRandomVectorTexture(MainCommandList.Get());

	mCamera.SetPosition(0.0f, 5.0f, -15.0f);

	ResetCommandList();

	textR = new TextRender(d3dDevice.Get(), MainCommandList.Get(), SwapChainBufferCount);
	textR->SetScreenSize(static_cast<float>(Width), static_cast<float>(Height));

	CreateRootSignature();
	CreatePipesAndShaders();
	AddShapeGeometry();
	CreateSRVDescriptorHeap();
	LoadTextures();
	textR->SetSharedSrvDescriptorHeap(SrvDescriptorHeap.Get(), CbvSrvUavDescriptorSize, SrvDescriptorHeapIndex, 64u);

	if (!textR->DXCreateFont(L"DATA\\Fonts\\STXIHEI.TTF", 34))
		return false;
	SrvDescriptorHeapIndex += textR->GetSrvDescriptorCount();

	BuildMaterials();
	BuildLight();

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvCPUHandle(SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvCpuHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart());
	ShadowMapHeapStartIndex = SrvDescriptorHeapIndex;
	D3D12_SHADER_RESOURCE_VIEW_DESC nullShadowSrvDesc = {};
	nullShadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullShadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	nullShadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullShadowSrvDesc.Texture2D.MostDetailedMip = 0;
	nullShadowSrvDesc.Texture2D.MipLevels = 1;
	nullShadowSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	nullShadowSrvDesc.Texture2D.PlaneSlice = 0;
	for (UINT shadowIndex = 0; shadowIndex < ShadowConfig.MaxShadowMapCount; ++shadowIndex)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE reservedShadowSrv(srvCPUHandle, ShadowMapHeapStartIndex + shadowIndex, CbvSrvUavDescriptorSize);
		d3dDevice->CreateShaderResourceView(nullptr, &nullShadowSrvDesc, reservedShadowSrv);
	}
	SrvDescriptorHeapIndex += ShadowConfig.MaxShadowMapCount;
	// SSAO 会连续占用一段 SRV / RTV 槽位，因此这里统一顺延堆索引。
	ambientOcclusion.BuildDescriptors(DepthStencilBuffer.Get(),
		GetCpuSrv().Offset(SrvDescriptorHeapIndex, CbvSrvUavDescriptorSize),
		GetGpuSrv().Offset(SrvDescriptorHeapIndex, CbvSrvUavDescriptorSize),
		rtvCpuHandle.Offset(SwapChainBufferCount, RtvDescriptorSize),
		CbvSrvUavDescriptorSize,
		RtvDescriptorSize);

	SrvDescriptorHeapIndex += 5;

	CreateFrameResources();
	CloseCommandListAndSynchronize();

	return true;
}

void D3DWindow::CreateCommandQueueAndSwapChain()
{
	//描述并创建命令队列。
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ThrowIfFailed(d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&CommandQueue)));

	//释放我们将重新创建的上一个交换链。
	SwapChain.Reset();

	//描述并创建交换链。
	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = Width;
	sd.BufferDesc.Height = Height;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = BackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.OutputWindow = m_hwnd;
	sd.Windowed = true;
	sd.SwapEffect = dxSwapEffect;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	//注意：交换链使用队列执行刷新。
	ComPtr<IDXGISwapChain> p_SwapChain = nullptr;
	ThrowIfFailed(dxgiFactory->CreateSwapChain(
		CommandQueue.Get(),
		&sd,
		p_SwapChain.GetAddressOf()));
	ThrowIfFailed(p_SwapChain.As(&SwapChain));

	ThrowIfFailed(d3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(MainCommandAllocator.GetAddressOf())));

	//创建用于初始GPU设置的主命令列表。
	ThrowIfFailed(d3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		MainCommandAllocator.Get(),     // 关联的命令分配器
		nullptr,                    // 初始的管道状态对象
		IID_PPV_ARGS(MainCommandList.GetAddressOf())));
	MainCommandList->SetName(L"MainCommandList");

	//为每个线程都创建一个命令列表。
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(d3dDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(mFrameResources[i].BeginCommandAllocator.GetAddressOf())));
		
		//创建用于初始GPU设置的主命令列表。
		ThrowIfFailed(d3dDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mFrameResources[i].BeginCommandAllocator.Get(),     // 关联的命令分配器
			nullptr,                    // 初始的管道状态对象
			IID_PPV_ARGS(mFrameResources[i].BeginCommandList.GetAddressOf())));
		mFrameResources[i].BeginCommandList->SetName(L"BeginCommandList");
		ThrowIfFailed(mFrameResources[i].BeginCommandList->Close());

		ThrowIfFailed(d3dDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(mFrameResources[i].MidCommandAllocator.GetAddressOf())));

		//创建用于初始GPU设置的主命令列表。
		ThrowIfFailed(d3dDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mFrameResources[i].MidCommandAllocator.Get(),     // 关联的命令分配器
			nullptr,                    // 初始的管道状态对象
			IID_PPV_ARGS(mFrameResources[i].MidCommandLidt.GetAddressOf())));
		mFrameResources[i].MidCommandLidt->SetName(L"MidCommandList");
		ThrowIfFailed(mFrameResources[i].MidCommandLidt->Close());

		for (UINT j = 0; j < NumContexts; j++)
		{
			ThrowIfFailed(d3dDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&mFrameResources[i].threadCommandAllocators[j])));

			//鍒涘缓姣忎釜绾跨▼鐨勫懡浠ゅ垪琛ㄣ€?
			ThrowIfFailed(d3dDevice->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				mFrameResources[i].threadCommandAllocators[j].Get(),
				nullptr,
				IID_PPV_ARGS(&mFrameResources[i].threadCommandLists[j])));
			mFrameResources[i].threadCommandLists[j]->SetName((L"threadCommandLists" + std::to_wstring(i) +L"." + std::to_wstring(j)).c_str());
			ThrowIfFailed(mFrameResources[i].threadCommandLists[j]->Close());

			ThrowIfFailed(d3dDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&mFrameResources[i].shadowThreadCommandAllocators[j])));

			ThrowIfFailed(d3dDevice->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				mFrameResources[i].shadowThreadCommandAllocators[j].Get(),
				nullptr,
				IID_PPV_ARGS(&mFrameResources[i].shadowThreadCommandLists[j])));
			mFrameResources[i].shadowThreadCommandLists[j]->SetName((L"shadowThreadCommandLists" + std::to_wstring(i) + L"." + std::to_wstring(j)).c_str());
			ThrowIfFailed(mFrameResources[i].shadowThreadCommandLists[j]->Close());

			ThrowIfFailed(d3dDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&mFrameResources[i].normalThreadCommandAllocators[j])));

			ThrowIfFailed(d3dDevice->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				mFrameResources[i].normalThreadCommandAllocators[j].Get(),
				nullptr,
				IID_PPV_ARGS(&mFrameResources[i].normalThreadCommandLists[j])));
			mFrameResources[i].normalThreadCommandLists[j]->SetName((L"normalThreadCommandLists" + std::to_wstring(i) + L"." + std::to_wstring(j)).c_str());
			ThrowIfFailed(mFrameResources[i].normalThreadCommandLists[j]->Close());
		}

		ThrowIfFailed(d3dDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(mFrameResources[i].EndCommandAllocator.GetAddressOf())));

		//创建用于初始GPU设置的主命令列表。
		ThrowIfFailed(d3dDevice->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			mFrameResources[i].EndCommandAllocator.Get(),     // 关联的命令分配器
			nullptr,                    // 初始的管道状态对象
			IID_PPV_ARGS(mFrameResources[i].EndCommandList.GetAddressOf())));
		mFrameResources[i].EndCommandList->SetName(L"EndCommandList");
		ThrowIfFailed(mFrameResources[i].EndCommandList->Close());
	}
}

void D3DWindow::CreateDevice()
{
	//加载渲染管道依赖项。
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// 启用D3D12调试层。
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	// 创建工厂
	// 使用DXGI 1.1工厂生成枚举适配器，创建交换链以及将窗口与alt + enter键序列相关联的对象，
	// 以便切换到全屏显示模式和从全屏显示模式切换。
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	int adapterIndex = 0;
	bool adapterFound = false;

	// 枚举适配器，并选择合适的适配器来创建3D设备对象
	while (dxgiFactory->EnumAdapters1(adapterIndex, &DeviceAdapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 adapterDesc{};
		DeviceAdapter->GetDesc1(&adapterDesc);
		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			adapterIndex++;
			continue;
		}

		HRESULT result = D3D12CreateDevice(
			DeviceAdapter.Get(),
			dxFeatureLevel,
			_uuidof(ID3D12Device),
			nullptr);

		if (SUCCEEDED(result))
		{
			adapterFound = true;
			break;
		}

		adapterIndex++;
	}

	// 尝试创建D3D硬件设备
	ThrowIfFailed(D3D12CreateDevice(
		DeviceAdapter.Get(),
		dxFeatureLevel,
		IID_PPV_ARGS(&d3dDevice)));

	RtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	DsvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	CbvSrvUavDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 检查4X MSAA质量对我们的后缓冲区格式的支持。
	// 所有支持Direct3D 12的设备对所有渲染目标格式都支持4X MSAA，
	// 因此我们只需要检查质量支持。
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = BackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;

	ThrowIfFailed(d3dDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

	m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m4xMsaaQuality > 0 && "意外的MSAA质量水平。");

	ThrowIfFailed(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	fenceEvent = CreateEventEx(nullptr, L"", false, EVENT_ALL_ACCESS);
	assert(fenceEvent != nullptr);
}

void D3DWindow::CreateDescriptorHeaps()
{	
	// 为屏幕法线贴图添加+1，为环境贴图添加+2。
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 3;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(RtvHeap.GetAddressOf())));

	// 为主深度和全部阴影贴图预留 DSV。
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1 + ShadowConfig.MaxShadowMapCount;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(DsvHeap.GetAddressOf())));
}

void D3DWindow::OnResize(bool Fullscreen)
{
	assert(d3dDevice != nullptr);
	assert(SwapChain != nullptr);
	assert(MainCommandAllocator != nullptr);

	// 更改任何资源之前先冲洗。
	FlushCommandQueue();

	// 重置 command list
	ResetCommandList();

	// 释放我们将重新创建的先前资源。
	for (int i = 0; i < SwapChainBufferCount; ++i)
	{
		SwapChainBuffer[i].Reset();
		mFrameResources[i].mCopyTexture.Reset();
	}

	DepthStencilBuffer.Reset();
	if (!Fullscreen)
	{
		Width = EngineHelpers::GetContextWidth(m_hwnd);
		Height = EngineHelpers::GetContextHeight(m_hwnd);
	}
	else
	{
		Width = EngineHelpers::GetDisplayWidth();
		Height = EngineHelpers::GetDisplayHeight();
	}
	if (Width * Height == 0)
		return;

	// 窗口尺寸变化后，交换链、深度缓冲、视口和投影矩阵都需要同步刷新。
	// 调整交换链的大小。
	ThrowIfFailed(SwapChain->ResizeBuffers(
		SwapChainBufferCount,
		Width, Height,
		BackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	// 每次调整大小，需要将后缓冲置为0。
	CurrBackBufferIndex = 0;

	//创建渲染目标视图（RTV）。
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart());

	// 为每个帧创建一个RTV。
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(SwapChain->GetBuffer(i, IID_PPV_ARGS(&SwapChainBuffer[i])));
		d3dDevice->CreateRenderTargetView(SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		SwapChainBuffer[i]->SetName((L"SwapChainBuffer" + std::to_wstring(i)).c_str());
		rtvHeapHandle.Offset(1, RtvDescriptorSize);
	}

	// 创建深度模板缓冲区和视图。
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = Width;
	depthStencilDesc.Height = Height;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;

	//更正2016年11月12日：SSAO章节要求对深度缓冲区有SRV才能从深度缓冲区中读取。
	//因此，因为我们需要为同一资源创建两个视图：
		 // 1. SRV格式：DXGI_FORMAT_R24_UNORM_X8_TYPELESS
		 // 2. DSV格式：DXGI_FORMAT_D24_UNORM_S8_UINT
		 //我们需要使用无类型格式创建深度缓冲区资源。
	depthStencilDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = DepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(DepthStencilBuffer.GetAddressOf())));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());

	// 使用资源格式将描述符创建为整个资源的MIP级别0。
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	d3dDevice->CreateDepthStencilView(DepthStencilBuffer.Get(), &dsvDesc, dsvHandle);

	//将资源从其初始状态转换为深度缓冲区。
	D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(DepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	MainCommandList->ResourceBarrier(1, &Barriers);
	
	CD3DX12_RESOURCE_DESC copyTexDesc;
	copyTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	copyTexDesc.Alignment = 0;
	copyTexDesc.Width = Width;
	copyTexDesc.Height = Height;
	copyTexDesc.DepthOrArraySize = 1;
	copyTexDesc.MipLevels = 1;
	copyTexDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	copyTexDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	copyTexDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	copyTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	copyTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue;        // 性能提示：在创建资源时告诉运行时所需的清除值。
	clearValue.Format = DepthStencilFormat;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&copyTexDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clearValue,
			IID_PPV_ARGS(mFrameResources[i].mCopyTexture.GetAddressOf())));
		mFrameResources[i].mCopyTexture->SetName((L"CopyTexture" + std::to_wstring(i)).c_str());
	}

	CloseCommandListAndSynchronize();

	// 此处设置窗口大小和裁剪大小
	m_viewport = CD3DX12_VIEWPORT{ 0.0f, 0.0f, 
		static_cast<float>(Width),
		static_cast<float>(Height),
		0.0f,1.0f };
	m_scissorRect = CD3DX12_RECT{ 0, 0, 
		(long)Width,
		(long)Height };
	if (textR != nullptr)
		textR->SetScreenSize(static_cast<float>(Width), static_cast<float>(Height));

	// 初始化相机状态
	mCamera.SetLens(0.25f * MathHelps::Pi,
		static_cast<float>(Width) / static_cast<float>(Height),
		1.0f, 1000.0f);

	shadowMap.OnResize(ShadowConfig.ShadowMapSize, ShadowConfig.ShadowMapSize);
	ambientOcclusion.OnResize(Width, Height);

	if (ambientOcclusion.mhAmbientMap0CpuSrv.ptr != 0 && ambientOcclusion.mhNormalMapCpuRtv.ptr != 0)
	{
		auto aoCpuSrvBase = ambientOcclusion.mhAmbientMap0CpuSrv;
		auto aoGpuSrvBase = ambientOcclusion.mhAmbientMap0GpuSrv;
		ambientOcclusion.BuildDescriptors(DepthStencilBuffer.Get(), aoCpuSrvBase, aoGpuSrvBase, ambientOcclusion.mhNormalMapCpuRtv, CbvSrvUavDescriptorSize, RtvDescriptorSize);
	}
}

void D3DWindow::SetFullscreen()
{
	ThrowIfFailed(SwapChain->GetFullscreenState(&fullscreenState, nullptr));
	fullscreenState = !fullscreenState;
	OnResize(fullscreenState);
}

// 创建根签名
void D3DWindow::CreateRootSignature()
{
	// 着色器程序通常需要资源作为输入（常量缓冲区，纹理，采样器）。
	// 根签名定义着色器程序期望的资源。 
	// 如果我们将着色器程序视为函数，将输入资源视为函数参数，则可以将根签名视为定义函数签名。
	{
		const UINT shadowRegisterStart = 13;
		const UINT shadowRegisterCount = 256;
		const UINT aoRegisterIndex = shadowRegisterStart + shadowRegisterCount;
		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] =
		{
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 12, 1, 0},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, shadowRegisterCount, shadowRegisterStart, 0}, 
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, aoRegisterIndex, 0}
		};

		// 根参数可以是表，根描述符或根常量。
		CD3DX12_ROOT_PARAMETER1 slotRootParameter[8];

		// 创建根CBV。效果提示：从最频繁到最不频繁的顺序
		slotRootParameter[0].InitAsConstantBufferView(0); // Per object CBV
		slotRootParameter[1].InitAsConstantBufferView(1); // Per pass CBV
		slotRootParameter[2].InitAsConstantBufferView(2); // Per light pass CBV
		slotRootParameter[3].InitAsConstantBufferView(3); // Per material CBV
		slotRootParameter[4].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[5].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[6].InitAsDescriptorTable(1, &descriptorRanges[2], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[7].InitAsDescriptorTable(1, &descriptorRanges[3], D3D12_SHADER_VISIBILITY_PIXEL);
		//slotRootParameter[8].InitAsConstants(1, 4);

		auto staticSamplers = GetStaticSamplers();

		//根签名是一个根参数的数组。
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
		rootSigDesc.Init_1_1(_countof(slotRootParameter), slotRootParameter,
			(UINT)staticSamplers.size(), staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		//使用单个插槽创建根签名，该插槽指向由单个常量缓冲区组成的描述符范围
		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc,
			D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}

		ThrowIfFailed(hr);
		ThrowIfFailed(d3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(RootSignature.GetAddressOf())));

	}

	ambientOcclusion.CreateRootSignature();
	textR->CreateRootSignature();
}

// 创建管道状态，其中包括编译和加载着色器。
void D3DWindow::CreatePipesAndShaders()
{
	ComPtr<ID3DBlob> vertexShader[着色器计数];
	ComPtr<ID3DBlob> pixelShader[着色器计数];
	ComPtr<ID3DBlob> debugvertexShader;
	ComPtr<ID3DBlob> debugpixelShader;

	vertexShader[天空着色器] = CompileShader(L"DATA/Shaders/Sky", nullptr, "VS", "vs_5_1");
	pixelShader[天空着色器] = CompileShader(L"DATA/Shaders/Sky", nullptr, "PS", "ps_5_1");
	vertexShader[不透明物体着色器] = CompileShader(L"DATA/Shaders/pbrx", nullptr, "VS", "vs_5_1");
	pixelShader[不透明物体着色器] = CompileShader(L"DATA/Shaders/pbrx", nullptr, "PS", "ps_5_1");
	debugvertexShader = CompileShader(L"DATA/Shaders/ShadowDebug", nullptr, "VS", "vs_5_1");
	debugpixelShader = CompileShader(L"DATA/Shaders/ShadowDebug", nullptr, "PS", "ps_5_1");
	vertexShader[文字着色器] = CompileShader(L"DATA/Shaders/Text", nullptr, "VS", "vs_5_1");
	pixelShader[文字着色器] = CompileShader(L"DATA/Shaders/Text", nullptr, "PS", "ps_5_1");

	// 定义顶点输入布局。
	InputElementDescs =
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",		0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,		0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BINORMAL",	0, DXGI_FORMAT_R32G32B32_FLOAT,		0, 60, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	//描述并创建用于渲染场景的PSO。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc = {};
	ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	basePsoDesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	basePsoDesc.pRootSignature = RootSignature.Get();
	basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	basePsoDesc.SampleMask = UINT_MAX;
	basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	basePsoDesc.NumRenderTargets = 1;
	basePsoDesc.RTVFormats[0] = BackBufferFormat;
	basePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	basePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	basePsoDesc.DSVFormat = DepthStencilFormat;

	//
	// 天空的PSO。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = basePsoDesc;

	// 相机位于天球内部，因此请关闭消隐功能。
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	// 确保depth函数不是LESS，而是LESS_EQUAL。
	// 否则，如果将深度缓冲区清除为1，
	// 则z = 1（NDC）处的归一化深度值将无法通过深度测试。
	skyPsoDesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	skyPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[天空着色器].Get());
	skyPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[天空着色器].Get());
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&skyPsoDesc,
		IID_PPV_ARGS(&PipelineState[天空管道])));

	//
	//不透明对象的PSO。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = basePsoDesc;
	opaquePsoDesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	opaquePsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[不透明物体着色器].Get());
	opaquePsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[不透明物体着色器].Get());
	opaquePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc,
		IID_PPV_ARGS(&PipelineState[不透明物体管道])));

	std::vector<ComPtr<ID3DBlob>>shadowMapvertexShader;
	std::vector<ComPtr<ID3DBlob>>shadowMappixelShader;
	std::vector<ComPtr<ID3D12PipelineState>> shadowMapPipelineState;

	//
	//用于阴影贴图传递的PSO。
	//
	shadowMapvertexShader.resize(1);
	shadowMappixelShader.resize(1);
	shadowMapPipelineState.resize(1);
	shadowMap.CreatePipesAndShaders(shadowMapvertexShader, shadowMappixelShader,
		basePsoDesc, shadowMapPipelineState);
	vertexShader[阴影着色器] = shadowMapvertexShader[0];
	pixelShader[阴影着色器] = shadowMappixelShader[0];
	PipelineState[阴影管道] = shadowMapPipelineState[0];

	//
	// PSO for debug layer.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = basePsoDesc;
	debugPsoDesc.pRootSignature = RootSignature.Get();
	debugPsoDesc.VS = CD3DX12_SHADER_BYTECODE(debugvertexShader.Get());
	debugPsoDesc.PS = CD3DX12_SHADER_BYTECODE(debugpixelShader.Get());
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, 
		IID_PPV_ARGS(&debugPipelineState)));

	std::vector<ComPtr<ID3DBlob>>AOvertexShader;
	std::vector<ComPtr<ID3DBlob>>AOpixelShader;
	std::vector<ComPtr<ID3D12PipelineState>> AOPipelineState;

	//
	// AO的PSO。
	// 	

	AOvertexShader.resize(3);
	AOpixelShader.resize(3);
	AOPipelineState.resize(3);
	ambientOcclusion.CreatePipesAndShaders(AOvertexShader, AOpixelShader, basePsoDesc, AOPipelineState);
	vertexShader[法线绘制着色器]= AOvertexShader[0];
	pixelShader[法线绘制着色器] = AOpixelShader[0];
	PipelineState[法线绘制管道] = AOPipelineState[0];
	vertexShader[环境遮蔽着色器] = AOvertexShader[1];
	pixelShader[环境遮蔽着色器] = AOpixelShader[1];
	PipelineState[环境遮蔽管道] = AOPipelineState[1];
	vertexShader[遮蔽模糊着色器] = AOvertexShader[2];
	pixelShader[遮蔽模糊着色器] = AOpixelShader[2];
	PipelineState[遮蔽模糊管道] = AOPipelineState[2];

	//
	//文字的PSO。
	//
	textR->CreatePipesAndShaders(vertexShader[文字着色器].Get(), pixelShader[文字着色器].Get(),
		BackBufferFormat, DepthStencilFormat, &PipelineState[文字管道]);
}

void D3DWindow::CreateSRVDescriptorHeap()
{
	//
	//创建SRV堆。
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = SwapChainBufferCount * 256; //贴图资源数量（大于实际数量没关系小了不行）
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	//srvHeapDesc.NodeMask = 0;

	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(
		&srvHeapDesc, IID_PPV_ARGS(&SrvDescriptorHeap)));
}

void D3DWindow::LoadTextures()
{
	ResourceUploadBatch resourceUpload(d3dDevice.Get());
	resourceUpload.Begin();

	Texture DiffuseTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"DiffuseMap", L"DATA/Textures/white8x8.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture defaultNmapTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"defaultNmap", L"DATA/Textures/8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture defaultMetallic(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"defaultMetal", L"DATA/Textures/8K_Metalness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture defaultRoughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"defaultRough", L"DATA/Textures/8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex); 
	SrvDescriptorHeapIndex++;

	TextureGroups[L"Diffuse"].resize(5);
	TextureGroups[L"Diffuse"][0] = DiffuseTex;
	TextureGroups[L"Diffuse"][1] = defaultNmapTex;
	TextureGroups[L"Diffuse"][3] = defaultMetallic;
	TextureGroups[L"Diffuse"][4] = defaultRoughness;

	Texture skyCubeTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"skyMap", L"DATA/HDRIs/scythian_tombs_2_4k.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"skyMap"].resize(1);
	TextureGroups[L"skyMap"][0] = skyCubeTex;
	SkyTexHeapIndex = skyCubeTex.GetIndex();

	Texture bricksTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"bricksDiffuseMap", L"DATA/Textures/bricks2.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture bricksMapTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"bricksNormalMap", L"DATA/Textures/bricks2_nmap.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"bricks"].resize(2);
	TextureGroups[L"bricks"][0] = bricksTex;
	TextureGroups[L"bricks"][1] = bricksMapTex;

	Texture stoneTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"stoneDiffuseMap", L"DATA/Textures/stone.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"stone"].resize(1);
	TextureGroups[L"stone"][0] = stoneTex;

	Texture tileTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"tileDiffuseMap", L"DATA/Textures/tile.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture tileMapTex(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"tileNormalMap", L"DATA/Textures/tile_nmap.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"tile"].resize(2);
	TextureGroups[L"tile"][0] = tileTex;
	TextureGroups[L"tile"][1] = tileMapTex;

	//--------------------------------------------------------
	Texture semlcibb_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Albedo", L"DATA/Textures/Brick_Modern/semlcibb_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Normal", L"DATA/Textures/Brick_Modern/semlcibb_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Specular", L"DATA/Textures/Brick_Modern/semlcibb_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Roughness", L"DATA/Textures/Brick_Modern/semlcibb_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex); 
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"semlcibb_Displacement", L"DATA/Textures/Brick_Modern/semlcibb_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"semlcibb"].resize(12);
	TextureGroups[L"semlcibb"][0] = semlcibb_Albedo;
	TextureGroups[L"semlcibb"][1] = semlcibb_Normal;
	TextureGroups[L"semlcibb"][2] = semlcibb_Specular;
	TextureGroups[L"semlcibb"][4] = semlcibb_Roughness;
	TextureGroups[L"semlcibb"][5] = semlcibb_Displacement;

	//--------------------------------------------------------
	Texture rustediron_basecolor(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"rustediron_basecolor", L"DATA/Textures/rustediron/rustediron2_basecolor.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rustediron_normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"rustediron_normal", L"DATA/Textures/rustediron/rustediron2_normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture rustediron_metallic(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"rustediron_metallic", L"DATA/Textures/rustediron/rustediron2_metallic.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rustediron_roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"rustediron_roughness", L"DATA/Textures/rustediron/rustediron2_roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"rustediron"].resize(12);
	TextureGroups[L"rustediron"][0] = rustediron_basecolor;
	TextureGroups[L"rustediron"][1] = rustediron_normal;
	TextureGroups[L"rustediron"][3] = rustediron_metallic;
	TextureGroups[L"rustediron"][4] = rustediron_roughness;

	//--------------------------------------------------------
	Texture rm4kshp_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Albedo", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Normal", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Specular", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Roughness", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex); 
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Dirty_Displacement", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"rm4kshp"].resize(12);
	TextureGroups[L"rm4kshp"][0] = rm4kshp_Albedo;
	TextureGroups[L"rm4kshp"][1] = rm4kshp_Normal;
	TextureGroups[L"rm4kshp"][2] = rm4kshp_Specular;
	TextureGroups[L"rm4kshp"][4] = rm4kshp_Roughness;
	TextureGroups[L"rm4kshp"][5] = rm4kshp_Displacement;

	//--------------------------------------------------------
	Texture sdbhdd3b_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Albedo", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Normal", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Specular", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Roughness", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex); 
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Concrete_Rough_Displacement", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"sdbhdd3b"].resize(12);
	TextureGroups[L"sdbhdd3b"][0] = sdbhdd3b_Albedo;
	TextureGroups[L"sdbhdd3b"][1] = sdbhdd3b_Normal;
	TextureGroups[L"sdbhdd3b"][2] = sdbhdd3b_Specular;
	TextureGroups[L"sdbhdd3b"][4] = sdbhdd3b_Roughness;
	TextureGroups[L"sdbhdd3b"][5] = sdbhdd3b_Displacement;

	//--------------------------------------------------------
	Texture sfknaeoa_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Albedo", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Normal", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Specular", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Roughness", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Grass_Wild_Displacement", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);

	SrvDescriptorHeapIndex++;
	TextureGroups[L"sfknaeoa"].resize(12);
	TextureGroups[L"sfknaeoa"][0] = sfknaeoa_Albedo;
	TextureGroups[L"sfknaeoa"][1] = sfknaeoa_Normal;
	TextureGroups[L"sfknaeoa"][2] = sfknaeoa_Specular;
	TextureGroups[L"sfknaeoa"][4] = sfknaeoa_Roughness;
	TextureGroups[L"sfknaeoa"][5] = sfknaeoa_Displacement;

	//--------------------------------------------------------
	Texture copper_rock1_alb(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"copper-rock1-alb", L"DATA/Textures/rockcopper/copper-rock1-alb.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture copper_rock1_normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"copper-rock1-normal", L"DATA/Textures/rockcopper/copper-rock1-normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture copper_rock1_metal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"copper-rock1-metal", L"DATA/Textures/rockcopper/copper-rock1-metal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture copper_rock1_rough(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"copper-rock1-rough", L"DATA/Textures/rockcopper/copper-rock1-rough.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"copper_rock1"].resize(12);
	TextureGroups[L"copper_rock1"][0] = copper_rock1_alb;
	TextureGroups[L"copper_rock1"][1] = copper_rock1_normal;
	TextureGroups[L"copper_rock1"][3] = copper_rock1_metal;
	TextureGroups[L"copper_rock1"][4] = copper_rock1_rough;

	//--------------------------------------------------------
	Texture scpgdgca_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Albedo", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Normal", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Specular", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Roughness", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Stone_Wall_Displacement", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"scpgdgca"].resize(12);
	TextureGroups[L"scpgdgca"][0] = scpgdgca_Albedo;
	TextureGroups[L"scpgdgca"][1] = scpgdgca_Normal;
	TextureGroups[L"scpgdgca"][2] = scpgdgca_Specular;
	TextureGroups[L"scpgdgca"][4] = scpgdgca_Roughness;
	TextureGroups[L"scpgdgca"][5] = scpgdgca_Displacement;

	//--------------------------------------------------------
	Texture se2abbvc_Albedo(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Albedo", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Normal(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Normal", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Specular(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Specular", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Roughness(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Roughness", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Displacement(d3dDevice.Get(), SrvDescriptorHeap.Get(), &resourceUpload,
		L"Metal_Bare_Displacement", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	TextureGroups[L"se2abbvc"].resize(12);
	TextureGroups[L"se2abbvc"][0] = se2abbvc_Albedo;
	TextureGroups[L"se2abbvc"][1] = se2abbvc_Normal;
	TextureGroups[L"se2abbvc"][2] = se2abbvc_Specular;
	TextureGroups[L"se2abbvc"][4] = se2abbvc_Roughness;
	TextureGroups[L"se2abbvc"][5] = se2abbvc_Displacement;

	auto uploadResourcesFinished = resourceUpload.End(
		CommandQueue.Get());

	uploadResourcesFinished.wait();
}

void D3DWindow::AddShapeGeometry()
{
	AssimpLoader assimpLoader;

	auto loadBuiltinGeometry = [&](const wchar_t* modelPath, const wchar_t* geometryName)
	{
		std::vector<Mesh> model = assimpLoader.LoadRawModel(modelPath);
		if (model.empty())
			return;

		AggrObject[geometryName].IndexCount = static_cast<UINT>(model[0].indices32.size());
		AggrObject[geometryName].StartIndexLocation = 0;
		AggrObject[geometryName].BaseVertexLocation = 0;

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
		geo.Name = geometryName;

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo.VertexBufferCPU));
		CopyMemory(geo.VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo.IndexBufferCPU));
		CopyMemory(geo.IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		geo.VertexBufferGPU = CreateDefaultBuffer(d3dDevice.Get(),
			MainCommandList.Get(), vertices.data(), vbByteSize, geo.VertexBufferUploader);

		geo.IndexBufferGPU = CreateDefaultBuffer(d3dDevice.Get(),
			MainCommandList.Get(), indices.data(), ibByteSize, geo.IndexBufferUploader);

		geo.vertexBufferView.BufferLocation = geo.VertexBufferGPU->GetGPUVirtualAddress();
		geo.vertexBufferView.StrideInBytes = sizeof(Vertex);
		geo.vertexBufferView.SizeInBytes = vbByteSize;
		geo.indexBufferView.BufferLocation = geo.IndexBufferGPU->GetGPUVirtualAddress();
		geo.indexBufferView.Format = IndexBufferFormat;
		geo.indexBufferView.SizeInBytes = ibByteSize;

		Geometries[geo.Name] = geo;
	};

	loadBuiltinGeometry(L"DATA\\Models\\Sphere.obj", L"shapeGeo");
}

void D3DWindow::AddShapeGeometry(MeshGeometry* geo)
{
	Geometries[geo->Name] = *geo;
}

void D3DWindow::RemoveShapeGeometry(std::wstring name)
{
	auto geometryIt = Geometries.find(name);
	if (geometryIt == Geometries.end())
		return;

	// 不要立刻释放 GPU 资源。
	// 命令列表/FrameResource 可能仍在引用它们；先把几何从活跃表里移除，
	// 等下一次 FlushCommandQueue 确认 GPU 空闲后，再统一释放。
	DeferredReleaseGeometries.push_back(std::move(geometryIt->second));
	Geometries.erase(geometryIt);
}

bool D3DWindow::HasShapeGeometry(const std::wstring& name) const
{
	return Geometries.find(name) != Geometries.end();
}

void D3DWindow::BuildMaterials()
{
	auto autoMaterial = std::make_unique<Material>();
	autoMaterial->SetName(L"autoMat");
	autoMaterial->MatCBIndex = 0;
	autoMaterial->DiffuseTexture = &TextureGroups[L"Diffuse"][0];
	autoMaterial->NormalTexture = &TextureGroups[L"Diffuse"][1];
	autoMaterial->MetallicTexture = &TextureGroups[L"Diffuse"][3];;
	autoMaterial->RoughnessTexture = &TextureGroups[L"Diffuse"][4];;
	autoMaterial->MatTransform = MathHelps::Identity;
	autoMaterial->Properties.Metallic = 0.0f;
	autoMaterial->Properties.Roughness = 0.0f;
	autoMaterial->Properties.ClearCoatThickness = 0.0f;
	autoMaterial->Properties.ClearCoatRoughness = 0.0f;
	autoMaterial->Properties.Anisotropy = 0.0f;
	autoMaterial->Properties.AnisotropyRotation = 0.0f;

	Materials[L"autoMat"] = *autoMaterial.get();

	auto sky = std::make_unique<Material>();
	sky->SetName(L"sky");
	sky->MatCBIndex = 1;
	sky->DiffuseTexture = &TextureGroups[L"skyMap"][0];
	//sky->NormalTexture = 0;//sky就不需要设置了
	SkyMapIndex = TextureGroups[L"skyMap"][0].GetIndex();
	sky->MatTransform = MathHelps::Identity;
	Materials[L"sky"] = *sky.get();
	SkyMaterialByTexturePath[NormalizeAssetPath(L"DATA/HDRIs/scythian_tombs_2_4k.png")] = L"sky";
}

void D3DWindow::BuildLight()
{
	AmbientColor = { 0.0f, 0.0f, 0.0f, 0.0f };
	ClearLights();
}

void D3DWindow::ClearLights()
{
	Lights.clear();
	RuntimeLightsCache.clear();
	ShadowRenderEntries.clear();
	RotatedLightDirections.clear();
	ShadowTransform.clear();
	ShadowTransform.push_back(MathHelps::Identity);
	MainPassCB.LightConst = 0;
	FreshenAllLight = true;
}

void D3DWindow::SetAmbientColor(const DirectX::XMFLOAT4& ambientColor)
{
	AmbientColor = ambientColor;
	FreshenAllLight = true;
}

void D3DWindow::AddLight(Light* light)
{
	if (light == nullptr)
		return;

	light->LitCBIndex = static_cast<int>(Lights.size());
	Lights[light->GetName()] = *light;
	MainPassCB.LightConst = static_cast<UINT>(Lights.size());
	FreshenAllLight = true;
}

void D3DWindow::AddRenderItem(std::wstring renderItemName, ObjectCollection* Obj, const std::wstring& geometryName,
	UINT renderLayerIndex, const DirectX::XMFLOAT4X4* worldTransform,
	const DirectX::XMFLOAT4X4* texTransform, const std::wstring* materialName,
	bool rebuildOpaqueBatches)
{
	if (Obj == nullptr)
		return;

	// 几何必须已经先注册到 Geometries 中，否则该渲染项无效。
	auto geometryIt = Geometries.find(geometryName);
	if (geometryIt == Geometries.end())
		return;

	if (materialName != nullptr && !materialName->empty())
	{
		// 若指定了材质名，则优先绑定该材质。
		auto materialIt = Materials.find(*materialName);
		if (materialIt != Materials.end())
			Obj->Material = &materialIt->second;
	}

	// 未显式指定材质时回退到默认材质，避免出现空材质引用。
	if (Obj->Material == nullptr)
		Obj->Material = &Materials[L"autoMat"];

	auto existingRenderItemIt = AllRitems.find(renderItemName);
	const bool replacingExistingItem = existingRenderItemIt != AllRitems.end();
	UINT existingObjectCBIndex = ObjCBCount;
	if (replacingExistingItem)
	{
		existingObjectCBIndex = existingRenderItemIt->second.ObjCBIndex;

		// 同名渲染项重建时，先把旧的层索引引用清掉，
		// 避免同一对象残留在多个渲染层或保留旧指针导致重复绘制/闪烁。
		for (UINT layerIndex = 0; layerIndex < (UINT)渲染项目计数; ++layerIndex)
			RitemLayer[layerIndex].erase(renderItemName);

		DirtyObjectCBItems.erase(renderItemName);
	}

	RenderItem renderItem;
	if (worldTransform != nullptr)
		renderItem.WorldTransform = *worldTransform;
	else
		// 历史默认行为：没有给定矩阵时使用一个固定缩放的初始矩阵。
		XMStoreFloat4x4(&renderItem.WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.0f, 0.0f));

	if (texTransform != nullptr)
		renderItem.TexTransform = *texTransform;
	else
		XMStoreFloat4x4(&renderItem.TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));

	renderItem.ObjCBIndex = replacingExistingItem ? existingObjectCBIndex : ObjCBCount;
	renderItem.NumFramesDirty = SwapChainBufferCount;
	renderItem.Obj = Obj;
	renderItem.Geo = &geometryIt->second;
	renderItem.PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// AllRitems 保存所有渲染项，RitemLayer 只保存各通道的引用索引。
	AllRitems[renderItemName] = renderItem;
	RitemLayer[renderLayerIndex][renderItemName] = &AllRitems[renderItemName];
	DirtyObjectCBItems.insert(renderItemName);
	if (!replacingExistingItem)
		ObjCBCount++;
	if (rebuildOpaqueBatches)
		RebuildOpaqueThreadBatches();
}

void D3DWindow::RemoveRenderItem(std::wstring meshName, UINT renderLayerIndex)
{
	RitemLayer[renderLayerIndex].erase(meshName);
	DirtyObjectCBItems.erase(meshName);
	if (AllRitems.erase(meshName) > 0 && ObjCBCount > 0)
		ObjCBCount--;

	RebuildOpaqueThreadBatches();
}

void D3DWindow::ClearRenderItems()
{
	// 仅清空渲染项汇总，不处理 ECS 实体本身。
	for (UINT layerIndex = 0; layerIndex < (UINT)渲染项目计数; ++layerIndex)
	{
		RitemLayer[layerIndex].clear();
	}

	AllRitems.clear();
	DirtyObjectCBItems.clear();
	ObjCBCount = 0;

	for (UINT i = 0; i < NumContexts; ++i)
	{
		OpaqueThreadBatches[i].clear();
	}
}

void D3DWindow::AppendRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr)
		return;
	if (ecs == nullptr)
	{
#ifdef _DEBUG
		assert(false && "需要一个有效的 WitchcraECS 指针。");
#endif
		return;
	}

	// 从实体组件中提取渲染所需的最小信息，并重建 RenderItem。
	EntityRenderView renderView;
	if (ecs->BuildEntityRenderView(entity, &renderView) && renderView.meshComponent != nullptr && renderView.visible)
	{
		// 重建渲染项时同步恢复世界矩阵与纹理矩阵。
		DirectX::XMFLOAT4X4 worldTransform = MathHelps::Identity;
		DirectX::XMFLOAT4X4 texTransform = MathHelps::Identity;
		BuildEntityRenderTransforms(entity, ecs, &worldTransform, &texTransform);

		AddRenderItem(renderView.renderItemName, renderView.meshComponent->GetObjectCollection(), renderView.geometryName,
			renderView.renderLayerIndex, &worldTransform, &texTransform,
			renderView.materialName.empty() ? nullptr : &renderView.materialName, false);
	}

	// 递归处理子实体，恢复完整层级对应的渲染项集合。
	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
	{
		AppendRenderItemsFromEntity(childEntity, ecs);
	}
}

void D3DWindow::RebuildRenderItemsFromEntities(WitchcraECS* ecs)
{
	// 用 ECS 当前实体树重新生成渲染项缓存。
	if (ecs == nullptr)
	{
#ifdef _DEBUG
		assert(false && "需要一个有效的 WitchcraECS 指针。");
#endif
		return;
	}

	mLastExternalECS = ecs;
	ecs->SyncTransformsToFlecs();

	ClearRenderItems();

	const std::vector<SceneEntityBase*>& rootEntities = ecs->GetSceneRootEntities();
	for (UINT i = 0; i < ecs->GetSceneRootEntityCount(); ++i)
	{
		AppendRenderItemsFromEntity(rootEntities[i], ecs);
	}

	// 渲染项数量变化后，线程分片和 FrameResource 容量都需要同步刷新。
	RebuildOpaqueThreadBatches();
	CreateFrameResources();
}

void D3DWindow::AddRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr)
		return;
	if (ecs == nullptr)
	{
#ifdef _DEBUG
		assert(false && "需要一个有效的 WitchcraECS 指针。");
#endif
		return;
	}

	mLastExternalECS = ecs;
	ecs->SyncTransformsToFlecs();

	// 先清掉同名旧项，再按当前 ECS 状态把这棵子树重新挂回渲染缓存。
	RemoveRenderItemsFromEntityRecursive(entity, ecs);
	AppendRenderItemsFromEntity(entity, ecs);
	ReindexRenderItemObjectCBIndices();
	RebuildOpaqueThreadBatches();
	CreateFrameResources();
}

void D3DWindow::RemoveRenderItemsFromEntity(SceneEntityBase* entity)
{
	if (entity == nullptr)
		return;

	// 旧接口仅作为兼容入口保留；
	// 内部统一转发到带 ecs 的新接口，避免继续依赖 ServicesContainer::FindServiceAs。
	if (mLastExternalECS != nullptr)
	{
		RemoveRenderItemsFromEntity(entity, mLastExternalECS);
		return;
	}

#ifdef _DEBUG
	assert(false && "RemoveRenderItemsFromEntity(entity) 已弃用，请传入有效的 WitchcraECS*。");
#endif
}

void D3DWindow::RemoveRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr)
		return;
	if (ecs == nullptr)
	{
#ifdef _DEBUG
		assert(false && "需要一个有效的 WitchcraECS 指针。");
#endif
		return;
	}

	mLastExternalECS = ecs;
	RemoveRenderItemsFromEntityRecursive(entity, ecs);
	ReindexRenderItemObjectCBIndices();
	RebuildOpaqueThreadBatches();
	CreateFrameResources();
}

void D3DWindow::UpdateRenderItemsTransformFromEntity(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr)
		return;
	if (ecs == nullptr)
	{
#ifdef _DEBUG
		assert(false && "需要一个有效的 WitchcraECS 指针。");
#endif
		return;
	}

	mLastExternalECS = ecs;
	ecs->SyncTransformsToFlecs();
	UpdateRenderItemsTransformFromEntityRecursive(entity, ecs);
}

void D3DWindow::UpdateRenderItemsTransformFromEntityRecursive(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr)
		return;

	EntityRenderView renderView;
	if (ecs->BuildEntityRenderView(entity, &renderView) && renderView.meshComponent != nullptr && renderView.visible)
	{
		RenderItem* renderItem = GetRenderItems(renderView.renderItemName);
		if (renderItem != nullptr)
		{
			DirectX::XMFLOAT4X4 worldTransform = MathHelps::Identity;
			DirectX::XMFLOAT4X4 texTransform = MathHelps::Identity;
			if (BuildEntityRenderTransforms(entity, ecs, &worldTransform, &texTransform))
			{
				renderItem->WorldTransform = worldTransform;
				renderItem->TexTransform = texTransform;
				renderItem->NumFramesDirty = SwapChainBufferCount;
				DirtyObjectCBItems.insert(renderView.renderItemName);
			}
		}
	}

	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
	{
		UpdateRenderItemsTransformFromEntityRecursive(childEntity, ecs);
	}
}

void D3DWindow::RemoveRenderItemsFromEntityRecursive(SceneEntityBase* entity, WitchcraECS* ecs)
{
	if (entity == nullptr || ecs == nullptr)
		return;

	EntityRenderView renderView;
	if (ecs->BuildEntityRenderView(entity, &renderView) && renderView.meshComponent != nullptr)
	{
		RitemLayer[renderView.renderLayerIndex].erase(renderView.renderItemName);
		DirtyObjectCBItems.erase(renderView.renderItemName);
		AllRitems.erase(renderView.renderItemName);
	}

	for (SceneEntityBase* childEntity : ecs->GetSceneChildren(entity))
	{
		RemoveRenderItemsFromEntityRecursive(childEntity, ecs);
	}
}

void D3DWindow::ReindexRenderItemObjectCBIndices()
{
	UINT objectIndex = 0;
	for (auto& renderItemPair : AllRitems)
	{
		renderItemPair.second.ObjCBIndex = objectIndex++;
	}
	ObjCBCount = objectIndex;
	FreshenObjectCBs();
}

void D3DWindow::RebuildOpaqueThreadBatches()
{
	// 透明/天空等通道仍由主线程处理；这里只重建不透明通道的线程分片。
	for (UINT i = 0; i < NumContexts; ++i)
	{
		OpaqueThreadBatches[i].clear();
	}

	std::vector<std::pair<std::wstring, RenderItem*>> opaqueItems;
	opaqueItems.reserve(RitemLayer[不透明物体渲染项目].size());

	for (const auto& item : RitemLayer[不透明物体渲染项目])
	{
		if (item.second != nullptr)
			opaqueItems.push_back(item);
	}

	std::sort(opaqueItems.begin(), opaqueItems.end(),
		[](const auto& lhs, const auto& rhs)
		{
			return lhs.first < rhs.first;
		});

	size_t itemIndex = 0;
	const size_t baseBatchSize = opaqueItems.size() / NumContexts;
	const size_t remainder = opaqueItems.size() % NumContexts;

	for (UINT threadIndex = 0; threadIndex < NumContexts; ++threadIndex)
	{
		const size_t batchSize = baseBatchSize + (threadIndex < remainder ? 1u : 0u);
		auto& batch = OpaqueThreadBatches[threadIndex];
		batch.reserve(batchSize);

		// 将排序后的渲染项尽量平均分配给每个工作线程。
		for (size_t i = 0; i < batchSize; ++i)
		{
			batch.push_back(opaqueItems[itemIndex++].second);
		}
	}
}

// 创建帧资源
void D3DWindow::CreateFrameResources()
{
	const UINT requiredObjectCount = std::max<UINT>(1u, static_cast<UINT>(AllRitems.size()));
	const UINT requiredMaterialCount = std::max<UINT>(1u, static_cast<UINT>(Materials.size()));

	// 尽量避免频繁重建 FrameResource。
	// 旧版这里按“精确数量”重建，会立刻释放旧 UploadBuffer，
	// 容易与仍在飞行中的命令列表形成资源生命周期竞争。
	if (FrameResourceObjectCapacity >= requiredObjectCount &&
		FrameResourceMaterialCapacity >= requiredMaterialCount &&
		!mFrameResources.empty())
	{
		FreshenObjectCBs();
		FreshenMaterialCBs();
		FreshenLightCBs();
		return;
	}

	// 旧 FrameResource 中的 ObjectCB / MaterialCB / PassCB 仍可能被上一批命令列表引用。
	// 真正需要扩容时，先等待 GPU 完全消费旧命令，避免 UploadBuffer 提前析构。
	FlushCommandQueue();

	FrameResourceObjectCapacity = std::max<UINT>(requiredObjectCount, std::max<UINT>(FrameResourceObjectCapacity * 2u, 256u));
	FrameResourceMaterialCapacity = std::max<UINT>(requiredMaterialCount, std::max<UINT>(FrameResourceMaterialCapacity * 2u, 64u));

	for (UINT i = 0; i < SwapChainBufferCount; ++i)
	{
		mFrameResources[i].Create(d3dDevice.Get(),
			1 + ShadowConfig.MaxShadowMapCount,
			FrameResourceObjectCapacity,
			FrameResourceMaterialCapacity);
	}

	// 新建/重建 FrameResource 后，新的上传缓冲内容是空的，
	// 需要把当前场景中的对象、材质、灯光常量重新整帧回灌一次。
	FreshenObjectCBs();
	FreshenMaterialCBs();
	FreshenLightCBs();
}

std::wstring D3DWindow::GetMaterialName(std::wstring meshName)
{
	RenderItem* renderItem = GetRenderItems(meshName);
	if (renderItem == nullptr || renderItem->Obj == nullptr || renderItem->Obj->Material == nullptr)
		return L"";

	return renderItem->Obj->Material->GetName();
}

void D3DWindow::SetMaterial(std::wstring meshName, std::wstring materialName)
{
	RenderItem* renderItem = GetRenderItems(meshName);
	if (renderItem == nullptr || renderItem->Obj == nullptr)
	{
#ifdef _DEBUG
		OutputDebugStringW((L"[D3DWindow] SetMaterial 跳过：缺少渲染项目 -> " + meshName + L"\n").c_str());
#endif
		return;
	}

	auto materialIt = Materials.find(materialName);
	if (materialIt == Materials.end())
	{
		auto autoMaterialIt = Materials.find(L"autoMat");
		if (autoMaterialIt == Materials.end())
		{
#ifdef _DEBUG
			OutputDebugStringW((L"[D3DWindow] SetMaterial 跳过：缺少材质 -> " + materialName + L"\n").c_str());
#endif
			return;
		}

		renderItem->Obj->Material = &autoMaterialIt->second;
		return;
	}

	renderItem->Obj->Material = &materialIt->second;
}

std::wstring D3DWindow::CreateMaterialFromImport(const std::wstring& Name, const ImportedMaterialInfo& materialInfo)
{
	const std::wstring uniqueMaterialName = MakeUniqueName(Materials, Name.empty() ? L"ImportedMaterial" : Name);
	const std::wstring textureGroupName = uniqueMaterialName + L"_TextureGroup";

	Texture* fallbackDiffuse = &TextureGroups[L"Diffuse"][0];
	Texture* fallbackNormal = &TextureGroups[L"Diffuse"][1];
	Texture* fallbackSpecular = &TextureGroups[L"Diffuse"][0];
	Texture* fallbackMetallic = &TextureGroups[L"Diffuse"][3];
	Texture* fallbackRoughness = &TextureGroups[L"Diffuse"][4];

	std::vector<Texture> textureGroup(5);
	ResourceUploadBatch resourceUpload(d3dDevice.Get());
	resourceUpload.Begin();

	auto createTextureSlot = [&](UINT slotIndex, const ImportedTextureSource& source, Texture* fallback, const std::wstring& slotName)
	{
		Texture texture;
		if (!source.Path.empty() && std::filesystem::exists(source.Path))
		{
			texture.Create(
				d3dDevice.Get(),
				SrvDescriptorHeap.Get(),
				&resourceUpload,
				uniqueMaterialName + L"_" + slotName,
				source.Path,
				ResolveTextureTypeFromPath(source.Path),
				SrvDescriptorHeapIndex);
		}
		else
		{
			texture.CreateAlias(
				d3dDevice.Get(),
				SrvDescriptorHeap.Get(),
				uniqueMaterialName + L"_" + slotName,
				fallback != nullptr ? fallback->GetResource() : nullptr,
				SrvDescriptorHeapIndex);
		}

		textureGroup[slotIndex] = texture;
		++SrvDescriptorHeapIndex;
	};

	createTextureSlot(0, materialInfo.DiffuseTexture, fallbackDiffuse, L"Diffuse");
	createTextureSlot(1, materialInfo.NormalTexture, fallbackNormal, L"Normal");
	createTextureSlot(2, materialInfo.SpecularTexture, fallbackSpecular, L"Specular");
	createTextureSlot(3, materialInfo.MetallicTexture, fallbackMetallic, L"Metallic");
	createTextureSlot(4, materialInfo.RoughnessTexture, fallbackRoughness, L"Roughness");

	auto uploadResourcesFinished = resourceUpload.End(CommandQueue.Get());
	uploadResourcesFinished.wait();

	TextureGroups[textureGroupName] = textureGroup;

	Material material;
	material.SetName(uniqueMaterialName);
	material.MatCBIndex = (int)Materials.size();
	material.DiffuseTexture = &TextureGroups[textureGroupName][0];
	material.NormalTexture = &TextureGroups[textureGroupName][1];
	material.SpecularTexture = &TextureGroups[textureGroupName][2];
	material.MetallicTexture = &TextureGroups[textureGroupName][3];
	material.RoughnessTexture = &TextureGroups[textureGroupName][4];
	material.Properties.DiffuseAlbedo = materialInfo.DiffuseColor;
	material.Properties.Emissive = materialInfo.Emissive;
	material.Properties.Metallic = materialInfo.Metallic;
	material.Properties.Roughness = materialInfo.Roughness;
	material.Properties.UseNormalTexture = !materialInfo.NormalTexture.Path.empty() ? 1u : 0u;
	material.Properties.UseMetallicTexture = !materialInfo.MetallicTexture.Path.empty() ? 1u : 0u;
	material.Properties.UseRoughnessTexture = !materialInfo.RoughnessTexture.Path.empty() ? 1u : 0u;
	material.NumFramesDirty = SwapChainBufferCount;

	Materials[uniqueMaterialName] = material;
	FreshenMaterialCBs();

	return uniqueMaterialName;
}

std::wstring D3DWindow::GetOrCreateMaterialFromWMaterialFile(const std::filesystem::path& materialFilePath)
{
	if (materialFilePath.empty())
		return L"";

	const std::wstring normalizedPath = NormalizeAssetPath(materialFilePath.wstring());
	auto existingMaterialIt = MaterialByFilePath.find(normalizedPath);
	if (existingMaterialIt != MaterialByFilePath.end())
	{
		auto materialIt = Materials.find(existingMaterialIt->second);
		if (materialIt != Materials.end())
			return existingMaterialIt->second;
	}

	WMaterialFileData materialFileData;
	if (!WMaterialFile::LoadFromFile(materialFilePath, &materialFileData))
		return L"";

	const ImportedMaterialInfo importedMaterialInfo =
		ConvertMaterialFileDataToImportedInfo(materialFileData, materialFilePath);
	const std::wstring materialName = CreateMaterialFromImport(
		importedMaterialInfo.Name.empty() ? materialFilePath.stem().wstring() : importedMaterialInfo.Name,
		importedMaterialInfo);
	if (!materialName.empty())
	{
		MaterialByFilePath[normalizedPath] = materialName;
		auto materialIt = Materials.find(materialName);
		if (materialIt != Materials.end())
		{
			materialIt->second.Properties.UseNormalTexture = materialFileData.UseNormalTexture ? 1u : 0u;
			materialIt->second.Properties.UseMetallicTexture = materialFileData.UseMetallicTexture ? 1u : 0u;
			materialIt->second.Properties.UseRoughnessTexture = materialFileData.UseRoughnessTexture ? 1u : 0u;
			materialIt->second.NumFramesDirty = SwapChainBufferCount;
			FreshenMaterialCBs();
		}
	}

	return materialName;
}

std::wstring D3DWindow::GetMaterialFilePathByRuntimeMaterialName(const std::wstring& runtimeMaterialName) const
{
	for (const auto& materialPair : MaterialByFilePath)
	{
		if (materialPair.second == runtimeMaterialName)
			return materialPair.first;
	}

	return L"";
}

std::wstring D3DWindow::GetSkyTexturePathByRuntimeMaterialName(const std::wstring& runtimeMaterialName) const
{
	for (const auto& materialPair : SkyMaterialByTexturePath)
	{
		if (materialPair.second == runtimeMaterialName)
			return materialPair.first;
	}

	return L"";
}

std::wstring D3DWindow::GetOrCreateSkyMaterial(const std::wstring& skyTexturePath)
{
	const std::wstring resolvedPath = NormalizeAssetPath(
		skyTexturePath.empty() ? L"DATA/HDRIs/scythian_tombs_2_4k.png" : skyTexturePath);
	std::filesystem::path resolvedFilePath = std::filesystem::path(resolvedPath);
	if (resolvedFilePath.is_relative())
	{
		std::filesystem::path probe = std::filesystem::current_path();
		while (!probe.empty())
		{
			const std::filesystem::path candidate = probe / resolvedFilePath;
			if (std::filesystem::exists(candidate))
			{
				resolvedFilePath = candidate.lexically_normal();
				break;
			}

			const std::filesystem::path parent = probe.parent_path();
			if (parent == probe)
				break;
			probe = parent;
		}
	}

	auto existingMaterialIt = SkyMaterialByTexturePath.find(resolvedPath);
	if (existingMaterialIt != SkyMaterialByTexturePath.end())
	{
		auto materialIt = Materials.find(existingMaterialIt->second);
		if (materialIt != Materials.end() && materialIt->second.DiffuseTexture != nullptr)
		{
			SkyTexHeapIndex = materialIt->second.DiffuseTexture->GetIndex();
			SkyMapIndex = SkyTexHeapIndex;
		}
		return existingMaterialIt->second;
	}

	if (!std::filesystem::exists(resolvedFilePath))
	{
		SkyTexHeapIndex = TextureGroups[L"skyMap"][0].GetIndex();
		SkyMapIndex = SkyTexHeapIndex;
		return L"sky";
	}

	const std::wstring baseName = std::filesystem::path(resolvedPath).stem().wstring();
	const std::wstring materialName = MakeUniqueName(Materials, baseName.empty() ? L"sky" : (L"sky_" + baseName));
	const std::wstring textureGroupName = materialName + L"_TextureGroup";

	ResourceUploadBatch resourceUpload(d3dDevice.Get());
	resourceUpload.Begin();

	Texture skyTexture(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		materialName + L"_SkyTexture",
		resolvedFilePath.wstring(),
		ResolveTextureTypeFromPath(resolvedPath),
		SrvDescriptorHeapIndex);
	++SrvDescriptorHeapIndex;

	auto uploadResourcesFinished = resourceUpload.End(CommandQueue.Get());
	uploadResourcesFinished.wait();

	TextureGroups[textureGroupName].resize(1);
	TextureGroups[textureGroupName][0] = skyTexture;

	Material material;
	material.SetName(materialName);
	material.MatCBIndex = static_cast<int>(Materials.size());
	material.DiffuseTexture = &TextureGroups[textureGroupName][0];
	material.MatTransform = MathHelps::Identity;
	material.NumFramesDirty = SwapChainBufferCount;

	Materials[materialName] = material;
	SkyMaterialByTexturePath[resolvedPath] = materialName;
	SkyTexHeapIndex = TextureGroups[textureGroupName][0].GetIndex();
	SkyMapIndex = SkyTexHeapIndex;
	FreshenMaterialCBs();

	return materialName;
}

Material* D3DWindow::GetMaterialByRuntimeMaterialName(const std::wstring& runtimeMaterialName)
{
	auto materialIt = Materials.find(runtimeMaterialName);
	if (materialIt == Materials.end())
		return nullptr;

	return &materialIt->second;
}

const Material* D3DWindow::GetMaterialByRuntimeMaterialName(const std::wstring& runtimeMaterialName) const
{
	auto materialIt = Materials.find(runtimeMaterialName);
	if (materialIt == Materials.end())
		return nullptr;

	return &materialIt->second;
}

void D3DWindow::NotifyRuntimeMaterialChanged(const std::wstring& runtimeMaterialName)
{
	auto materialIt = Materials.find(runtimeMaterialName);
	if (materialIt == Materials.end())
		return;

	materialIt->second.NumFramesDirty = SwapChainBufferCount;
	FreshenMaterialCBs();
}

std::vector<std::wstring> D3DWindow::GetMaterialNameList()
{
	std::vector<std::wstring> NameList(Materials.size());
	UINT i = 0;
	for (auto mat = Materials.begin(); mat != Materials.end(); mat++)
	{
		NameList[i] = mat->first;
		i++;
	}
	return NameList;
}

ID3D12Resource* D3DWindow::GetRenderTargetBuffer()
{
	return SwapChainBuffer->Get();
}

ID3D12Resource* D3DWindow::GetDepthStencilBuffer()
{
	return DepthStencilBuffer.Get();
}

ID3D12Device* D3DWindow::GetDevice()
{
	return d3dDevice.Get();
}

IDXGISwapChain* D3DWindow::GetSwapChain()
{
	return SwapChain.Get();
}

ID3D12CommandQueue* D3DWindow::GetCommandQueue()
{
	return CommandQueue.Get();
}

ID3D12GraphicsCommandList* D3DWindow::GetCommandList()
{
	return MainCommandList.Get();
}

ID3D12GraphicsCommandList* D3DWindow::GetThreadCommandList(int threadIndex)
{
	return CurrFrameResource->threadCommandLists[threadIndex].Get();
}

ID3D12CommandAllocator* D3DWindow::GetWorkerCommandAllocator(UINT passIndex, int threadIndex)
{
	switch (passIndex)
	{
	case 阴影工作阶段:
		return CurrFrameResource->shadowThreadCommandAllocators[threadIndex].Get();
	case 法线工作阶段:
		return CurrFrameResource->normalThreadCommandAllocators[threadIndex].Get();
	case 不透明工作阶段:
	default:
		return CurrFrameResource->threadCommandAllocators[threadIndex].Get();
	}
}

ID3D12GraphicsCommandList* D3DWindow::GetWorkerCommandList(UINT passIndex, int threadIndex)
{
	switch (passIndex)
	{
	case 0:
		return CurrFrameResource->shadowThreadCommandLists[threadIndex].Get();
	case 2:
		return CurrFrameResource->normalThreadCommandLists[threadIndex].Get();
	case 1:
	default:
		return CurrFrameResource->threadCommandLists[threadIndex].Get();
	}
}

void D3DWindow::EnsureShadowMapResources(UINT requiredShadowMapCount)
{
	requiredShadowMapCount = (std::min)(requiredShadowMapCount, ShadowConfig.MaxShadowMapCount);
	if (shadowMap.GetHeapIndexSize() == requiredShadowMapCount)
		return;

	FlushCommandQueue();
	shadowMap.Clear();
	if (requiredShadowMapCount == 0)
		return;

	shadowMap.OnResize(ShadowConfig.ShadowMapSize, ShadowConfig.ShadowMapSize);

	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());
	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT shadowIndex = 0; shadowIndex < requiredShadowMapCount; ++shadowIndex)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle(dsvHandle, 1 + shadowIndex, DsvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE shadowSrvHandle(srvHandle, ShadowMapHeapStartIndex + shadowIndex, CbvSrvUavDescriptorSize);
		shadowMap.AddShadowMap(L"shadowmap" + std::to_wstring(shadowIndex), DepthStencilFormat, shadowDsvHandle, shadowSrvHandle, ShadowMapHeapStartIndex + shadowIndex);
	}
}

void D3DWindow::BeginWorkerPass(UINT passIndex)
{
	// 通过对应 pass 的事件直接唤醒工作线程，避免额外共享“当前阶段”状态。
	for (UINT i = 0; i < NumContexts; ++i)
	{
		ResetEvent(workerFinishedRecordCommand[i]);
		SetEvent(workerBeginRecordCommand[passIndex][i]);
	}
}

void D3DWindow::WaitForWorkerPass()
{
	// 等待所有工作线程结束本阶段录制，之后主线程再统一提交命令列表。
	WaitForMultipleObjects(NumContexts, workerFinishedRecordCommand, TRUE, INFINITE);
}

ID3D12GraphicsCommandList* D3DWindow::GetCurrFrameResourceCommandList()
{
	return CurrFrameResource->EndCommandList.Get();
}

CD3DX12_VIEWPORT D3DWindow::GetViewport()
{
	return m_viewport;
}

void D3DWindow::BegineThread()
{
	struct threadwrapper
	{
		static unsigned int WINAPI thunk(LPVOID lpParameter)
		{
			ThreadParameter* parameter = reinterpret_cast<ThreadParameter*>(lpParameter);
			s_app->WorkerThread(parameter->threadIndex);
			return 0;
		}
	};

	// 子线程现在按工作阶段分段工作。
	for (int i = 0; i < NumContexts; i++)
	{
		for (UINT passIndex = 0; passIndex < 工作阶段计数; ++passIndex)
		{
			workerBeginRecordCommand[passIndex][i] = CreateAutoResetEventHandle();
		}

		workerFinishedRecordCommand[i] = CreateManualResetEventHandle();

		threadParameters[i].threadIndex = i;

		threadHandles[i] = reinterpret_cast<HANDLE>(_beginthreadex(
			nullptr,
			0,
			threadwrapper::thunk,
			reinterpret_cast<LPVOID>(&threadParameters[i]),
			0,
			nullptr));

		for (UINT passIndex = 0; passIndex < 工作阶段计数; ++passIndex)
			assert(workerBeginRecordCommand[passIndex][i] != NULL);
		assert(workerFinishedRecordCommand[i] != NULL);
		assert(threadHandles[i] != NULL);
	}
}

void D3DWindow::UpdateCamera()
{
	mCamera.UpdateViewMatrix();
}

void D3DWindow::UpdateObjectCBs()
{
	auto currObjectCB = CurrFrameResource->ObjectCB.get();

	if (FreshenAllObject)
	{
		DirtyObjectCBItems.clear();
		for (auto& renderItemPair : AllRitems)
		{
			renderItemPair.second.NumFramesDirty = SwapChainBufferCount;
			DirtyObjectCBItems.insert(renderItemPair.first);
		}
		FreshenAllObject = false;
	}

	if (DirtyObjectCBItems.empty())
		return;

	std::unordered_set<std::wstring> nextDirtyItems;
	nextDirtyItems.reserve(DirtyObjectCBItems.size());

	for (const std::wstring& renderItemName : DirtyObjectCBItems)
	{
		auto renderItemIt = AllRitems.find(renderItemName);
		if (renderItemIt == AllRitems.end())
			continue;

		RenderItem& renderItem = renderItemIt->second;
		if (renderItem.NumFramesDirty > 0)
		{
			XMMATRIX WorldTransform = XMLoadFloat4x4(&renderItem.WorldTransform);
			XMMATRIX texTransform = XMLoadFloat4x4(&renderItem.TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.WorldTransform, XMMatrixTranspose(WorldTransform));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(renderItem.ObjCBIndex, objConstants);

			// 下一帧资源也需要更新。
			renderItem.NumFramesDirty--;
			if (renderItem.NumFramesDirty > 0)
				nextDirtyItems.insert(renderItemName);
		}
	}

	DirtyObjectCBItems.swap(nextDirtyItems);
}

void D3DWindow::FreshenObjectCBs()
{
	FreshenAllObject = true;
}

void D3DWindow::FreshenObjectCBs(const std::wstring& renderItemName)
{
	auto renderItemIt = AllRitems.find(renderItemName);
	if (renderItemIt == AllRitems.end())
		return;

	renderItemIt->second.NumFramesDirty = SwapChainBufferCount;
	DirtyObjectCBItems.insert(renderItemName);
}

void D3DWindow::UpdateMaterialCBs()
{
	auto currMaterialCB = CurrFrameResource->MaterialCB.get();

	for (auto& M : Materials)
	{
		Material* Mat = &M.second;
		if (Mat->NumFramesDirty > 0 && Mat->MatCBIndex!=-1)
		{
			currMaterialCB->CopyData(Mat->MatCBIndex, Mat->Properties);

			Mat->NumFramesDirty--;
		}
	}
	FreshenAllMaterial = false;
}

void D3DWindow::FreshenMaterialCBs()
{
	FreshenAllMaterial = true;
}

void D3DWindow::UpdateLightCBs()
{
	auto currLightCB = CurrFrameResource->LightCB.get();

	LightConstants lightConstants = {};
	lightConstants.AmbientColor = AmbientColor;

	const UINT maxLightCount = _countof(lightConstants.Lights);
	const UINT directionalLightType = static_cast<UINT>(std::lround(ShadowConfig.DirectionalLightType));
	const UINT pointLightType = static_cast<UINT>(std::lround(ShadowConfig.PointLightType));
	const UINT spotLightType = static_cast<UINT>(std::lround(ShadowConfig.SpotLightType));

	RuntimeLightsCache.clear();
	RuntimeLightsCache.reserve((std::min)(static_cast<UINT>(Lights.size()), maxLightCount));
	RotatedLightDirections.clear();
	RotatedLightDirections.reserve((std::min)(static_cast<UINT>(Lights.size()), maxLightCount));

	UINT lightCount = 0;
	UINT shadowCastingCount = 0;
	for (auto& lightPair : Lights)
	{
		if (lightCount >= maxLightCount)
			break;

		Light& light = lightPair.second;
		if (FreshenAllLight)
			light.NumFramesDirty = SwapChainBufferCount;
		else if (light.NumFramesDirty > 0)
			light.NumFramesDirty--;

		RuntimeLightsCache.push_back(light);
		RotatedLightDirections.push_back(light.Direction);

		LightData& lightData = lightConstants.Lights[lightCount];
		lightData.Type = light.Type;
		lightData.Color = light.Color;
		lightData.Direction = light.Direction;
		lightData.Position = light.Position;
		lightData.Power = light.Power;
		lightData.ShadowMapIndex = -1.0f;

		const UINT resolvedLightType = static_cast<UINT>(std::lround(light.Type));
		const bool isShadowSupportedLight =
			resolvedLightType == directionalLightType ||
			resolvedLightType == pointLightType ||
			resolvedLightType == spotLightType;
		if (light.CastShadow && isShadowSupportedLight && shadowCastingCount < ShadowConfig.MaxShadowMapCount)
		{
			lightData.ShadowMapIndex = static_cast<float>(shadowCastingCount);
			++shadowCastingCount;
		}

		++lightCount;
	}

	MainPassCB.LightConst = lightCount;
	EnsureShadowMapResources(shadowCastingCount);
	currLightCB->CopyData(0, lightConstants);
	FreshenAllLight = false;
}

void D3DWindow::FreshenLightCBs()
{
	FreshenAllLight = true;
}

void D3DWindow::UpdateMainPassCB()
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMVECTOR DeterminantView(XMMatrixDeterminant(view));
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&DeterminantView, view);
	XMVECTOR DeterminantProj(XMMatrixDeterminant(proj));
	XMMATRIX invProj = XMMatrixInverse(&DeterminantProj, proj);
	XMVECTOR DeterminantViewProj(XMMatrixDeterminant(viewProj));
	XMMATRIX invViewProj = XMMatrixInverse(&DeterminantViewProj, viewProj);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	DirectX::XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	DirectX::XMMATRIX viewProjTex = DirectX::XMMatrixMultiply(viewProj, T);

	XMStoreFloat4x4(&MainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&MainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&MainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&MainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&MainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&MainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&MainPassCB.ViewProjTex, XMMatrixTranspose(viewProjTex));
	for (UINT i = 0; i < MainPassCB.LightConst; i++)
		XMStoreFloat4x4(&MainPassCB.ShadowTransform[i], XMMatrixTranspose(XMLoadFloat4x4(&ShadowTransform[i])));
	MainPassCB.EyePosW = mCamera.GetPosition3f();
	MainPassCB.RenderTargetSize = XMFLOAT2(
		(float)Width,
		(float)Height);
	MainPassCB.InvRenderTargetSize = XMFLOAT2(
		1.0f / (float)Width,
		1.0f / (float)Height);

	auto currPassCB = CurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, MainPassCB);
}

void D3DWindow::UpdateShadowTransform()
{
	BoundingSphere sceneBounds;
	sceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	sceneBounds.Radius = sqrtf(100.0f * 100.0f + 64.0f * 64.0f);

	ShadowTransform.assign(MainPassCB.LightConst, MathHelps::Identity);
	ShadowRenderEntries.clear();
	if (MainPassCB.LightConst == 0 || RuntimeLightsCache.empty() || shadowMap.GetHeapIndexSize() == 0)
		return;

	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	const int directionalLightType = static_cast<int>(std::lround(ShadowConfig.DirectionalLightType));
	const int pointLightType = static_cast<int>(std::lround(ShadowConfig.PointLightType));
	const int spotLightType = static_cast<int>(std::lround(ShadowConfig.SpotLightType));
	const XMVECTOR sceneCenter = XMLoadFloat3(&sceneBounds.Center);

	for (UINT lightIndex = 0; lightIndex < RuntimeLightsCache.size() && ShadowRenderEntries.size() < shadowMap.GetHeapIndexSize(); ++lightIndex)
	{
		const Light& light = RuntimeLightsCache[lightIndex];
		if (!light.CastShadow)
			continue;

		const int resolvedLightType = static_cast<int>(std::lround(light.Type));
		if (resolvedLightType != directionalLightType && resolvedLightType != pointLightType && resolvedLightType != spotLightType)
			continue;

		XMVECTOR lightDir = XMLoadFloat3(&light.Direction);
		if (XMVector3Equal(lightDir, XMVectorZero()))
			lightDir = XMLoadFloat3(&ShadowConfig.FallbackDirection);
		lightDir = XMVector3Normalize(lightDir);

		XMVECTOR lightUp = XMLoadFloat3(&light.Up);
		if (XMVector3Equal(lightUp, XMVectorZero()))
			lightUp = XMLoadFloat3(&ShadowConfig.FallbackUp);
		lightUp = XMVector3Normalize(lightUp);

		XMVECTOR lightPos = XMLoadFloat3(&light.Position);
		XMVECTOR targetPos = sceneCenter;
		XMMATRIX lightView = XMMatrixIdentity();
		XMMATRIX lightProj = XMMatrixIdentity();

		if (resolvedLightType == directionalLightType)
		{
			const float shadowMapSize = static_cast<float>(ShadowConfig.ShadowMapSize);
			const float worldUnitsPerTexel = (2.0f * sceneBounds.Radius) / shadowMapSize;
			const float orthographicPadding = worldUnitsPerTexel * 4.0f;
			const float depthPadding = worldUnitsPerTexel * 16.0f;

			targetPos = sceneCenter;
			lightPos = targetPos - 2.0f * sceneBounds.Radius * lightDir;
			if (std::abs(XMVectorGetX(XMVector3Dot(lightDir, lightUp))) > 0.99f)
			{
				XMVECTOR fallbackUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
				if (std::abs(XMVectorGetX(XMVector3Dot(lightDir, fallbackUp))) > 0.99f)
					fallbackUp = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
				lightUp = fallbackUp;
			}

			lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

			XMFLOAT3 sphereCenterLS;
			XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));
			sphereCenterLS.x = std::floor((sphereCenterLS.x / worldUnitsPerTexel) + 0.5f) * worldUnitsPerTexel;
			sphereCenterLS.y = std::floor((sphereCenterLS.y / worldUnitsPerTexel) + 0.5f) * worldUnitsPerTexel;

			const float l = sphereCenterLS.x - sceneBounds.Radius - orthographicPadding;
			const float b = sphereCenterLS.y - sceneBounds.Radius - orthographicPadding;
			const float n = sphereCenterLS.z - sceneBounds.Radius - depthPadding;
			const float r = sphereCenterLS.x + sceneBounds.Radius + orthographicPadding;
			const float t = sphereCenterLS.y + sceneBounds.Radius + orthographicPadding;
			const float f = sphereCenterLS.z + sceneBounds.Radius + depthPadding;

			lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);
		}
		else
		{
			if (resolvedLightType == spotLightType)
			{
				targetPos = lightPos + lightDir * (sceneBounds.Radius + 1.0f);
			}
			else
			{
				XMVECTOR toSceneCenter = sceneCenter - lightPos;
				if (!XMVector3Equal(toSceneCenter, XMVectorZero()))
					targetPos = sceneCenter;
				else
					targetPos = lightPos + lightDir * (sceneBounds.Radius + 1.0f);
			}

			XMVECTOR lightForward = XMVector3Normalize(targetPos - lightPos);
			if (std::abs(XMVectorGetX(XMVector3Dot(lightForward, lightUp))) > 0.99f)
			{
				XMVECTOR fallbackUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
				if (std::abs(XMVectorGetX(XMVector3Dot(lightForward, fallbackUp))) > 0.99f)
					fallbackUp = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
				lightUp = fallbackUp;
			}

			lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

			const float distanceToTarget = XMVectorGetX(XMVector3Length(targetPos - lightPos));
			const float nearZ = 0.1f;
			const float farZ = (std::max)(distanceToTarget + 2.0f * sceneBounds.Radius, nearZ + 1.0f);
			const float fovY = resolvedLightType == pointLightType ? XM_PIDIV2 : XM_PIDIV4;
			lightProj = XMMatrixPerspectiveFovLH(fovY, 1.0f, nearZ, farZ);
		}

		ShadowRenderEntry entry;
		entry.LightIndex = lightIndex;
		XMStoreFloat4x4(&entry.View, lightView);
		XMStoreFloat4x4(&entry.Proj, lightProj);
		XMStoreFloat4x4(&entry.Transform, lightView * lightProj * T);
		XMStoreFloat3(&entry.LightPosition, lightPos);

		ShadowTransform[lightIndex] = entry.Transform;
		ShadowRenderEntries.push_back(entry);
	}
}

void D3DWindow::UpdateShadowPassCB()
{
	auto currPassCB = CurrFrameResource->PassCB.get();
	const float shadowMapSize = static_cast<float>(ShadowConfig.ShadowMapSize);

	for (UINT shadowIndex = 0; shadowIndex < ShadowRenderEntries.size(); ++shadowIndex)
	{
		const ShadowRenderEntry& entry = ShadowRenderEntries[shadowIndex];
		PassConstants shadowPassCB = {};

		XMMATRIX view = XMLoadFloat4x4(&entry.View);
		XMMATRIX proj = XMLoadFloat4x4(&entry.Proj);
		XMMATRIX viewProj = XMMatrixMultiply(view, proj);
		XMVECTOR determinantView = XMMatrixDeterminant(view);
		XMVECTOR determinantProj = XMMatrixDeterminant(proj);
		XMVECTOR determinantViewProj = XMMatrixDeterminant(viewProj);
		XMMATRIX invView = XMMatrixInverse(&determinantView, view);
		XMMATRIX invProj = XMMatrixInverse(&determinantProj, proj);
		XMMATRIX invViewProj = XMMatrixInverse(&determinantViewProj, viewProj);

		XMStoreFloat4x4(&shadowPassCB.View, XMMatrixTranspose(view));
		XMStoreFloat4x4(&shadowPassCB.InvView, XMMatrixTranspose(invView));
		XMStoreFloat4x4(&shadowPassCB.Proj, XMMatrixTranspose(proj));
		XMStoreFloat4x4(&shadowPassCB.InvProj, XMMatrixTranspose(invProj));
		XMStoreFloat4x4(&shadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
		XMStoreFloat4x4(&shadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
		shadowPassCB.EyePosW = entry.LightPosition;
		shadowPassCB.RenderTargetSize = XMFLOAT2(shadowMapSize, shadowMapSize);
		shadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / shadowMapSize, 1.0f / shadowMapSize);
		shadowPassCB.ShadowSettings = MainPassCB.ShadowSettings;
		shadowPassCB.LightConst = MainPassCB.LightConst;

		currPassCB->CopyData(1 + shadowIndex, shadowPassCB);
	}
}

void D3DWindow::UpdateAOCB()
{
	AOConstants AOCB;

	XMMATRIX P = mCamera.GetProj();

	// 将裁剪空间坐标映射到纹理空间，供 AO shader 进行屏幕空间采样。
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	AOCB.Proj = MainPassCB.Proj;
	AOCB.InvProj = MainPassCB.InvProj;
	XMStoreFloat4x4(&AOCB.ProjTex, XMMatrixTranspose(P * T));

	// AO 采样核方向由 AmbientOcclusion 统一生成，这里只把结果拷到常量缓冲。
	ambientOcclusion.GetOffsetVectors(AOCB.OffsetVectors);

	float sigma = 2.5f;
	UINT MaxBlurRadius = 5;
	float twoSigma2 = 2.0f * sigma * sigma;

	// 根据 sigma 生成高斯权重，供后续 AO 模糊 pass 使用。
	int blurRadius = (int)ceil(2.0f * sigma);

	assert(blurRadius <= MaxBlurRadius);

	std::vector<float> blurWeights;
	blurWeights.resize(2 * blurRadius + 1);

	float weightSum = 0.0f;

	for (int i = -blurRadius; i <= blurRadius; ++i)
	{
		float x = (float)i;
		blurWeights[i + blurRadius] = expf(-x * x / twoSigma2);
		weightSum += blurWeights[i + blurRadius];
	}

	// 归一化后总权重为 1，避免模糊结果整体变亮或变暗。
	for (int i = 0; i < blurWeights.size(); ++i)
	{
		blurWeights[i] /= weightSum;
	}

	AOCB.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
	AOCB.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
	AOCB.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

	AOCB.InvRenderTargetSize = XMFLOAT2(1.0f / Width, 1.0f / Height);

	// 当前场景尺寸下半径过大时，AO 会在轮廓周围产生过宽的暗边；
	// 同时把淡出距离拉回更合理的范围，减少“贴着物体外轮廓发黑”的感觉。
	AOCB.OcclusionRadius = 0.3f;
	AOCB.OcclusionFadeStart = 0.2f;
	AOCB.OcclusionFadeEnd = 2.0f;
	AOCB.SurfaceEpsilon = 0.02f;

	auto currSsaoCB = CurrFrameResource->AOCB.get();
	currSsaoCB->CopyData(0, AOCB);
}

void D3DWindow::Update()
{
	BOOL isFull = false;
	ThrowIfFailed(SwapChain->GetFullscreenState(&isFull, nullptr));
	if (fullscreenState != isFull)
		ThrowIfFailed(SwapChain->SetFullscreenState(fullscreenState, nullptr));

	UpdateCamera();

	// 以交换链的真实 current back buffer 为准，避免手动递增与实际呈现顺序失同步。
	CurrBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();

	// 循环遍历图形框架资源数组。
	CurrFrameResource = &mFrameResources[CurrBackBufferIndex];

	// GPU是否已完成对当前帧资源的命令的处理？
	// 如果没有，请等到GPU完成命令直到该防护点为止。
	if (CurrFrameResource->Fence != 0 && fence->GetCompletedValue() < CurrFrameResource->Fence)
	{
		ThrowIfFailed(fence->SetEventOnCompletion(CurrFrameResource->Fence, fenceEvent));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	UpdateObjectCBs();
	UpdateMaterialCBs();
	UpdateLightCBs();
	UpdateShadowTransform();
	UpdateMainPassCB();
	UpdateShadowPassCB();
	UpdateAOCB();
}

void D3DWindow::RenderB()
{
	const bool hasSkyRenderItems = !RitemLayer[天空渲染项目].empty();
	const bool hasOpaqueRenderItems = !RitemLayer[不透明物体渲染项目].empty();

	// 重用与命令记录相关的内存。
	// 只有当关联的命令列表在 GPU 上执行完毕后，
	// 我们才能重置，不进行重置则会导致内存溢出。
	ThrowIfFailed(CurrFrameResource->BeginCommandAllocator->Reset());
	ThrowIfFailed(CurrFrameResource->MidCommandAllocator->Reset());
	ThrowIfFailed(CurrFrameResource->EndCommandAllocator->Reset());

	// 命令列表可以在通过ExecuteCommandList添加到命令队列后重置。
	// 重复使用命令列表会重复使用内存。
	ThrowIfFailed(CurrFrameResource->BeginCommandList->Reset(CurrFrameResource->BeginCommandAllocator.Get(), nullptr));
	ThrowIfFailed(CurrFrameResource->MidCommandLidt->Reset(CurrFrameResource->MidCommandAllocator.Get(), nullptr));
	// 重置线程工作命令分配器和列表。
	ThrowIfFailed(CurrFrameResource->EndCommandList->Reset(CurrFrameResource->EndCommandAllocator.Get(), nullptr));

	// 指示资源使用情况的状态转换。
	D3D12_RESOURCE_BARRIER Barriers;
	if (hasOpaqueRenderItems && shadowMap.GetHeapIndexSize() > 0)
	{
		std::vector<D3D12_RESOURCE_BARRIER> shadowBarriers;
		shadowBarriers.reserve(shadowMap.GetHeapIndexSize());
		for (UINT shadowIndex = 0; shadowIndex < shadowMap.GetHeapIndexSize(); ++shadowIndex)
		{
			auto shadowResource = shadowMap.GetResource(shadowIndex);
			if (shadowResource != nullptr)
			{
				shadowBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(shadowResource.Get(),
					D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));
			}
		}
		if (!shadowBarriers.empty())
			CurrFrameResource->BeginCommandList->ResourceBarrier(static_cast<UINT>(shadowBarriers.size()), shadowBarriers.data());
	}

	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CurrFrameResource->BeginCommandList->ResourceBarrier(1, &Barriers);

	if (hasSkyRenderItems)
	{
		Barriers = CD3DX12_RESOURCE_BARRIER::Transition(mFrameResources[CurrBackBufferIndex].mCopyTexture.Get(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		CurrFrameResource->MidCommandLidt->ResourceBarrier(1, &Barriers);
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());
	const float clearColor[] = { 0.120f, 0.345f, 0.935f, 1.00f };
	CurrFrameResource->BeginCommandList->ClearRenderTargetView(rtvHandle, (float*)&clearColor, 0, nullptr);
	CurrFrameResource->BeginCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	{
		skyTexDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
		skyTexDescriptor.Offset(SkyTexHeapIndex, CbvSrvUavDescriptorSize);

		otherTexDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

		//otherTexDescriptor.Offset(mSkyTexHeapIndex + 1, CbvSrvUavDescriptorSize);

		shadowMapDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
		shadowMapDescriptor.Offset(ShadowMapHeapStartIndex, CbvSrvUavDescriptorSize);
		ambientOcclusionDescriptor = ambientOcclusion.mhAmbientMap0GpuSrv;
	}

	if (hasSkyRenderItems)
	{
		auto midCommandList = CurrFrameResource->MidCommandLidt.Get();
		ID3D12DescriptorHeap* descriptorHeaps[] = { SrvDescriptorHeap.Get() };

		// 中段命令列表负责主线程固定通道：天空、共享根参数等。
		midCommandList->SetGraphicsRootSignature(RootSignature.Get());
		midCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
		midCommandList->SetGraphicsRootConstantBufferView(1, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
		midCommandList->SetGraphicsRootConstantBufferView(2, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
		midCommandList->SetGraphicsRootDescriptorTable(4, skyTexDescriptor);
		midCommandList->SetGraphicsRootDescriptorTable(5, otherTexDescriptor);
		midCommandList->SetGraphicsRootDescriptorTable(6, shadowMapDescriptor);
		midCommandList->SetGraphicsRootDescriptorTable(7, ambientOcclusionDescriptor);
		midCommandList->RSSetViewports(1, &m_viewport);
		midCommandList->RSSetScissorRects(1, &m_scissorRect);
		midCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
		const std::vector<RenderItem*> skyRenderItems = CollectRenderItems(RitemLayer[天空渲染项目]);
		DrawRenderItems(midCommandList, skyRenderItems, PipelineState[天空管道], 天空管道);
	}

	// 处理文字部分
	if (renderFPS)
	{
		static int frameCnt = 0;
		static float timeElapsed = 0.0f;
		frameCnt++;

		// 调试文字继续按 1 秒一次刷新，避免每帧都重建 FPS 字符串。
		if ((mTimer->TotalTime() - timeElapsed) >= 1.0f)
		{
			float fps = (float)frameCnt; // fps = frameCnt / 1
			float mspf = 1000.0f / fps;

			std::wstring fpsStr = std::to_wstring(fps);
			std::wstring mspfStr = std::to_wstring(mspf);

			Text =
				L"帧率：" + fpsStr +
				L"   MSPF: " + mspfStr + L'\n';

			// Reset for next average.
			frameCnt = 0;
			timeElapsed += 1.0f;
		}
	}

	ThrowIfFailed(CurrFrameResource->BeginCommandList->Close());
	ThrowIfFailed(CurrFrameResource->MidCommandLidt->Close());
}

void D3DWindow::RenderE()
{
	const bool hasSkyRenderItems = !RitemLayer[天空渲染项目].empty();
	const bool hasOpaqueRenderItems = !RitemLayer[不透明物体渲染项目].empty();

	// Begin / Mid / Worker / End 四类命令列表按阶段串接提交。
	ID3D12CommandList* beginCommandLists[] =
	{
		CurrFrameResource->BeginCommandList.Get()
	};
	CommandQueue->ExecuteCommandLists(_countof(beginCommandLists), beginCommandLists);

	if (hasSkyRenderItems)
	{
		ID3D12CommandList* skyCommandLists[] =
		{
			CurrFrameResource->MidCommandLidt.Get()
		};
		CommandQueue->ExecuteCommandLists(_countof(skyCommandLists), skyCommandLists);
	}
	if (hasOpaqueRenderItems)
	{
		BeginWorkerPass(阴影工作阶段);
		WaitForWorkerPass();
		{
			// 阴影 pass 由多个线程分别录制，再批量提交。
			ID3D12CommandList* shadowCommandLists[NumContexts] = { nullptr };
			for (UINT i = 0; i < NumContexts; ++i)
			{
				shadowCommandLists[i] = CurrFrameResource->shadowThreadCommandLists[i].Get();
			}
			CommandQueue->ExecuteCommandLists(_countof(shadowCommandLists), shadowCommandLists);
		}

		BeginWorkerPass(不透明工作阶段);
		WaitForWorkerPass();
		// MidCommandList 在上面的天空阶段已经单独提交过；
		// 这里不能再次执行同一份命令列表，否则会触发 COMMAND_LIST_SYNC，
		// 并且 CopyTexture 的状态也会因为重复执行同一条 barrier 而失配。
		ID3D12CommandList* opaqueCommandLists[NumContexts] = { nullptr };
		for (UINT i = 0; i < NumContexts; ++i)
		{
			opaqueCommandLists[i] = CurrFrameResource->threadCommandLists[i].Get();
		}
		CommandQueue->ExecuteCommandLists(_countof(opaqueCommandLists), opaqueCommandLists);

		BeginWorkerPass(法线工作阶段);
		WaitForWorkerPass();
	}

	auto endCommandList = CurrFrameResource->EndCommandList.Get();
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());
	ID3D12DescriptorHeap* descriptorHeaps[] = { SrvDescriptorHeap.Get() };

	endCommandList->SetGraphicsRootSignature(RootSignature.Get());
	endCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	endCommandList->SetGraphicsRootConstantBufferView(1, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
	endCommandList->SetGraphicsRootConstantBufferView(2, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
	endCommandList->SetGraphicsRootDescriptorTable(4, skyTexDescriptor);
	endCommandList->SetGraphicsRootDescriptorTable(5, otherTexDescriptor);
	endCommandList->SetGraphicsRootDescriptorTable(6, shadowMapDescriptor);
	endCommandList->SetGraphicsRootDescriptorTable(7, ambientOcclusionDescriptor);
	endCommandList->RSSetViewports(1, &m_viewport);
	endCommandList->RSSetScissorRects(1, &m_scissorRect);
	endCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	const std::vector<RenderItem*> debugRenderItems = CollectRenderItems(RitemLayer[debugrt]);
	DrawRenderItems(endCommandList, debugRenderItems, debugPipelineState, 不透明物体管道);

	if (hasOpaqueRenderItems)
	{
		// 法线图由法线 pass 写入，深度图由主深度缓冲提供；AO pass 开始前都要切到可采样状态。
		D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(ambientOcclusion.NormalMap().Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
		endCommandList->ResourceBarrier(1, &Barriers);

		Barriers = CD3DX12_RESOURCE_BARRIER::Transition(DepthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		endCommandList->ResourceBarrier(1, &Barriers);

		endCommandList->SetGraphicsRootSignature(ambientOcclusion.GetRootSignature().Get());

		// AmbientMap 是 AO 结果贴图，本 pass 会把全屏采样结果写入这里。
		Barriers = CD3DX12_RESOURCE_BARRIER::Transition(ambientOcclusion.AmbientMap().Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
		endCommandList->ResourceBarrier(1, &Barriers);

		ambientOcclusion.SetViewports(CurrFrameResource->EndCommandList);

		float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		endCommandList->ClearRenderTargetView(ambientOcclusion.mhAmbientMap0CpuRtv, clearColor, 0, nullptr);

		ambientOcclusion.SetRenderTargets(CurrFrameResource->EndCommandList);

		auto ssaoCBAddress = CurrFrameResource->AOCB->Resource()->GetGPUVirtualAddress();
		endCommandList->SetGraphicsRootConstantBufferView(0, ssaoCBAddress);
		endCommandList->SetGraphicsRoot32BitConstant(1, 0, 0);
		endCommandList->SetGraphicsRootDescriptorTable(2, ambientOcclusion.mhNormalMapGpuSrv);
		endCommandList->SetGraphicsRootDescriptorTable(3, ambientOcclusion.mhRandomVectorMapGpuSrv);
		endCommandList->SetPipelineState(PipelineState[环境遮蔽管道].Get());
		
		// AO 使用全屏三角形/四边形式绘制，不依赖场景网格顶点缓冲。
		endCommandList->IASetVertexBuffers(0, 0, nullptr);
		endCommandList->IASetIndexBuffer(nullptr);
		endCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		endCommandList->DrawInstanced(6, 1, 0, 0);

		// AO 结果写完后先切回可读状态，随后做一次双边模糊，减少条带与轮廓锯齿。
		Barriers = CD3DX12_RESOURCE_BARRIER::Transition(ambientOcclusion.AmbientMap().Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
		endCommandList->ResourceBarrier(1, &Barriers);

		endCommandList->SetPipelineState(PipelineState[遮蔽模糊管道].Get());
		ambientOcclusion.SetViewports(CurrFrameResource->EndCommandList);
		ambientOcclusion.BlurAmbientMap(CurrFrameResource->EndCommandList, true);
		ambientOcclusion.BlurAmbientMap(CurrFrameResource->EndCommandList, false);

		Barriers = CD3DX12_RESOURCE_BARRIER::Transition(DepthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		endCommandList->ResourceBarrier(1, &Barriers);
	}

	endCommandList->RSSetViewports(1, &m_viewport);
	endCommandList->RSSetScissorRects(1, &m_scissorRect);
	endCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	if (renderFPS)
	{
		endCommandList->SetPipelineState(PipelineState[文字管道].Get());
		textR->DXDrawText(endCommandList, Text, DirectX::XMFLOAT2(0.32f, 0.25f), DirectX::XMFLOAT4{ 1.0f,1.0f,1.0f,1.0f }, CurrBackBufferIndex);
	}

	if (mEditor)
		mEditor->Render();

	D3D12_RESOURCE_BARRIER Barriers = {};
	if (hasSkyRenderItems)
	{
		Barriers = CD3DX12_RESOURCE_BARRIER::Transition(mFrameResources[CurrBackBufferIndex].mCopyTexture.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		endCommandList->ResourceBarrier(1, &Barriers);
	}

	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(SwapChainBuffer[CurrBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	endCommandList->ResourceBarrier(1, &Barriers);

	ThrowIfFailed(endCommandList->Close());

	if (hasOpaqueRenderItems)
	{
		// 法线 pass 的线程命令列表与 EndCommandList 一起提交，保证 AO 读取到的是本帧最新法线图。
		ID3D12CommandList* commandLists[NumContexts + 1] = { nullptr };
		for (UINT i = 0; i < NumContexts; i++)
		{
			commandLists[i] = CurrFrameResource->normalThreadCommandLists[i].Get();
		}
		commandLists[NumContexts] = endCommandList;
		CommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
	}
	else
	{
		ID3D12CommandList* commandLists[] = { endCommandList };
		CommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
	}

	SwapChain->Present(0, 0);
	CurrBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();

	// 提升围栏值以将命令标记到该围栏点。
	CurrFrameResource->Fence = ++fenceValue;

	// 将指令添加到命令队列以设置新的围栏点。
	// 因为我们在GPU的时间线上，所以在GPU完成此Signal（）之前的所有命令处理之前，
	// 不会设置新的围栏点。
	ThrowIfFailed(CommandQueue->Signal(fence.Get(), fenceValue));
}

void D3DWindow::WorkerThread(int threadIndex)
{
	assert(threadIndex >= 0);
	assert(threadIndex < NumContexts);

	HANDLE beginEvents[工作阶段计数] =
	{
		workerBeginRecordCommand[阴影工作阶段][threadIndex],
		workerBeginRecordCommand[不透明工作阶段][threadIndex],
		workerBeginRecordCommand[法线工作阶段][threadIndex]
	};

	while (threadIndex >= 0 && threadIndex < NumContexts)
	{
		const DWORD waitResult = WaitForMultipleObjects(工作阶段计数, beginEvents, FALSE, INFINITE);
		UINT workerPassIndex = 不透明工作阶段;
		switch (waitResult)
		{
		case WAIT_OBJECT_0 + 0:
			workerPassIndex = 阴影工作阶段;
			break;
		case WAIT_OBJECT_0 + 1:
			workerPassIndex = 不透明工作阶段;
			break;
		case WAIT_OBJECT_0 + 2:
			workerPassIndex = 法线工作阶段;
			break;
		default:
			continue;
		}

		// 工作线程每次只录制当前阶段对应的那份命令列表。
		auto threadCommandAllocator = GetWorkerCommandAllocator(workerPassIndex, threadIndex);
		auto threadCommandList = GetWorkerCommandList(workerPassIndex, threadIndex);
		ThrowIfFailed(threadCommandAllocator->Reset());
		ThrowIfFailed(threadCommandList->Reset(threadCommandAllocator, nullptr));

		const auto& opaqueBatch = OpaqueThreadBatches[threadIndex];
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());
		ID3D12DescriptorHeap* descriptorHeaps[] = { SrvDescriptorHeap.Get() };

		switch (workerPassIndex)
		{
		case 阴影工作阶段:
		{
			// 阴影阶段只关心可投影的不透明物体批次。
			threadCommandList->SetGraphicsRootSignature(RootSignature.Get());
			threadCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
			threadCommandList->SetGraphicsRootDescriptorTable(4, skyTexDescriptor);
			threadCommandList->SetGraphicsRootDescriptorTable(5, otherTexDescriptor);
			threadCommandList->SetGraphicsRootDescriptorTable(6, shadowMapDescriptor);
			threadCommandList->SetGraphicsRootDescriptorTable(7, ambientOcclusionDescriptor);

			for (UINT i = 0; i < ShadowRenderEntries.size(); ++i)
			{
				shadowMap.SetRenderTargets(CurrFrameResource->shadowThreadCommandLists[threadIndex], i);
				if (threadIndex == 0)
				{
					threadCommandList->ClearDepthStencilView(shadowMap.shaderMapDSVCpuHandle,
						D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
				}
				DrawRenderItems(threadCommandList, opaqueBatch, PipelineState[2], 2, 1 + i);
			}

			if (threadIndex == NumContexts - 1 && shadowMap.GetHeapIndexSize() > 0)
			{
				std::vector<D3D12_RESOURCE_BARRIER> shadowBarriers;
				shadowBarriers.reserve(shadowMap.GetHeapIndexSize());
				for (UINT shadowIndex = 0; shadowIndex < shadowMap.GetHeapIndexSize(); ++shadowIndex)
				{
					auto shadowResource = shadowMap.GetResource(shadowIndex);
					if (shadowResource != nullptr)
					{
						shadowBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(shadowResource.Get(),
							D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
					}
				}
				if (!shadowBarriers.empty())
					threadCommandList->ResourceBarrier(static_cast<UINT>(shadowBarriers.size()), shadowBarriers.data());
			}
			break;
		}
		case 法线工作阶段:
		{
			// 法线阶段为 SSAO 准备 normal/depth 输入。
			threadCommandList->SetGraphicsRootSignature(RootSignature.Get());
			threadCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
			threadCommandList->SetGraphicsRootConstantBufferView(1, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
			threadCommandList->SetGraphicsRootConstantBufferView(2, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
			threadCommandList->SetGraphicsRootDescriptorTable(4, skyTexDescriptor);
			threadCommandList->SetGraphicsRootDescriptorTable(5, otherTexDescriptor);
			threadCommandList->SetGraphicsRootDescriptorTable(6, shadowMapDescriptor);
			threadCommandList->SetGraphicsRootDescriptorTable(7, ambientOcclusionDescriptor);
			ambientOcclusion.SetViewports(CurrFrameResource->normalThreadCommandLists[threadIndex]);

			if (threadIndex == 0)
			{
				D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(ambientOcclusion.NormalMap().Get(),
					D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
				threadCommandList->ResourceBarrier(1, &Barriers);
				threadCommandList->OMSetRenderTargets(1, &ambientOcclusion.mhNormalMapCpuRtv, true, &dsvHandle);
				float clearNormalMapColor[] = { 0.0f, 0.0f, 1.0f, 0.0f };
				threadCommandList->ClearRenderTargetView(ambientOcclusion.mhNormalMapCpuRtv, clearNormalMapColor, 0, nullptr);
				threadCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
			}
			else
			{
				threadCommandList->OMSetRenderTargets(1, &ambientOcclusion.mhNormalMapCpuRtv, true, &dsvHandle);
			}

			DrawRenderItems(threadCommandList, opaqueBatch, PipelineState[法线绘制管道], 法线绘制管道);
			break;
		}
		case 不透明工作阶段:
		default:
		{
			// 常规不透明绘制阶段直接输出到当前背缓冲。
			threadCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
			threadCommandList->SetGraphicsRootSignature(RootSignature.Get());
			threadCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
			threadCommandList->SetGraphicsRootConstantBufferView(1, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
			threadCommandList->SetGraphicsRootConstantBufferView(2, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());
			threadCommandList->SetGraphicsRootDescriptorTable(4, skyTexDescriptor);
			threadCommandList->SetGraphicsRootDescriptorTable(5, otherTexDescriptor);
			threadCommandList->SetGraphicsRootDescriptorTable(6, shadowMapDescriptor);
			threadCommandList->SetGraphicsRootDescriptorTable(7, ambientOcclusionDescriptor);
			threadCommandList->RSSetViewports(1, &m_viewport);
			threadCommandList->RSSetScissorRects(1, &m_scissorRect);
			DrawRenderItems(threadCommandList, opaqueBatch, PipelineState[不透明物体管道], 不透明物体管道);
			break;
		}
		}

		ThrowIfFailed(threadCommandList->Close());
		SetEvent(workerFinishedRecordCommand[threadIndex]);
	}
}

void D3DWindow::DestroyRender()
{
	//确保GPU不再引用将由析构函数清除的资源。
	FlushCommandQueue();
}

void D3DWindow::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& rditems, ComPtr<ID3D12PipelineState> pipelineState, UINT pipelineNumber, UINT passCBIndex)
{
	if (rditems.empty()) return;

	cmdList->SetPipelineState(pipelineState.Get());

	UINT objCBByteSize = CalculateConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = CalculateConstantBufferByteSize(sizeof(MaterialConstants));
	UINT passCBByteSize = CalculateConstantBufferByteSize(sizeof(PassConstants));

	auto objectCB = CurrFrameResource->ObjectCB->Resource();
	auto MatCB = CurrFrameResource->MaterialCB->Resource();
	auto passCB = CurrFrameResource->PassCB->Resource();

	for (auto ritem : rditems)
	{
		if (ritem == nullptr)
			continue;

		// RenderItem 仅保存聚合缓冲中的绘制范围，实际几何数据在 Geo 中。
		cmdList->IASetVertexBuffers(0, 1, &ritem->Geo->vertexBufferView);
		cmdList->IASetIndexBuffer(&ritem->Geo->indexBufferView);
		cmdList->IASetPrimitiveTopology(ritem->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE Tex(SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		if (ritem->Obj->Material->DiffuseTexture != nullptr)
			Tex = ritem->Obj->Material->DiffuseTexture->GetGPUTexDescriptor();

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress()
			+ ritem->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		if (pipelineNumber == 0 || pipelineNumber == 1 || pipelineNumber == 3)
		{
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = MatCB->GetGPUVirtualAddress()
				+ ritem->Obj->Material->MatCBIndex * matCBByteSize;

			cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
			cmdList->SetGraphicsRootDescriptorTable(5, Tex);
		}
		else if (pipelineNumber == 2 || pipelineNumber == 4)
		{
			D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress()
				+ passCBIndex * passCBByteSize;

			cmdList->SetGraphicsRootConstantBufferView(1, passCBAddress);
		}

		cmdList->DrawIndexedInstanced(ritem->Obj->AggrObject->IndexCount, 1, ritem->Obj->AggrObject->StartIndexLocation, ritem->Obj->AggrObject->BaseVertexLocation, 0);
	}
}

void D3DWindow::CloseCommandListAndSynchronize()
{
	// 命令列表在记录状态下创建，但是尚无记录。
	// 主循环期望它被关闭，所以现在就关闭它。
	CloseCommandList();

	ID3D12CommandList* ppCommandLists[] = { MainCommandList.Get() };
	CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// 等待命令列表执行；
	// 我们在主循环中重复使用了相同的命令列表，但就目前而言，我们只想等待安装完成后再继续。
	FlushCommandQueue();
}

void D3DWindow::FlushCommandQueue()
{
	if (fence == nullptr)
		return;

	fenceValue++;

	ThrowIfFailed(CommandQueue->Signal(fence.Get(), fenceValue));


	if (fence->GetCompletedValue() < fenceValue)
	{
		ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// 到这里说明队列中在当前 fence 之前提交的命令都已完成，
	// 可以安全释放此前延迟销毁的几何资源。
	if (!DeferredReleaseGeometries.empty())
		DeferredReleaseGeometries.clear();
}

void D3DWindow::ResetCommandList()
{
	ThrowIfFailed(MainCommandList->Reset(MainCommandAllocator.Get(), nullptr));
	CommandListClose = false;
}

void D3DWindow::CloseCommandList()
{
	ThrowIfFailed(MainCommandList->Close());
	CommandListClose = true;
}

bool D3DWindow::IsCommandListClose()
{
	return CommandListClose;
}

HWND D3DWindow::GethWnd()
{
	return m_hwnd;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE D3DWindow::GetCpuSrv() const
{
	auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	return srv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE D3DWindow::GetGpuSrv() const
{
	auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	return srv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE D3DWindow::GetDsv() const
{
	auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(DsvHeap->GetCPUDescriptorHandleForHeapStart());
	return dsv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE D3DWindow::GetRtv() const
{
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(RtvHeap->GetCPUDescriptorHandleForHeapStart(), CurrBackBufferIndex, RtvDescriptorSize);
	return rtv;
}

UINT D3DWindow::GetRtvDescriptorSize()
{
	return RtvDescriptorSize;
}

UINT D3DWindow::GetDsvDescriptorSize()
{
	return DsvDescriptorSize;
}

UINT D3DWindow::GetCbvSrvUavDescriptorSize()
{
	return CbvSrvUavDescriptorSize;
}

DXGI_FORMAT D3DWindow::GetIndexBufferFormat() const
{
	return IndexBufferFormat;
}

AggregateGraphicObj* D3DWindow::GetAggregateGraphicObj(const std::wstring& geometryName)
{
	auto it = AggrObject.find(geometryName);
	if (it == AggrObject.end())
		return nullptr;

	return &it->second;
}

RenderItem* D3DWindow::GetRenderItems(const std::wstring& name)
{
	auto it = AllRitems.find(name);
	if (it == AllRitems.end())
		return nullptr;

	return &it->second;
}


void D3DWindow::SetFPSRender(bool enable)
{
	renderFPS = enable;
}

bool D3DWindow::IsFPSRender() const
{
	return renderFPS;
}

float D3DWindow::GetShadowOpacity() const
{
	return MainPassCB.ShadowSettings.x;
}

void D3DWindow::SetShadowOpacity(float opacity)
{
	MainPassCB.ShadowSettings.x = std::clamp(opacity, ShadowConfig.MinOpacity, ShadowConfig.MaxOpacity);
}

float D3DWindow::GetShadowSoftness() const
{
	return MainPassCB.ShadowSettings.y;
}

void D3DWindow::SetShadowSoftness(float softness)
{
	MainPassCB.ShadowSettings.y = std::clamp(softness, ShadowConfig.MinSoftness, ShadowConfig.MaxSoftness);
}

DirectX::XMFLOAT3 D3DWindow::GetPosition3f() const
{
	return mCamera.GetPosition3f();
}

void D3DWindow::SetPosition3f(DirectX::XMFLOAT3 Position)
{
	mCamera.SetPosition(Position);
}

DirectX::XMFLOAT3 D3DWindow::GetRotation3f() const
{
	return mCamera.GetRotation3f();
}

void D3DWindow::SetRotation3f(DirectX::XMFLOAT3 Rotation)
{
	mCamera.SetRotation(Rotation);
}

float D3DWindow::GetCameraSpeed()
{
	return mCamera.CameraParame.camSpeed;
}

void D3DWindow::SetCameraSpeed(float speed)
{
	mCamera.CameraParame.camSpeed = speed;
}

float D3DWindow::GetNearZ() const
{
	return mCamera.GetNearZ();
}

float D3DWindow::GetFarZ() const
{
	return mCamera.GetFarZ();
}

float D3DWindow::GetFovY() const
{
	return mCamera.GetFovY();
}

float D3DWindow::GetFovX() const
{
	return mCamera.GetFovX();
}

void D3DWindow::SetNearZ(float nearZ)
{
	mCamera.SetNearZ(nearZ);
}

void D3DWindow::SetFarZ(float farZ)
{
	mCamera.SetFarZ(farZ);
}

void D3DWindow::SetFovY(float fovY)
{
	mCamera.SetFovY(fovY);
}

float D3DWindow::GetViewportScale()const
{
	return mCamera.GetViewportScale();
}

void D3DWindow::SetViewportScale(float scale)
{
	mCamera.SetViewportScale(scale);
}

void D3DWindow::RestoreScale()
{
	mCamera.SetViewportScale(static_cast<float>(EngineHelpers::GetContextWidth(m_hwnd)) /
		static_cast<float>(EngineHelpers::GetContextHeight(m_hwnd)));
}

DirectX::XMMATRIX D3DWindow::GetView() const
{
	return mCamera.GetView();
}

DirectX::XMMATRIX D3DWindow::GetProj() const
{
	return mCamera.GetProj();
}

void D3DWindow::RotateCamera(float DeltaTime, DirectX::XMFLOAT2 angle)
{
	mCamera.RotateCamera(DeltaTime, angle);
}

void D3DWindow::MoveCamera(float DeltaTime, DirectX::XMFLOAT3 distance)
{
	mCamera.MoveCamera(DeltaTime, distance);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> D3DWindow::GetStaticSamplers()
{
	// 应用程序通常只需要少量的采样器。
	// 因此，只需预先定义它们，并将其作为根签名的一部分。

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,                               // mipLODBias
		16,                                 // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp,
		shadow};
}

ComPtr<ID3DBlob> D3DWindow::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	// 约定调用方只传不带扩展名的路径，这里统一补成 .hlsl。
	std::wstring hlsl_Path = filename;
	hlsl_Path.append(L".hlsl");
	
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	// 调试构建保留调试信息并关闭优化，便于定位 shader 问题。
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	// defines / entrypoint / target 分别控制宏变体、入口函数和着色器模型。
	hr = D3DCompileFromFile(hlsl_Path.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if (errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

	return byteCode;
}

ComPtr<ID3D12Resource> D3DWindow::CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer)
{
	ComPtr<ID3D12Resource> defaultBuffer;

	// default heap 是最终给 GPU 读取的正式资源。
	D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
	//创建实际的默认缓冲区资源。
	ThrowIfFailed(device->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&Desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

	// upload heap 作为中转缓冲，把 CPU 侧初始化数据拷贝到 default heap。
	HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	//为了将CPU内存数据复制到我们的默认缓冲区中，我们需要创建一个中间上传堆。
	ThrowIfFailed(device->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&Desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


	//描述我们要复制到默认缓冲区的数据。
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	//计划将数据复制到默认缓冲区资源。 在较高级别上，辅助函数UpdateSubresources
	//将CPU内存复制到中间上传堆中。
	//然后，使用ID3D12CommandList::Copy子资源区域，将中间上传堆数据复制到mBuffer。
	D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &Barriers);
	UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(),
		0, 0, 1, &subResourceData);
	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &Barriers);

	//注意：在上述函数调用之后，uploadBuffer必须保持活动状态，因为尚未执行实际复制的命令列表。
	//直到复制已执行后，调用者可以释放uploadBuffer，所以我们要保证这份资源存活并能被调用。
	return defaultBuffer;
}
