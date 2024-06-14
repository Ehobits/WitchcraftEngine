#include "D3DWindow.h"

#include "HELPERS/Helpers.h"
#include "ModelAnalysis/AssimpLoader.h"
#include "../ServicesContainer/Component/MeshComponent.h"
#include "../String/SStringUtils.h"

#include "Editor/Editor.h"

FrameResource::FrameResource()
{
}

FrameResource::~FrameResource()
{
}

void FrameResource::Update(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	if(materialCount>0)
		MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, false);
	if (objectCount > 0)
		ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
	LightCB = std::make_unique<UploadBuffer<LightConstants>>(device, 1, true);
	AOCB = std::make_unique<UploadBuffer<AOConstants>>(device, 1, true);
}

// 单例对象，以便工作线程可以共享成员。
static D3DWindow* s_app;

D3DWindow::D3DWindow()
{
	s_app = this;
	
	mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; //DXGI_FORMAT_R16G16B16A16_FLOAT;// 
	mDepthStencilFormat = DXGI_FORMAT_D32_FLOAT;
}

D3DWindow::~D3DWindow()
{
	if (d3dDevice != nullptr)
		FlushCommandQueue();

	// 关闭线程事件和线程句柄。
	for (int i = 0; i < NumContexts; i++)
	{
		CloseHandle(workerBeginRecordCommand[i]);
		CloseHandle(workerFinishedRecordCommand[i]);
		CloseHandle(threadHandles[i]);
	}

	s_app = nullptr;
}

bool D3DWindow::Create(HWND hWnd, Timer* timer, Editor* editor)
{
	m_hwnd = hWnd;
	m_Timer = timer;
	m_editor = editor;

	CreateDevice();
	CreateCommandQueueAndSwapChain();
	CreateDescriptorHeaps();

	// 从关闭状态开始。 
	// 这是因为第一次引用命令列表时，我们将其重置，并且需要在调用Reset之前将其关闭。
	CloseCommandList();

	//进行初始大小调整代码。
	OnResize();

	mCamera.SetLens(0.25f * MathHelps::Pi,
		static_cast<float>(EngineHelpers::GetContextWidth(m_hwnd)) / static_cast<float>(EngineHelpers::GetContextHeight(m_hwnd)),
		1.0f, 1000.0f);

	mCamera.SetPosition(0.0f, 5.0f, -15.0f);

	ResetCommandList();

	textR = new TextRender(d3dDevice.Get(), MainCommandList.Get(), SwapChainBufferCount);

	CreateRootSignature();
	CreatePipesAndShaders();
	AddShapeGeometry();

	CreateCBVAndSRVDescriptorHeaps();

	LoadTextures();
	
	if (!textR->DXCreateFont(L"DATA\\Fonts\\STXIHEI.TTF", 66))
		return false;

	BuildMaterials();
	BuildLight();

	BuildAndGenerateObjects();
	BuildRenderItems();
	UpdateFrameResources();

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
	sd.BufferDesc.Width = (float)EngineHelpers::GetContextWidth(m_hwnd);
	sd.BufferDesc.Height = (float)EngineHelpers::GetContextHeight(m_hwnd);
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = mBackBufferFormat;
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
		mFrameResources[i].BeginCommandList->SetName(L"MidCommandList");
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

			ThrowIfFailed(d3dDevice->CreateCommandList(
				0, D3D12_COMMAND_LIST_TYPE_DIRECT,
				mFrameResources[i].threadCommandAllocators[j].Get(),
				nullptr,
				IID_PPV_ARGS(&mFrameResources[i].threadCommandLists[j])));
			mFrameResources[i].threadCommandLists[j]->SetName((L"threadCommandLists" + std::to_wstring(i) +L"." + std::to_wstring(j)).c_str());
			ThrowIfFailed(mFrameResources[i].threadCommandLists[j]->Close());
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
	msQualityLevels.Format = mBackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(d3dDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

	m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m4xMsaaQuality > 0 && "意外的MSAA质量水平。");
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

	// 为阴影贴图添加+1个DSV。
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 2;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(DsvHeap.GetAddressOf())));
}

void D3DWindow::OnResize()
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
		mSwapChainBuffer[i].Reset();
		mFrameResources[i].mCopyTexture.Reset();
	}
	mDepthStencilBuffer.Reset();

	// 调整交换链的大小。
	ThrowIfFailed(SwapChain->ResizeBuffers(
		SwapChainBufferCount,
		(float)EngineHelpers::GetContextWidth(m_hwnd), (float)EngineHelpers::GetContextHeight(m_hwnd),
		mBackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	// 每次调整大小，需要将后缓冲置为0。
	mCurrBackBufferIndex = 0;

	//创建渲染目标视图（RTV）。
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart());
	// 为每个帧创建一个RTV。
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(SwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
		d3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		mSwapChainBuffer[i]->SetName((L"SwapChainBuffer" + std::to_wstring(i)).c_str());
		rtvHeapHandle.Offset(1, RtvDescriptorSize);
	}

	// 创建深度模板缓冲区和视图。
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = (float)EngineHelpers::GetContextWidth(m_hwnd);
	depthStencilDesc.Height = (float)EngineHelpers::GetContextHeight(m_hwnd);
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;

	//更正2016年11月12日：SSAO章节要求对深度缓冲区有SRV才能从深度缓冲区中读取。
	//因此，因为我们需要为同一资源创建两个视图：
		 // 1. SRV格式：DXGI_FORMAT_R24_UNORM_X8_TYPELESS
		 // 2. DSV格式：DXGI_FORMAT_D24_UNORM_S8_UINT
		 //我们需要使用无类型格式创建深度缓冲区资源。
	depthStencilDesc.Format = mDepthStencilFormat;

	depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = mDepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), mCurrBackBufferIndex, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());

	// 使用资源格式将描述符创建为整个资源的MIP级别0。
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	d3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, dsvHandle);

	//将资源从其初始状态转换为深度缓冲区。
	D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	MainCommandList->ResourceBarrier(1, &Barriers);

	CD3DX12_RESOURCE_DESC copyTexDesc;
	copyTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	copyTexDesc.Alignment = 0;
	copyTexDesc.Width = (float)EngineHelpers::GetContextWidth(m_hwnd);
	copyTexDesc.Height = (float)EngineHelpers::GetContextHeight(m_hwnd);
	copyTexDesc.DepthOrArraySize = 1;
	copyTexDesc.MipLevels = 1;
	copyTexDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	copyTexDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	copyTexDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	copyTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	copyTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue;        // Performance tip: Tell the runtime at resource creation the desired clear value.
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
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
		static_cast<float>(EngineHelpers::GetContextWidth(m_hwnd)), 
		static_cast<float>(EngineHelpers::GetContextHeight(m_hwnd)),
		0.0f,1.0f };
	m_scissorRect = CD3DX12_RECT{ 0, 0, 
		(long)EngineHelpers::GetContextWidth(m_hwnd),
		(long)EngineHelpers::GetContextHeight(m_hwnd) };
}

// 创建根签名
void D3DWindow::CreateRootSignature()
{
	// 着色器程序通常需要资源作为输入（常量缓冲区，纹理，采样器）。
	// 根签名定义着色器程序期望的资源。 
	// 如果我们将着色器程序视为函数，将输入资源视为函数参数，则可以将根签名视为定义函数签名。

	CD3DX12_DESCRIPTOR_RANGE1 cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE1 texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE1 texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 12, 1, 1);

	//根参数可以是表，根描述符或根常量。
	CD3DX12_ROOT_PARAMETER1 slotRootParameter[8];

	//创建根CBV。效果提示：从最频繁到最不频繁的顺序。
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable); // color
	slotRootParameter[1].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL); // MaterialData
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL); // opaquetexture
	slotRootParameter[3].InitAsConstantBufferView(1); // register b1
	slotRootParameter[4].InitAsConstantBufferView(2); // register b2
	slotRootParameter[5].InitAsConstantBufferView(3); // register b3
	slotRootParameter[6].InitAsConstantBufferView(4); // register b4
	slotRootParameter[7].InitAsShaderResourceView(0, 1);

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

	textR->CreateRootSignature();
}

// 创建管道状态，其中包括编译和加载着色器。
void D3DWindow::CreatePipesAndShaders()
{
	// 透明通道测试定义
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	ComPtr<ID3DBlob> vertexShader[着色器计数];
	ComPtr<ID3DBlob> pixelShader[着色器计数];
	ComPtr<ID3DBlob> AlphaTestedShader[着色器计数];

	vertexShader[天空着色器] = CompileShader(L"DATA/Shaders/Sky", nullptr, "VS", "vs_5_1");
	pixelShader[天空着色器] = CompileShader(L"DATA/Shaders/Sky", nullptr, "PS", "ps_5_1");

	vertexShader[不透明物体着色器] = CompileShader(L"DATA/Shaders/Default", nullptr, "VS", "vs_5_1");
	pixelShader[不透明物体着色器] = CompileShader(L"DATA/Shaders/Default", nullptr, "PS", "ps_5_1");

	vertexShader[阴影着色器] = CompileShader(L"DATA/Shaders/Shadows", nullptr, "VS", "vs_5_1");
	pixelShader[阴影着色器] = CompileShader(L"DATA/Shaders/Shadows", nullptr, "PS", "ps_5_1");
	//AlphaTestedShader[阴影透明通道着色器] = CompileShader(L"DATA\\Shaders\\Shadows.hlsl", alphaTestDefines, "PS", "ps_5_1");

	//vertexShader[环境遮蔽着色器] = CompileShader(L"DATA\\Shaders\\Ssao.hlsl", nullptr, "VS", "vs_5_1");
	//pixelShader[环境遮蔽着色器] = CompileShader(L"DATA\\Shaders\\Ssao.hlsl", nullptr, "PS", "ps_5_1");

	//vertexShader[遮蔽模糊着色器] = CompileShader(L"DATA\\Shaders\\SsaoBlur.hlsl", nullptr, "VS", "vs_5_1");
	//pixelShader[遮蔽模糊着色器] = CompileShader(L"DATA\\Shaders\\SsaoBlur.hlsl", nullptr, "PS", "ps_5_1");

	vertexShader[文字着色器] = CompileShader(L"DATA/Shaders/Text", nullptr, "VS", "vs_5_1");
	pixelShader[文字着色器] = CompileShader(L"DATA/Shaders/Text", nullptr, "PS", "ps_5_1");

	// 定义顶点输入布局。
	InputElementDescs =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 60, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
	basePsoDesc.RTVFormats[0] = mBackBufferFormat;
	basePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	basePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	basePsoDesc.DSVFormat = mDepthStencilFormat;

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

	//
	//用于阴影贴图传递的PSO。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = basePsoDesc;
	smapPsoDesc.RasterizerState.DepthBias = 100000;
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	smapPsoDesc.pRootSignature = RootSignature.Get();
	smapPsoDesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	smapPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[阴影着色器].Get());
	smapPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[阴影着色器].Get());

	//阴影贴图传递没有渲染目标。
	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	smapPsoDesc.NumRenderTargets = 0;
	ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&smapPsoDesc,
		IID_PPV_ARGS(&PipelineState[阴影管道])));

	//
	// AO的PSO。
	//
	//D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc = basePsoDesc;
	//ssaoPsoDesc.InputLayout = { nullptr, 0 };
	//ssaoPsoDesc.pRootSignature = AORootSignature.Get();
	//ssaoPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[环境遮蔽着色器].Get());
	//ssaoPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[环境遮蔽着色器].Get());
	//ssaoPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16_UNORM;

	// AO效果不需要深度缓冲区。
	//ssaoPsoDesc.DepthStencilState.DepthEnable = false;
	//ssaoPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	//ssaoPsoDesc.SampleDesc.Count = 1;
	//ssaoPsoDesc.SampleDesc.Quality = 0;
	//ssaoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	//ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&ssaoPsoDesc,
	//	IID_PPV_ARGS(&PipelineState[环境遮蔽管道])));

	//
	// AO模糊功能的PSO。
	//
	//D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoBlurPsoDesc = ssaoPsoDesc;
	//ssaoBlurPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader[遮蔽模糊着色器].Get());
	//ssaoBlurPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader[遮蔽模糊着色器].Get());
	//ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&ssaoBlurPsoDesc,
	//	IID_PPV_ARGS(&PipelineState[遮蔽模糊管道])));

	textR->CreatePipesAndShaders(vertexShader[文字着色器].Get(), pixelShader[文字着色器].Get(),
		mBackBufferFormat, mDepthStencilFormat, &PipelineState[文字管道]);
}

void D3DWindow::CreateCBVAndSRVDescriptorHeaps()
{
	//
	//创建CBV堆。
	//
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = SwapChainBufferCount;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(
		&cbvHeapDesc, IID_PPV_ARGS(&CbvDescriptorHeap)));

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

	Texture DiffuseTex(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"DiffuseMap", L"DATA/Textures/white8x8.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture defaultNmapTex(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"defaultNmap", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture skyCubeTex(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"skyMap", L"DATA/HDRIs/qwantani_puresky_4k.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	mSkyTexHeapIndex = 2;

	Texture bricksTex(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"bricksDiffuseMap", L"DATA/Textures/bricks.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture bricksMapTex(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"bricksNormalMap", L"DATA/Textures/bricks_nmap.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture stoneTex(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"stoneDiffuseMap", L"DATA/Textures/stone.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture tileTex(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"tileDiffuseMap", L"DATA/Textures/tile.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture tileMapTex(
		d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"tileNormalMap", L"DATA/Textures/tile_nmap.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	//--------------------------------------------------------
	Texture semlcibb_Albedo(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"semlcibb_Albedo", L"DATA/Textures/Brick_Modern/semlcibb_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Normal(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"semlcibb_Normal", L"DATA/Textures/Brick_Modern/semlcibb_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Roughness(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"semlcibb_Roughness", L"DATA/Textures/Brick_Modern/semlcibb_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Specular(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"semlcibb_Specular", L"DATA/Textures/Brick_Modern/semlcibb_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture semlcibb_Displacement(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"semlcibb_Displacement", L"DATA/Textures/Brick_Modern/semlcibb_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	//--------------------------------------------------------
	Texture rustediron_basecolor(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"rustediron_basecolor", L"DATA/Textures/rustediron/rustediron2_basecolor.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rustediron_normal(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"rustediron_normal", L"DATA/Textures/rustediron/rustediron2_normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rustediron_metallic(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"rustediron_metallic", L"DATA/Textures/rustediron/rustediron2_metallic.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rustediron_roughness(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"rustediron_roughness", L"DATA/Textures/rustediron/rustediron2_roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	//--------------------------------------------------------
	Texture rm4kshp_Albedo(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Dirty_Albedo", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Normal(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Dirty_Normal", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Roughness(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Dirty_Roughness", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Specular(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Dirty_Specular", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture rm4kshp_Displacement(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Dirty_Displacement", L"DATA/Textures/Concrete_Dirty_1K/rm4kshp_4K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	//--------------------------------------------------------
	Texture sdbhdd3b_Albedo(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Rough_Albedo", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Normal(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Rough_Normal", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Roughness(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Rough_Roughness", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Specular(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Rough_Specular", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sdbhdd3b_Displacement(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Concrete_Rough_Displacement", L"DATA/Textures/Concrete_Rough_1K/sdbhdd3b_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	//--------------------------------------------------------
	Texture sfknaeoa_Albedo(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Grass_Wild_Albedo", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Normal(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Grass_Wild_Normal", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Roughness(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Grass_Wild_Roughness", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Specular(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Grass_Wild_Specular", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture sfknaeoa_Displacement(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Grass_Wild_Displacement", L"DATA/Textures/Grass_Wild_1K/sfknaeoa_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	//--------------------------------------------------------
	Texture copper_rock1_alb(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"copper-rock1-alb", L"DATA/Textures/rockcopper/copper-rock1-alb.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture copper_rock1_normal(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"copper-rock1-normal", L"DATA/Textures/rockcopper/copper-rock1-normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture copper_rock1_metal(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"copper-rock1-metal", L"DATA/Textures/rockcopper/copper-rock1-metal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture copper_rock1_rough(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"copper-rock1-rough", L"DATA/Textures/rockcopper/copper-rock1-rough.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	//--------------------------------------------------------
	Texture scpgdgca_Albedo(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Stone_Wall_Albedo", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Normal(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Stone_Wall_Normal", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Roughness(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Stone_Wall_Roughness", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Specular(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Stone_Wall_Specular", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture scpgdgca_Displacement(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Stone_Wall_Displacement", L"DATA/Textures/Stone_Wall_1K/scpgdgca_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;
	//--------------------------------------------------------
	Texture se2abbvc_Albedo(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Metal_Bare_Albedo", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Albedo.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Normal(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Metal_Bare_Normal", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Normal.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Roughness(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Metal_Bare_Roughness", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Roughness.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Specular(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Metal_Bare_Specular", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Specular.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	Texture se2abbvc_Displacement(d3dDevice.Get(),
		SrvDescriptorHeap.Get(),
		&resourceUpload,
		L"Metal_Bare_Displacement", L"DATA/Textures/Metal_Bare_1K/se2abbvc_8K_Displacement.png",
		TextureType::PNG,
		SrvDescriptorHeapIndex);
	SrvDescriptorHeapIndex++;

	auto uploadResourcesFinished = resourceUpload.End(
		CommandQueue.Get());
	uploadResourcesFinished.wait();

	mTextures[DiffuseTex.GetName()] = DiffuseTex;
	mTextures[defaultNmapTex.GetName()] = defaultNmapTex;
	mTextures[skyCubeTex.GetName()] = skyCubeTex;
	mTextures[bricksTex.GetName()] = bricksTex;
	mTextures[bricksMapTex.GetName()] = bricksMapTex;
	mTextures[stoneTex.GetName()] = stoneTex;
	mTextures[tileTex.GetName()] = tileTex;
	mTextures[tileMapTex.GetName()] = tileMapTex;

	//--------------------------------------------------------
	mTextures[semlcibb_Albedo.GetName()] = semlcibb_Albedo;
	mTextures[semlcibb_Normal.GetName()] = semlcibb_Normal;
	mTextures[semlcibb_Roughness.GetName()] = semlcibb_Roughness;
	mTextures[semlcibb_Specular.GetName()] = semlcibb_Specular;
	mTextures[semlcibb_Displacement.GetName()] = semlcibb_Displacement;
	//--------------------------------------------------------
	mTextures[rustediron_basecolor.GetName()] = rustediron_basecolor;
	mTextures[rustediron_normal.GetName()] = rustediron_normal;
	mTextures[rustediron_metallic.GetName()] = rustediron_metallic;
	mTextures[rustediron_roughness.GetName()] = rustediron_roughness;
	//--------------------------------------------------------
	mTextures[rm4kshp_Albedo.GetName()] = rm4kshp_Albedo;
	mTextures[rm4kshp_Normal.GetName()] = rm4kshp_Normal;
	mTextures[rm4kshp_Roughness.GetName()] = rm4kshp_Roughness;
	mTextures[rm4kshp_Specular.GetName()] = rm4kshp_Specular;
	mTextures[rm4kshp_Displacement.GetName()] = rm4kshp_Displacement;
	//--------------------------------------------------------
	mTextures[sdbhdd3b_Albedo.GetName()] = sdbhdd3b_Albedo;
	mTextures[sdbhdd3b_Normal.GetName()] = sdbhdd3b_Normal;
	mTextures[sdbhdd3b_Roughness.GetName()] = sdbhdd3b_Roughness;
	mTextures[sdbhdd3b_Specular.GetName()] = sdbhdd3b_Specular;
	mTextures[sdbhdd3b_Displacement.GetName()] = sdbhdd3b_Displacement;
	//--------------------------------------------------------
	mTextures[sfknaeoa_Albedo.GetName()] = sfknaeoa_Albedo;
	mTextures[sfknaeoa_Normal.GetName()] = sfknaeoa_Normal;
	mTextures[sfknaeoa_Roughness.GetName()] = sfknaeoa_Roughness;
	mTextures[sfknaeoa_Specular.GetName()] = sfknaeoa_Specular;
	mTextures[sfknaeoa_Displacement.GetName()] = sfknaeoa_Displacement;
	//--------------------------------------------------------
	mTextures[copper_rock1_alb.GetName()] = copper_rock1_alb;
	mTextures[copper_rock1_normal.GetName()] = copper_rock1_normal;
	mTextures[copper_rock1_metal.GetName()] = copper_rock1_metal;
	mTextures[copper_rock1_rough.GetName()] = copper_rock1_rough;
	//--------------------------------------------------------
	mTextures[scpgdgca_Albedo.GetName()] = scpgdgca_Albedo;
	mTextures[scpgdgca_Normal.GetName()] = scpgdgca_Normal;
	mTextures[scpgdgca_Roughness.GetName()] = scpgdgca_Roughness;
	mTextures[scpgdgca_Specular.GetName()] = scpgdgca_Specular;
	mTextures[scpgdgca_Displacement.GetName()] = scpgdgca_Displacement;
	//--------------------------------------------------------
	mTextures[se2abbvc_Albedo.GetName()] = se2abbvc_Albedo;
	mTextures[se2abbvc_Normal.GetName()] = se2abbvc_Normal;
	mTextures[se2abbvc_Roughness.GetName()] = se2abbvc_Roughness;
	mTextures[se2abbvc_Specular.GetName()] = se2abbvc_Specular;
	mTextures[se2abbvc_Displacement.GetName()] = se2abbvc_Displacement;

}

void D3DWindow::AddShapeGeometry()
{
	std::vector<Mesh> skyModel;
	std::vector<Mesh> boxModel;
	std::vector<Mesh> plModel;

	AssimpLoader assimpLoader;
	skyModel = assimpLoader.LoadRawModel(L"DATA\\Models\\Sphere.obj");
	boxModel = assimpLoader.LoadRawModel(L"DATA\\Models\\Cube.obj");
	plModel = assimpLoader.LoadRawModel(L"DATA\\Models\\Plane.obj");

	//
	// 我们将所有几何图形连接到一个大的顶点/索引缓冲区中。
	// 因此，在缓冲区中定义每个子网格覆盖的区域。
	//

	// 将顶点偏移量缓存到级联的顶点缓冲区中的每个对象。
	UINT skyVertexOffset = 0;
	UINT boxVertexOffset = (UINT)skyModel[0].vertices.size();
	UINT plVertexOffset = boxVertexOffset + (UINT)boxModel[0].vertices.size();
	UINT bVertexOffset = plVertexOffset + (UINT)plModel[0].vertices.size();

	//在串联的索引缓冲区中缓存每个对象的起始索引。
	UINT skyIndexOffset = 0;
	UINT boxIndexOffset = (UINT)skyModel[0].indices32.size();
	UINT plIndexOffset = boxIndexOffset + (UINT)boxModel[0].indices32.size();
	UINT bIndexOffset = plIndexOffset + (UINT)plModel[0].indices32.size();

	AggrObject[0].IndexCount = (UINT)skyModel[0].indices32.size();
	AggrObject[0].StartIndexLocation = skyIndexOffset;
	AggrObject[0].BaseVertexLocation = skyVertexOffset;

	AggrObject[1].IndexCount = (UINT)boxModel[0].indices32.size();
	AggrObject[1].StartIndexLocation = boxIndexOffset;
	AggrObject[1].BaseVertexLocation = boxVertexOffset;

	AggrObject[2].IndexCount = (UINT)plModel[0].indices32.size();
	AggrObject[2].StartIndexLocation = plIndexOffset;
	AggrObject[2].BaseVertexLocation = plVertexOffset;

	//
	//提取我们感兴趣的顶点元素，并将所有网格的顶点打包到一个顶点缓冲区中。
	//
	auto totalVertexCount =
		skyModel[0].vertices.size() +
		boxModel[0].vertices.size() +
		plModel[0].vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < skyModel[0].vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = skyModel[0].vertices[i].Pos;
		vertices[k].Color = skyModel[0].vertices[i].Color;
		vertices[k].Normal = skyModel[0].vertices[i].Normal;
		vertices[k].TexC = skyModel[0].vertices[i].TexC;
		vertices[k].TangentU = skyModel[0].vertices[i].TangentU;
	}

	for (size_t i = 0; i < boxModel[0].vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = boxModel[0].vertices[i].Pos;
		vertices[k].Color = boxModel[0].vertices[i].Color;
		vertices[k].Normal = boxModel[0].vertices[i].Normal;
		vertices[k].TexC = boxModel[0].vertices[i].TexC;
		vertices[k].TangentU = boxModel[0].vertices[i].TangentU;
	}

	for (size_t i = 0; i < plModel[0].vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = plModel[0].vertices[i].Pos;
		vertices[k].Color = plModel[0].vertices[i].Color;
		vertices[k].Normal = plModel[0].vertices[i].Normal;
		vertices[k].TexC = plModel[0].vertices[i].TexC;
		vertices[k].TangentU = plModel[0].vertices[i].TangentU;
	}

	std::vector<std::uint32_t> indices;
	indices.insert(indices.end(), std::begin(skyModel[0].GetIndices16()), std::end(skyModel[0].GetIndices16()));
	indices.insert(indices.end(), std::begin(boxModel[0].GetIndices16()), std::end(boxModel[0].GetIndices16()));
	indices.insert(indices.end(), std::begin(plModel[0].GetIndices16()), std::end(plModel[0].GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

	MeshGeometry geo;
	geo.Name = L"shapeGeo";

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
	geo.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	geo.indexBufferView.SizeInBytes = ibByteSize;

	mGeometries[geo.Name] = geo;
}

void D3DWindow::AddShapeGeometry(MeshGeometry* geo)
{
	mGeometries[geo->Name] = *geo;
}

void D3DWindow::RemoveShapeGeometry(std::wstring name)
{
	mGeometries[name].Name = L"空闲";
	mGeometries[name].VertexBufferCPU.Reset();
	mGeometries[name].IndexBufferCPU.Reset();
	mGeometries[name].VertexBufferGPU.Reset();
	mGeometries[name].IndexBufferGPU.Reset();
	mGeometries[name].VertexBufferUploader.Reset();
	mGeometries[name].IndexBufferUploader.Reset();
	mGeometries.erase(name);
}

void D3DWindow::BuildMaterials()
{
	auto autoMaterial = std::make_unique<Material>();
	autoMaterial->SetName(L"autoMat");
	autoMaterial->MatCBIndex = 0;
	autoMaterial->DiffuseTexture = &mTextures[L"DiffuseMap"];
	autoMaterial->NormalTexture = &mTextures[L"defaultNmap"];
	autoMaterial->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	autoMaterial->Roughness = 1.0f;
	autoMaterial->Metallic = 0.0f;
	mMaterials[L"autoMat"] = *autoMaterial.get();

	auto sky = std::make_unique<Material>();
	sky->SetName(L"sky");
	sky->MatCBIndex = 1;
	sky->DiffuseTexture = &mTextures[L"skyMap"];
	//sky->NormalTexture = 0;//sky就不需要了
	sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;
	//sky->Metallic = 0.0f;
	SkyMapIndex = mTextures[L"skyMap"].GetIndex();
	mMaterials[L"sky"] = *sky.get();

	auto bricks0 = std::make_unique<Material>();
	bricks0->SetName(L"box");
	bricks0->MatCBIndex = 2;
	bricks0->DiffuseTexture = &mTextures[L"bricksDiffuseMap"];
	bricks0->NormalTexture = &mTextures[L"bricksNormalMap"];
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;
	bricks0->Metallic = 0.0f;
	mMaterials[L"box"] = *bricks0.get();

	auto tile = std::make_unique<Material>();
	tile->SetName(L"tile");
	tile->MatCBIndex = 3;
	tile->DiffuseTexture = &mTextures[L"tileDiffuseMap"];
	tile->NormalTexture = &mTextures[L"tileNormalMap"];
	tile->FresnelR0 = XMFLOAT3(0.002f, 0.002f, 0.002f);
	tile->Roughness = 0.1f;
	tile->Metallic = 0.0f;
	mMaterials[L"tile"] = *tile.get();

	//-----------------------------------------------
	auto semlcibb = std::make_unique<Material>();
	semlcibb->SetName(L"semlcibb");
	semlcibb->MatCBIndex = 4;
	semlcibb->DiffuseTexture = &mTextures[L"semlcibb_Albedo"];
	semlcibb->NormalTexture = &mTextures[L"semlcibb_Normal"];
	semlcibb->RoughnessTexture = &mTextures[L"semlcibb_Roughness"];
	semlcibb->SpecularTexture = &mTextures[L"semlcibb_Specular"];
	semlcibb->DisplacementTexture = &mTextures[L"semlcibb_Displacement"];
	semlcibb->FresnelR0 = XMFLOAT3(0.002f, 0.002f, 0.002f);
	semlcibb->Roughness = 0.1f;
	semlcibb->Metallic = 0.0f;
	mMaterials[L"semlcibb"] = *semlcibb.get();

	auto rustediron = std::make_unique<Material>();
	rustediron->SetName(L"rustediron");
	rustediron->MatCBIndex = 5;
	rustediron->DiffuseTexture = &mTextures[L"rustediron_basecolor"];
	rustediron->NormalTexture = &mTextures[L"rustediron_normal"];
	rustediron->MetallicTexture = &mTextures[L"rustediron_metallic"];
	rustediron->RoughnessTexture = &mTextures[L"rustediron_roughness"];
	rustediron->FresnelR0 = XMFLOAT3(0.002f, 0.002f, 0.002f);
	rustediron->Roughness = 0.1f;
	rustediron->Metallic = 0.0f;
	mMaterials[L"rustediron"] = *rustediron.get();
	
	auto Concrete_Dirty = std::make_unique<Material>();
	Concrete_Dirty->SetName(L"Concrete_Dirty");
	Concrete_Dirty->MatCBIndex = 6;
	Concrete_Dirty->DiffuseTexture = &mTextures[L"Concrete_Dirty_Albedo"];
	Concrete_Dirty->NormalTexture = &mTextures[L"Concrete_Dirty_Normal"];
	Concrete_Dirty->RoughnessTexture = &mTextures[L"Concrete_Dirty_Roughness"];
	Concrete_Dirty->SpecularTexture = &mTextures[L"Concrete_Dirty_Specular"];
	Concrete_Dirty->DisplacementTexture = &mTextures[L"Concrete_Dirty_Displacement"];
	Concrete_Dirty->FresnelR0 = XMFLOAT3(0.002f, 0.002f, 0.002f);
	Concrete_Dirty->Roughness = 0.1f;
	Concrete_Dirty->Metallic = 0.0f;
	mMaterials[L"Concrete_Dirty"] = *Concrete_Dirty.get();

	auto Concrete_Rough = std::make_unique<Material>();
	Concrete_Rough->SetName(L"Concrete_Rough");
	Concrete_Rough->MatCBIndex = 7;
	Concrete_Rough->DiffuseTexture = &mTextures[L"Concrete_Rough_Albedo"];
	Concrete_Rough->NormalTexture = &mTextures[L"Concrete_Rough_Normal"];
	Concrete_Rough->RoughnessTexture = &mTextures[L"Concrete_Rough_Roughness"];
	Concrete_Rough->SpecularTexture = &mTextures[L"Concrete_Rough_Specular"];
	Concrete_Rough->DisplacementTexture = &mTextures[L"Concrete_Rough_Displacement"];
	Concrete_Rough->FresnelR0 = XMFLOAT3(0.002f, 0.002f, 0.002f);
	Concrete_Rough->Roughness = 0.1f;
	Concrete_Rough->Metallic = 0.0f;
	mMaterials[L"Concrete_Rough"] = *Concrete_Rough.get();

	auto Grass_Wild = std::make_unique<Material>();
	Grass_Wild->SetName(L"Grass_Wild");
	Grass_Wild->MatCBIndex = 8;
	Grass_Wild->DiffuseTexture = &mTextures[L"Grass_Wild_Albedo"];
	Grass_Wild->NormalTexture = &mTextures[L"Grass_Wild_Normal"];
	Grass_Wild->RoughnessTexture = &mTextures[L"Grass_Wild_Roughness"];
	Grass_Wild->SpecularTexture = &mTextures[L"Grass_Wild_Specular"];
	Grass_Wild->DisplacementTexture = &mTextures[L"Grass_Wild_Displacement"];
	Grass_Wild->FresnelR0 = XMFLOAT3(0.002f, 0.002f, 0.002f);
	Grass_Wild->Roughness = 0.1f;
	Grass_Wild->Metallic = 0.0f;
	mMaterials[L"Grass_Wild"] = *Grass_Wild.get();

	auto rockcopper = std::make_unique<Material>();
	rockcopper->SetName(L"rockcopper");
	rockcopper->MatCBIndex = 9;
	rockcopper->DiffuseTexture = &mTextures[L"copper-rock1-alb"];
	rockcopper->NormalTexture = &mTextures[L"copper-rock1-normal"];
	rockcopper->MetallicTexture = &mTextures[L"copper-rock1-metal"];
	rockcopper->RoughnessTexture = &mTextures[L"copper-rock1-rough"];
	rockcopper->FresnelR0 = XMFLOAT3(0.002f, 0.002f, 0.002f);
	rockcopper->Roughness = 0.1f;
	rockcopper->Metallic = 0.0f;
	mMaterials[L"rockcopper"] = *rockcopper.get();

	auto Stone_Wall = std::make_unique<Material>();
	Stone_Wall->SetName(L"Stone_Wall");
	Stone_Wall->MatCBIndex = 10;
	Stone_Wall->DiffuseTexture = &mTextures[L"Stone_Wall_Albedo"];
	Stone_Wall->NormalTexture = &mTextures[L"Stone_Wall_Normal"];
	Stone_Wall->RoughnessTexture = &mTextures[L"Stone_Wall_Roughness"];
	Stone_Wall->SpecularTexture = &mTextures[L"Stone_Wall_Specular"];
	Stone_Wall->DisplacementTexture = &mTextures[L"Stone_Wall_Displacement"];
	Stone_Wall->FresnelR0 = XMFLOAT3(0.002f, 0.002f, 0.002f);
	Stone_Wall->Roughness = 0.1f;
	Stone_Wall->Metallic = 0.0f;
	mMaterials[L"Stone_Wall"] = *Stone_Wall.get();

	auto Metal_Bare = std::make_unique<Material>();
	Metal_Bare->SetName(L"Metal_Bare");
	Metal_Bare->MatCBIndex = 11;
	Metal_Bare->DiffuseTexture = &mTextures[L"Metal_Bare_Albedo"];
	Metal_Bare->NormalTexture = &mTextures[L"Metal_Bare_Normal"];
	Metal_Bare->RoughnessTexture = &mTextures[L"Metal_Bare_Roughness"];
	Metal_Bare->SpecularTexture = &mTextures[L"Metal_Bare_Specular"];
	Metal_Bare->DisplacementTexture = &mTextures[L"Metal_Bare_Displacement"];
	Metal_Bare->FresnelR0 = XMFLOAT3(0.002f, 0.002f, 0.002f);
	Metal_Bare->Roughness = 0.1f;
	Metal_Bare->Metallic = 0.0f;
	mMaterials[L"Metal_Bare"] = *Metal_Bare.get();

	UINT j = 12;
	for (UINT i = 8; i < 72; i++)
	{
		auto redSphere = std::make_unique<Material>();
		redSphere->SetName(L"sphere_" + std::to_wstring(i));
		redSphere->MatCBIndex = j;
		redSphere->DiffuseTexture = &mTextures[L"DiffuseMap"];
		redSphere->NormalTexture = &mTextures[L"defaultNmap"];
		redSphere->FresnelR0 = { 0.04f, 0.04f, 0.04f };
		redSphere->Roughness = (i % 8) / 6.0f;
		redSphere->Metallic = 1.0f - (i / 8) / 6.0f;
		mMaterials[redSphere->GetName()] = *redSphere.get();
		j++;
	}

}

void D3DWindow::BuildLight()
{
	mLights[L"0"].LitCBIndex = 0;
	mLights[L"0"].Direction = {0.57735f, -0.57735f, 0.57735f};
	mLights[L"0"].Strength = { 0.25f, 0.25f, 0.25f };
	mLights[L"1"].LitCBIndex = 1;
	mLights[L"1"].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mLights[L"1"].Strength = { 0.25f, 0.25f, 0.25f };
	mLights[L"2"].LitCBIndex = 2;
	mLights[L"2"].Direction = { 0.57735f, -0.57735f, -0.57735f };
	mLights[L"2"].Strength = { 0.25f, 0.25f, 0.25f };
}

void D3DWindow::AddLight(Light* light)
{
	mLights[light->GetName()] = *light;
}

void D3DWindow::UpdateFrameResources()
{
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = 0;
	
	for (int i = 0; i < SwapChainBufferCount; ++i)
	{
		mFrameResources[i].Update(d3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size());

		if(mAllRitems.size()>0)
			cbAddress = mFrameResources[i].ObjectCB->Resource()->GetGPUVirtualAddress();
	}

	// 缓冲区中第 i 个对象常量缓冲区的偏移量。
	UINT boxCBufIndex = 0;
	UINT objCBByteSize = CalculateConstantBufferByteSize(sizeof(ObjectConstants));
	cbAddress += boxCBufIndex * objCBByteSize;

	// 偏移到第i个对象常量缓冲区。
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = cbAddress;
	cbvDesc.SizeInBytes = objCBByteSize;

	d3dDevice->CreateConstantBufferView(
		&cbvDesc,
		CbvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}

void D3DWindow::BuildAndGenerateObjects()
{
	skyObj.Material = &mMaterials[L"sky"];
	skyObj.AggrObject = &AggrObject[0];//

	boxObj.Material = &mMaterials[L"box"];
	boxObj.AggrObject = &AggrObject[1];

	plObj.Material = &mMaterials[L"tile"];
	plObj.AggrObject = &AggrObject[2];

	//------------------------------------------------
	SphereObj[0].Material = &mMaterials[L"semlcibb"];
	SphereObj[0].AggrObject = &AggrObject[0];
	SphereObj[1].Material = &mMaterials[L"rustediron"];
	SphereObj[1].AggrObject = &AggrObject[0];
	SphereObj[2].Material = &mMaterials[L"Concrete_Dirty"];
	SphereObj[2].AggrObject = &AggrObject[0];
	SphereObj[3].Material = &mMaterials[L"Concrete_Rough"];
	SphereObj[3].AggrObject = &AggrObject[0];
	SphereObj[4].Material = &mMaterials[L"Grass_Wild"];
	SphereObj[4].AggrObject = &AggrObject[0];
	SphereObj[5].Material = &mMaterials[L"rockcopper"];
	SphereObj[5].AggrObject = &AggrObject[0];
	SphereObj[6].Material = &mMaterials[L"Stone_Wall"];
	SphereObj[6].AggrObject = &AggrObject[0];
	SphereObj[7].Material = &mMaterials[L"Metal_Bare"];
	SphereObj[7].AggrObject = &AggrObject[0];
	for(UINT i = 8; i < 72; i++)
	{
		SphereObj[i].Material = &mMaterials[L"sphere_" + std::to_wstring(i)];
		SphereObj[i].AggrObject = &AggrObject[0];
	}
}

void D3DWindow::BuildRenderItems()
{
	auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->WorldTransform, XMMatrixScaling(8000.0f, 8000.0f, 8000.0f));
	XMStoreFloat4x4(&skyRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	skyRitem->ObjCBIndex = ObjCBCount;
	skyRitem->Obj = &skyObj;
	skyRitem->Geo = &mGeometries[L"shapeGeo"];
	skyRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"skyRitem"] = *skyRitem.get();
	mRitemLayer[天空渲染项目][L"Sky"] = &mAllRitems[L"skyRitem"];
	ObjCBCount++;

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 1.0f, 10.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxRitem->ObjCBIndex = ObjCBCount;
	boxRitem->Obj = &boxObj;
	boxRitem->Geo = &mGeometries[L"shapeGeo"];
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"boxRitem"] = *boxRitem.get();
	mRitemLayer[不透明物体渲染项目][L"Box"] = &mAllRitems[L"boxRitem"];
	ObjCBCount++;

	auto plRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&plRitem->WorldTransform, XMMatrixScaling(42.0f, 42.0f, 42.0f) * XMMatrixTranslation(0.0f, -1.0f, 10.0f));
	XMStoreFloat4x4(&plRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	plRitem->ObjCBIndex = ObjCBCount;
	plRitem->Obj = &plObj;
	plRitem->Geo = &mGeometries[L"shapeGeo"];
	plRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"plRitem"] = *plRitem.get();
	mRitemLayer[不透明物体渲染项目][L"Pl"] = &mAllRitems[L"plRitem"];
	ObjCBCount++;

	//-------------------------------------------------------------------
	auto SphereRitem1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&SphereRitem1->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(3.0f, 68.0f, 10.0f));
	XMStoreFloat4x4(&SphereRitem1->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	SphereRitem1->ObjCBIndex = ObjCBCount;
	SphereRitem1->Obj = &SphereObj[0];
	SphereRitem1->Geo = &mGeometries[L"shapeGeo"];
	SphereRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"SphereRitem1"] = *SphereRitem1.get();
	mRitemLayer[不透明物体渲染项目][L"Sphere1"] = &mAllRitems[L"SphereRitem1"];
	ObjCBCount++;

	auto SphereRitem2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&SphereRitem2->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-3.0f, 68.0f, 10.0f));
	XMStoreFloat4x4(&SphereRitem2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	SphereRitem2->ObjCBIndex = ObjCBCount;
	SphereRitem2->Obj = &SphereObj[1];
	SphereRitem2->Geo = &mGeometries[L"shapeGeo"];
	SphereRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"SphereRitem2"] = *SphereRitem2.get();
	mRitemLayer[不透明物体渲染项目][L"Sphere2"] = &mAllRitems[L"SphereRitem2"];
	ObjCBCount++;

	auto SphereRitem3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&SphereRitem3->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-9.0f, 68.0f, 10.0f));
	XMStoreFloat4x4(&SphereRitem3->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	SphereRitem3->ObjCBIndex = ObjCBCount;
	SphereRitem3->Obj = &SphereObj[2];
	SphereRitem3->Geo = &mGeometries[L"shapeGeo"];
	SphereRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"SphereRitem3"] = *SphereRitem3.get();
	mRitemLayer[不透明物体渲染项目][L"Sphere3"] = &mAllRitems[L"SphereRitem3"];
	ObjCBCount++;

	auto SphereRitem4 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&SphereRitem4->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(9.0f, 68.0f, 10.0f));
	XMStoreFloat4x4(&SphereRitem4->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	SphereRitem4->ObjCBIndex = ObjCBCount;
	SphereRitem4->Obj = &SphereObj[3];
	SphereRitem4->Geo = &mGeometries[L"shapeGeo"];
	SphereRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"SphereRitem4"] = *SphereRitem4.get();
	mRitemLayer[不透明物体渲染项目][L"Sphere4"] = &mAllRitems[L"SphereRitem4"];
	ObjCBCount++;

	auto SphereRitem5 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&SphereRitem5->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-15.0f, 68.0f, 10.0f));
	XMStoreFloat4x4(&SphereRitem5->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	SphereRitem5->ObjCBIndex = ObjCBCount;
	SphereRitem5->Obj = &SphereObj[4];
	SphereRitem5->Geo = &mGeometries[L"shapeGeo"];
	SphereRitem5->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"SphereRitem5"] = *SphereRitem5.get();
	mRitemLayer[不透明物体渲染项目][L"Sphere5"] = &mAllRitems[L"SphereRitem5"];
	ObjCBCount++;

	auto SphereRitem6 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&SphereRitem6->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(15.0f, 68.0f, 10.0f));
	XMStoreFloat4x4(&SphereRitem6->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	SphereRitem6->ObjCBIndex = ObjCBCount;
	SphereRitem6->Obj = &SphereObj[5];
	SphereRitem6->Geo = &mGeometries[L"shapeGeo"];
	SphereRitem6->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"SphereRitem6"] = *SphereRitem6.get();
	mRitemLayer[不透明物体渲染项目][L"Sphere6"] = &mAllRitems[L"SphereRitem6"];
	ObjCBCount++;

	auto SphereRitem7 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&SphereRitem7->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f)* XMMatrixTranslation(-21.0f, 68.0f, 10.0f));
	XMStoreFloat4x4(&SphereRitem7->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	SphereRitem7->ObjCBIndex = ObjCBCount;
	SphereRitem7->Obj = &SphereObj[6];
	SphereRitem7->Geo = &mGeometries[L"shapeGeo"];
	SphereRitem7->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"SphereRitem7"] = *SphereRitem7.get();
	mRitemLayer[不透明物体渲染项目][L"Sphere7"] = &mAllRitems[L"SphereRitem7"];
	ObjCBCount++;

	auto SphereRitem8 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&SphereRitem8->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f)* XMMatrixTranslation(21.0f, 68.0f, 10.0f));
	XMStoreFloat4x4(&SphereRitem8->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	SphereRitem8->ObjCBIndex = ObjCBCount;
	SphereRitem8->Obj = &SphereObj[7];
	SphereRitem8->Geo = &mGeometries[L"shapeGeo"];
	SphereRitem8->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mAllRitems[L"SphereRitem8"] = *SphereRitem8.get();
	mRitemLayer[不透明物体渲染项目][L"Sphere8"] = &mAllRitems[L"SphereRitem8"];
	ObjCBCount++;

	for (UINT i = 8; i < 72; i++)
	{
		auto SphereRitemP = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&SphereRitemP->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-21.0f + (i % 8) * 6.0f, 68.0f - (i / 8) * 6.0f, 10.0f));
		XMStoreFloat4x4(&SphereRitemP->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		SphereRitemP->ObjCBIndex = ObjCBCount;
		SphereRitemP->Obj = &SphereObj[i];
		SphereRitemP->Geo = &mGeometries[L"shapeGeo"];
		SphereRitemP->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		mAllRitems[L"SphereRitem" + std::to_wstring(i + 1)] = *SphereRitemP.get();
		mRitemLayer[不透明物体渲染项目][L"Sphere" + std::to_wstring(i + 1)] = &mAllRitems[L"SphereRitem" + std::to_wstring(i + 1)];
		ObjCBCount++;
	}
}

void D3DWindow::AddRenderItem(std::wstring meshName, ObjectCollection* Obj, UINT renderLayerIndex)
{
	Obj->Material = &mMaterials[L"autoMat"];

	auto Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Ritem->WorldTransform, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&Ritem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	Ritem->ObjCBIndex = ObjCBCount;
	Ritem->Obj = Obj;
	Ritem->Geo = &mGeometries[meshName + L" Geo"];
	Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	mAllRitems[meshName] = *Ritem.get();
	mRitemLayer[renderLayerIndex][meshName] = &mAllRitems[meshName];
	ObjCBCount++;
}

void D3DWindow::RemoveRenderItem(std::wstring meshName, UINT renderLayerIndex)
{
	mRitemLayer[renderLayerIndex].erase(meshName);
	mAllRitems.erase(meshName);
	ObjCBCount--;
}

std::wstring D3DWindow::GetMaterialName(std::wstring meshName)
{
	return mAllRitems[meshName].Obj->Material->GetName();
}

void D3DWindow::SetMaterial(std::wstring meshName, std::wstring materialName)
{
	mAllRitems[meshName].Obj->Material = &mMaterials[materialName];
}

std::vector<std::wstring> D3DWindow::GetMaterialNameList()
{
	std::vector<std::wstring> NameList(mMaterials.size());
	UINT i = 0;
	for (auto mat = mMaterials.begin(); mat != mMaterials.end(); mat++)
	{
		NameList[i] = mat->first;
		i++;
	}
	return NameList;
}

//void D3DWindow::SetStartIndexLocation(UINT value)
//{
//	StartIndexLocation = value;
//}
//
//void D3DWindow::SetBaseVertexLocation(INT value)
//{
//	BaseVertexLocation = value;
//}
//
//UINT D3DWindow::GetStartIndexLocation()
//{
//	return StartIndexLocation;
//}
//
//INT D3DWindow::GetBaseVertexLocation()
//{
//	return BaseVertexLocation;
//}

ID3D12Resource* D3DWindow::GetRenderTargetBuffer()
{
	return mSwapChainBuffer->Get();
}

ID3D12Resource* D3DWindow::GetDepthStencilBuffer()
{
	return mDepthStencilBuffer.Get();
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

	for (int i = 0; i < NumContexts; i++)
	{
		workerBeginRecordCommand[i] = CreateEvent(
			NULL,
			FALSE,
			FALSE,
			NULL);

		workerFinishedRecordCommand[i] = CreateEvent(
			NULL,
			FALSE,
			FALSE,
			NULL);

		threadParameters[i].threadIndex = i;

		threadHandles[i] = reinterpret_cast<HANDLE>(_beginthreadex(
			nullptr,
			0,
			threadwrapper::thunk,
			reinterpret_cast<LPVOID>(&threadParameters[i]),
			0,
			nullptr));

		assert(workerBeginRecordCommand[i] != NULL);
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
	auto ri = mAllRitems.begin();
	for (int count = 0; count < mAllRitems.size(); count++)
	{
		//仅在常量更改后才更新cbuffer数据。
		//需要针对每个帧资源进行跟踪。
		if (FreshenAllObject)
			ri->second.NumFramesDirty = SwapChainBufferCount;
		if (ri->second.NumFramesDirty > 0)
		{
			XMMATRIX WorldTransform = XMLoadFloat4x4(&ri->second.WorldTransform);
			XMMATRIX texTransform = XMLoadFloat4x4(&ri->second.TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.WorldTransform, XMMatrixTranspose(WorldTransform));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = ri->second.Obj->Material->MatCBIndex;

			currObjectCB->CopyData(ri->second.ObjCBIndex, objConstants);

			//下一帧资源也需要更新。
			ri->second.NumFramesDirty--;
		}
		ri++;
	}
	FreshenAllObject = false;
}

void D3DWindow::FreshenObjectCBs()
{
	FreshenAllObject = true;
}

void D3DWindow::UpdateMaterialCBs()
{
	auto currMaterialCB = CurrFrameResource->MaterialCB.get();

	for (auto& e : mMaterials)
	{
		//仅在常量更改后才更新cbuffer数据。
		//如果cbuffer数据更改，则需要为每个FrameResource更新。
		Material* mat = &e.second;
		if (FreshenAllMaterial)
			mat->NumFramesDirty = SwapChainBufferCount;
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));
			matConstants.SkyMapIndex = SkyMapIndex;
			matConstants.DiffuseMapIndex = mat->DiffuseTexture->GetIndex();
			if (mat->NormalTexture)
				matConstants.NormalMapIndex = mat->NormalTexture->GetIndex();
			if (mat->SpecularTexture)
				matConstants.SpecularMapIndex = mat->SpecularTexture->GetIndex();
			if (mat->MetallicTexture)
				matConstants.MetallicMapIndex = mat->MetallicTexture->GetIndex();
			if (mat->RoughnessTexture)
				matConstants.RoughnessMapIndex = mat->RoughnessTexture->GetIndex();
			if (mat->DisplacementTexture)
				matConstants.DisplacementMapIndex = mat->DisplacementTexture->GetIndex();
			if (mat->BumpTexture)
				matConstants.BumpMapIndex = mat->BumpTexture->GetIndex();
			if (mat->AmbientOcclusionTexture)
				matConstants.AmbientOcclusionMapIndex = mat->AmbientOcclusionTexture->GetIndex();
			if (mat->CavityTexture)
				matConstants.CavityMapIndex = mat->CavityTexture->GetIndex();
			if (mat->SheenTexture)
				matConstants.SheenMapIndex = mat->SheenTexture->GetIndex();
			if (mat->EmissiveTexture)
				matConstants.EmissiveMapIndex = mat->EmissiveTexture->GetIndex();

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			//下一帧资源也需要更新。
			mat->NumFramesDirty--;
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

	auto li = mLights.begin();
	for (int count = 0; count < mLights.size(); count++)
	{		
		//仅在常量更改后才更新cbuffer数据。
		//需要针对每个帧资源进行跟踪。
		if (FreshenAllObject)
			li->second.NumFramesDirty = SwapChainBufferCount;
		if (li->second.NumFramesDirty > 0)
		{
			LightConstants LightCB;

			LightCB.AmbientLight = { 0.5f, 0.5f, 0.5f, 1.0f };
			LightCB.Lights[count].Strength = li->second.Strength;
			LightCB.Lights[count].FalloffStart = li->second.FalloffStart;
			LightCB.Lights[count].Direction = li->second.Direction;
			LightCB.Lights[count].FalloffEnd = li->second.FalloffEnd;
			LightCB.Lights[count].SpotPower = li->second.SpotPower;
			currLightCB->CopyData(0, LightCB);
		}
	}
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

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2(
		(float)EngineHelpers::GetContextWidth(m_hwnd),
		(float)EngineHelpers::GetContextHeight(m_hwnd));
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(
		1.0f / (float)EngineHelpers::GetContextWidth(m_hwnd),
		1.0f / (float)EngineHelpers::GetContextHeight(m_hwnd));

	auto currPassCB = CurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void D3DWindow::Update()
{
	UpdateCamera();
	
	//循环遍历图形框架资源数组。
	CurrFrameResource = &mFrameResources[mCurrBackBufferIndex];

	// GPU是否已完成对当前帧资源的命令的处理？
	//如果没有，请等到GPU完成命令直到该防护点为止。
	if (CurrFrameResource->Fence != 0 && m_fence->GetCompletedValue() < CurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, L"", false, EVENT_ALL_ACCESS);
		ThrowIfFailed(m_fence->SetEventOnCompletion(CurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs();
	UpdateMaterialCBs();
	UpdateLightCBs();
	UpdateMainPassCB();
}

void D3DWindow::RenderB()
{
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
	for (int i = 0; i < NumContexts; i++)
	{
		ThrowIfFailed(CurrFrameResource->threadCommandAllocators[i]->Reset());
		ThrowIfFailed(CurrFrameResource->threadCommandLists[i]->Reset(CurrFrameResource->threadCommandAllocators[i].Get(), nullptr));
	}
	ThrowIfFailed(CurrFrameResource->EndCommandList->Reset(CurrFrameResource->EndCommandAllocator.Get(), nullptr));

	// 指示资源使用情况的状态转换。
	D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(mSwapChainBuffer[mCurrBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CurrFrameResource->BeginCommandList->ResourceBarrier(1, &Barriers);
	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(mFrameResources[mCurrBackBufferIndex].mCopyTexture.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	CurrFrameResource->MidCommandLidt->ResourceBarrier(1, &Barriers);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), mCurrBackBufferIndex, RtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());
	const float clearColor[] = { 0.120f, 0.345f, 0.935f, 1.00f };
	CurrFrameResource->BeginCommandList->ClearRenderTargetView(rtvHandle, (float*)&clearColor, 0, nullptr);
	CurrFrameResource->BeginCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	for (int i = 0; i < NumContexts; i++)
	{
		SetEvent(workerBeginRecordCommand[i]); // 告诉每个工作线程开始记录命令列表
	}

	// 处理文字部分
	{
		static int frameCnt = 0;
		static float timeElapsed = 0.0f;
		frameCnt ++;

		// Compute averages over one second period.
		if ((m_Timer->TotalTime() - timeElapsed) >= 1.0f)
		{
			float fps = (float)frameCnt; // fps = frameCnt / 1
			float mspf = 1000.0f / fps;

			std::wstring fpsStr = std::to_wstring(fps);
			std::wstring mspfStr = std::to_wstring(mspf);

			Text =
				L"FPS: " + fpsStr +
				L"   MSPF: " + mspfStr + L'\n';

			// Reset for next average.
			frameCnt = 0;
			timeElapsed += 1.0f;
		}
	}
}

void D3DWindow::RenderE()
{	

	if (m_editor)
		m_editor->Render();

	// 指示资源使用情况的状态转换。
	D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(mFrameResources[mCurrBackBufferIndex].mCopyTexture.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	CurrFrameResource->EndCommandList->ResourceBarrier(1, &Barriers);
	
	Barriers = CD3DX12_RESOURCE_BARRIER::Transition(mSwapChainBuffer[mCurrBackBufferIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	CurrFrameResource->EndCommandList->ResourceBarrier(1, &Barriers);

	// 命令录制完毕。
	ThrowIfFailed(CurrFrameResource->BeginCommandList->Close());
	ThrowIfFailed(CurrFrameResource->MidCommandLidt->Close());
	ThrowIfFailed(CurrFrameResource->EndCommandList->Close());

	// 将命令列表添加到队列中以执行。
	// 这里先提交Begin和Mid的命令。
	ID3D12CommandList* commandList[] = { CurrFrameResource->BeginCommandList.Get() };
	CommandQueue->ExecuteCommandLists(_countof(commandList), commandList);

	// 等待工作线程的录制结束事件。
	WaitForMultipleObjects(NumContexts, workerFinishedRecordCommand, TRUE, INFINITE);

	// 组装并提交剩余的命令列表。
	ID3D12CommandList* commandLists[NumContexts + 2] = { nullptr }; // 这里要加上begine、mid，所以加个2
	commandLists[0] = CurrFrameResource->MidCommandLidt.Get();
	for (int i = 0; i < NumContexts; i++)
	{
		commandLists[i + 1] = CurrFrameResource->threadCommandLists[i].Get();
	}
	commandLists[NumContexts + 1]= CurrFrameResource->EndCommandList.Get();
	CommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	SwapChain->Present(0, 0);
	mCurrBackBufferIndex = (mCurrBackBufferIndex + 1) % SwapChainBufferCount;
	//mCurrBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();

	// 提升围栏值以将命令标记到该围栏点。
	CurrFrameResource->Fence = ++m_fenceValue;

	// 将指令添加到命令队列以设置新的围栏点。
	// 因为我们在GPU的时间线上，所以在GPU完成此Signal（）之前的所有命令处理之前，
	// 不会设置新的围栏点。
	ThrowIfFailed(CommandQueue->Signal(m_fence.Get(), m_fenceValue));
}

void D3DWindow::WorkerThread(int threadIndex)
{
	assert(threadIndex >= 0);
	assert(threadIndex < NumContexts);

	while (threadIndex >= 0 && threadIndex < NumContexts)
	{		
		// 等待主线程的开始记录命令列表事件。
		WaitForSingleObject(workerBeginRecordCommand[threadIndex], INFINITE);

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(RtvHeap->GetCPUDescriptorHandleForHeapStart(), mCurrBackBufferIndex, RtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(DsvHeap->GetCPUDescriptorHandleForHeapStart());

		CurrFrameResource->threadCommandLists[threadIndex]->RSSetViewports(1, &m_viewport);
		CurrFrameResource->threadCommandLists[threadIndex]->RSSetScissorRects(1, &m_scissorRect);

		// 指定我们要渲染到的缓冲区。
		CurrFrameResource->threadCommandLists[threadIndex]->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

		// 设置要使用的根签名
		CurrFrameResource->threadCommandLists[threadIndex]->SetGraphicsRootSignature(RootSignature.Get());

		// 切换CBV堆
		CurrFrameResource->threadCommandLists[threadIndex]->SetDescriptorHeaps(1, CbvDescriptorHeap.GetAddressOf());

		CurrFrameResource->threadCommandLists[threadIndex]->SetGraphicsRootDescriptorTable(0, CbvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

		// 切换到我们当前使用的SRV堆
		CurrFrameResource->threadCommandLists[threadIndex]->SetDescriptorHeaps(1, SrvDescriptorHeap.GetAddressOf());

		CurrFrameResource->threadCommandLists[threadIndex]->SetGraphicsRootConstantBufferView(4, CurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
		CurrFrameResource->threadCommandLists[threadIndex]->SetGraphicsRootConstantBufferView(5, CurrFrameResource->LightCB->Resource()->GetGPUVirtualAddress());

		// 绑定此场景中使用的所有材质。对于结构化缓冲区，我们可以绕过堆并设置为根描述符。
		if (CurrFrameResource->MaterialCB)
			CurrFrameResource->threadCommandLists[threadIndex]->SetGraphicsRootShaderResourceView(7, CurrFrameResource->MaterialCB->Resource()->GetGPUVirtualAddress());

		//{
		//	CurrFrameResource->threadCommandLists[threadIndex]->SetGraphicsRootDescriptorTable(2, SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		//	
		//	CurrFrameResource->threadCommandLists[threadIndex]->RSSetViewports(1, &mShadowMap->Viewport());
		//	CurrFrameResource->threadCommandLists[threadIndex]->RSSetScissorRects(1, &mShadowMap->ScissorRect());

		//	// Change to DEPTH_WRITE.
		//	CurrFrameResource->threadCommandLists[threadIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		//		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

		//	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

		//	// Clear the back buffer and depth buffer.
		//	CurrFrameResource->threadCommandLists[threadIndex]->ClearDepthStencilView(mShadowMap->Dsv(),
		//		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		//	// Set null render target because we are only going to draw to
		//	// depth buffer.  Setting a null render target will disable color writes.
		//	// Note the active PSO also must specify a render target count of 0.
		//	CurrFrameResource->threadCommandLists[threadIndex]->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

		//	// Bind the pass constant buffer for the shadow map pass.
		//	auto passCB = CurrFrameResource->PassCB->Resource();
		//	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
		//	CurrFrameResource->threadCommandLists[threadIndex]->SetGraphicsRootConstantBufferView(1, passCBAddress);

		//	CurrFrameResource->threadCommandLists[threadIndex]->SetPipelineState(PipelineState[阴影着色器].Get());
		//	DrawRenderItems(threadCommandLists[threadIndex].Get(), mRitemLayer[(int)RenderLayer::Opaque]);

		//	// Change back to GENERIC_READ so we can read the texture in a shader.
		//	CurrFrameResource->threadCommandLists[threadIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		//		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
		//}

		// 绑定天空立方体贴图。对于我们的演示，我们只使用一个代表环境的“全局”立方体贴图
		// 如果我们想使用“局部”立方体贴图，
		// 我们必须针对每个对象更改它们，或者动态索引到立方体贴图数组中。
		skyTexDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
		skyTexDescriptor.Offset(mSkyTexHeapIndex, CbvSrvUavDescriptorSize);
		CurrFrameResource->threadCommandLists[threadIndex]->SetGraphicsRootDescriptorTable(1, skyTexDescriptor);

		// 绑定该场景中使用的所有纹理。请注意，我们只需指定表中的第一个描述符。
		// 根签名知道表中需要多少个描述符。
		otherTexDescriptor = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
		// 所以下面这句就不需要
		//otherTexDescriptor.Offset(mSkyTexHeapIndex + 1, CbvSrvUavDescriptorSize);
		CurrFrameResource->threadCommandLists[threadIndex]->SetGraphicsRootDescriptorTable(2, otherTexDescriptor);

		CurrFrameResource->threadCommandLists[threadIndex]->SetPipelineState(PipelineState[天空管道].Get());
		DrawRenderItems(CurrFrameResource->threadCommandLists[threadIndex].Get(), mRitemLayer[天空渲染项目]);

		CurrFrameResource->threadCommandLists[threadIndex]->SetPipelineState(PipelineState[不透明物体管道].Get());
		DrawRenderItems(CurrFrameResource->threadCommandLists[threadIndex].Get(), mRitemLayer[不透明物体渲染项目]);

		if(renderFPS)
		{
			CurrFrameResource->threadCommandLists[threadIndex]->SetPipelineState(PipelineState[文字管道].Get());
			textR->DXDrawText(CurrFrameResource->threadCommandLists[threadIndex].Get(), Text, DirectX::XMFLOAT2(0.32f, 0.25f), DirectX::XMFLOAT4{ 1.0f,1.0f,1.0f,1.0f }, mCurrBackBufferIndex);
		}

		CurrFrameResource->threadCommandLists[threadIndex]->Close();
		
		// 告诉主线，完成了录制工作。
		SetEvent(workerFinishedRecordCommand[threadIndex]);

	}
}

void D3DWindow::DestroyRender()
{
	//确保GPU不再引用将由析构函数清除的资源。
	FlushCommandQueue();
}

void D3DWindow::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::unordered_map<std::wstring, RenderItem*>& ritems)
{
	if (ritems.size() <= 0) return;

	UINT objCBByteSize = CalculateConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = CurrFrameResource->ObjectCB->Resource();
	
	//对于每个渲染项目...
	for (auto ri : ritems)
	{
		cmdList->IASetVertexBuffers(0, 1, &ri.second->Geo->vertexBufferView);
		cmdList->IASetIndexBuffer(&ri.second->Geo->indexBufferView);
		cmdList->IASetPrimitiveTopology(ri.second->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri.second->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(3, objCBAddress);

		cmdList->DrawIndexedInstanced(ri.second->Obj->AggrObject->IndexCount, 1, ri.second->Obj->AggrObject->StartIndexLocation, ri.second->Obj->AggrObject->BaseVertexLocation, 0);
	}
}

// 关闭命令列表并进行同步
void D3DWindow::CloseCommandListAndSynchronize()
{
	// 命令列表在记录状态下创建，但是尚无记录。
	// 主循环期望它被关闭，所以现在就关闭它。
	CloseCommandList();

	// 将命令列表添加到队列以供执行。
	ID3D12CommandList* ppCommandLists[] = { MainCommandList.Get() };
	CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// 等待命令列表执行；
	// 我们在主循环中重复使用了相同的命令列表，但就目前而言，我们只想等待安装完成后再继续。
	FlushCommandQueue();
}

void D3DWindow::FlushCommandQueue()
{
	// 等待帧继续下去并不是最佳实践。
	// 这是为简化起见而实现的代码。 D3D12HelloFrameBuffering示例说明了如何使用围墙来有效地利用资源并最大程度地利用GPU。

	// 创建围栏
	ThrowIfFailed(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
	// 发信号并增加围栏值。
	m_fenceValue++;

	ThrowIfFailed(CommandQueue->Signal(m_fence.Get(), m_fenceValue));

	//等待直到上一帧结束。
	if (m_fence->GetCompletedValue() < m_fenceValue)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, L"", false, EVENT_ALL_ACCESS);

		// GPU碰到当前栅栏时触发事件。
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValue, eventHandle));

		//等到GPU击中当前围栏事件。
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

void D3DWindow::ResetCommandList()
{
	//将命令列表重置为准备初始化命令。
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

void D3DWindow::Shutdown()
{
	if (SwapChain) SwapChain->Release();
	if (d3dDevice) d3dDevice->Release();
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
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(RtvHeap->GetCPUDescriptorHandleForHeapStart(), mCurrBackBufferIndex, RtvDescriptorSize);
	return rtv;
}

void D3DWindow::SetSkyTexHeapIndex(UINT value)
{
	mSkyTexHeapIndex = value;
}

void D3DWindow::SetDefHeapTexIndex(UINT value)
{
	mDefHeapTexIndex = value;
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

RenderItem* D3DWindow::GetRenderItems(std::wstring name)
{
	return &mAllRitems[name];
}

void D3DWindow::SetFPSRender(bool enable)
{
	renderFPS = enable;
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

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> D3DWindow::GetStaticSamplers()
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
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		4, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		6, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

// 编译着色器
ComPtr<ID3DBlob> D3DWindow::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	std::wstring hlsl_Path = filename;
	hlsl_Path.append(L".hlsl");

	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(hlsl_Path.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if (errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

//
//	std::wstring spirv_Path = filename;
//	spirv_Path.append(L".spv");
//
//	std::string command = "dxc -spirv -T " + target + " -E " + entrypoint + " " + SString::WstringToUTF8(hlsl_Path) + " -Fo " + SString::WstringToUTF8(spirv_Path) + " -O0";
//	int result = system(command.c_str());
//	if (result != 0)
//		assert(false);
//
//	std::ifstream readFileStream = std::ifstream(spirv_Path, std::ios::ate | std::ios::binary);;
//	BYTE* shaderCode = nullptr;
//	size_t length;
//	if (readFileStream.is_open())
//	{
//
//		readFileStream.seekg(0, std::ios::end);
//		int flenght = (int)readFileStream.tellg();
//
//		readFileStream.seekg(0, std::ios::beg);
//		shaderCode = new BYTE[flenght];
//
//		readFileStream.read((char*)shaderCode, flenght);
//		shaderCode[flenght] = '\0';
//
//		length = flenght;
//	}
//	uint32_t* code = reinterpret_cast<uint32_t*>(shaderCode);
//	// Read SPIR-V from disk or similar.
//	std::vector<uint32_t> spirv_binary;
//	spirv_cross::CompilerHLSL hlsl(code, length / sizeof(uint32_t));
//
//	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
//#if defined( DEBUG ) || defined( _DEBUG )
//	flags |= D3DCOMPILE_DEBUG;
//#endif
//
//	// Set some options.
//	spirv_cross::CompilerHLSL::Options options;
//	options.shader_model = 50;
//	hlsl.set_hlsl_options(options);
//
//	//Compiling Shader
//	ComPtr<ID3DBlob> shaderBlob = nullptr;
//	ComPtr<ID3DBlob> errors;
//	std::string source = hlsl.compile();
//	HRESULT hr = D3DCompile(source.c_str(), source.size(), nullptr, nullptr, nullptr, "main", target.c_str(), flags, 0, &shaderBlob, &errors);
//	if (hr != S_OK)
//	{
//		OutputDebugStringA((char*)errors->GetBufferPointer()); 
//		return nullptr;
//	}
//
//	ThrowIfFailed(hr);
//#pragma endregion

	return byteCode;
}

ComPtr<ID3D12Resource> D3DWindow::CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const void* initData, UINT64 byteSize, ComPtr<ID3D12Resource>& uploadBuffer)
{
	ComPtr<ID3D12Resource> defaultBuffer;

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
	//知道复制已执行后，调用者可以释放uploadBuffer。

	return defaultBuffer;
}
