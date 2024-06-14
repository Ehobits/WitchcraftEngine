#pragma once

#include "D3DHelpers.h"
#include "D3D12_framework.h"

#include "Camera.h"

#include "Engine/EngineUtils.h"
#include "Engine/Timer.h"
#include "Texture.h"
#include "Material.h"
#include "Light.h"
#include "TextRender.h"

#include <Windows.h>

// 着色器编号
#define 天空着色器              UINT(0)
#define 不透明物体着色器         UINT(1)
#define 阴影着色器              UINT(2)
#define 阴影透明通道着色器       UINT(3)
#define 环境遮蔽着色器          UINT(4)
#define 遮蔽模糊着色器          UINT(5)
#define 文字着色器			  UINT(6)
#define 着色器计数              UINT(7)

// 管道状态编号
#define 天空管道              UINT(0)
#define 不透明物体管道         UINT(1)
#define 阴影管道              UINT(2)
#define 法线绘制管道          UINT(3)
#define 环境遮蔽管道          UINT(4)
#define 遮蔽模糊管道          UINT(5)
#define 文字管道				 UINT(6)
#define 管道计数              UINT(7)

// 物体类型编号
#define 天空              UINT(0)
#define 地面              UINT(1)
#define 固定景物          UINT(2)
#define 可变化景物         UINT(3)
#define 互动实体          UINT(4)
#define 光照相关          UINT(5)
#define 粒子相关          UINT(6)
#define UI相关            UINT(7)
#define 物体项目计数       UINT(8)

// 渲染项目编号
#define 天空渲染项目           UINT(0)
#define 不透明物体渲染项目      UINT(1)
#define 半透明物体渲染项目      UINT(2)
#define 渲染项目计数           UINT(3)

// 聚合物体对象
struct AggregateGraphicObj
{
	// 用于顶点和索引的记录
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;
};

// 对象集合
struct ObjectCollection
{
	Material* Material = nullptr;
	AggregateGraphicObj* AggrObject = nullptr;
};

// 轻型结构存储参数以绘制形状。
// 这将因应用程序而异。
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	//描述对象相对于世界空间的局部空间的形状的世界矩阵，
	//该世界矩阵定义了对象在世界中的位置，方向和比例。
	XMFLOAT4X4 WorldTransform = MathHelps::Identity;
	XMFLOAT4X4 TexTransform = MathHelps::Identity;

	//指示对象数据已更改的脏标志，我们需要更新常量缓冲区。
	//因为每个FrameResource都有一个对象缓冲区，所以我们必须将更新应用于每个FrameResource。
	//因此，当我们修改对象数据时，我们应该设置NumFramesDirty = gNumFrameResources，
	//以便每个帧资源都能获得更新。
	UINT NumFramesDirty = 3;

	// 索引到与此渲染项的 ObjectCB 相对应的 GPU 常量缓冲区。
	UINT ObjCBIndex = -1;

	ObjectCollection* Obj = nullptr;
	MeshGeometry* Geo = nullptr;

	//基本拓扑。
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced参数。
	//UINT IndexCount = 0;
	//UINT StartIndexLocation = 0;
	//int BaseVertexLocation = 0;
};

// 创建常量缓冲区
template<typename T>
class UploadBuffer
{
public:
	UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
		mIsConstantBuffer(isConstantBuffer)
	{
		mElementByteSize = sizeof(T);

		// Constant buffer elements need to be multiples of 256 bytes.
		// This is because the hardware can only view constant data 
		// at m*256 byte offsets and of n*256 byte lengths. 
		// typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
		// UINT64 OffsetInBytes; // multiple of 256
		// UINT   SizeInBytes;   // multiple of 256
		// } D3D12_CONSTANT_BUFFER_VIEW_DESC;
		if (isConstantBuffer)
			mElementByteSize = CalculateConstantBufferByteSize(sizeof(T));

		D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount);
		ThrowIfFailed(device->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&Desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mUploadBuffer)));

		ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

		// We do not need to unmap until we are done with the resource.  However, we must not write to
		// the resource while it is in use by the GPU (so we must use synchronization techniques).
	}

	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
	~UploadBuffer()
	{
		if (mUploadBuffer != nullptr)
			mUploadBuffer->Unmap(0, nullptr);

		mMappedData = nullptr;
	}

	ID3D12Resource* Resource()const
	{
		return mUploadBuffer.Get();
	}

	void CopyData(int elementIndex, const T& data)
	{
		memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
	}

private:
	ComPtr<ID3D12Resource> mUploadBuffer = nullptr;
	BYTE* mMappedData = nullptr;

	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};

static const UINT NumContexts = 4;

//存储CPU生成框架命令列表所需的资源。
struct FrameResource
{
public:
	FrameResource();
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	void Update(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);

	//在GPU完成处理命令之前，我们无法重置分配器。
	//因此，每个框架都需要有自己的分配器。
	//ComPtr<ID3D12CommandAllocator> CmdListAlloc = nullptr;

	//在GPU完成处理引用它的命令之前，我们无法更新cbuffer。
	// 因此，每个帧都需要自己的cbuffer。    
	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
	std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
	std::unique_ptr<UploadBuffer<LightConstants>> LightCB = nullptr;
	std::unique_ptr<UploadBuffer<AOConstants>> AOCB = nullptr;

	ComPtr<ID3D12Resource> mCopyTexture = nullptr;

	ComPtr<ID3D12CommandAllocator> BeginCommandAllocator = nullptr; //命令分配器
	ComPtr<ID3D12CommandAllocator> MidCommandAllocator = nullptr; //命令分配器
	ComPtr<ID3D12CommandAllocator> EndCommandAllocator = nullptr; //命令分配器
	ComPtr<ID3D12GraphicsCommandList> BeginCommandList = nullptr;
	ComPtr<ID3D12GraphicsCommandList> MidCommandLidt = nullptr;
	ComPtr<ID3D12GraphicsCommandList> EndCommandList = nullptr;

	ComPtr<ID3D12CommandAllocator> threadCommandAllocators[NumContexts] = { nullptr }; //命令分配器
	ComPtr<ID3D12GraphicsCommandList> threadCommandLists[NumContexts] = { nullptr };

	//围栏值以将命令标记到该围栏点。
	//这可以让我们检查GPU是否仍在使用这些帧资源。
	UINT64 Fence = 0;
};

class Editor;

class D3DWindow
{
public:
	D3DWindow();
	~D3DWindow();

	bool Create(HWND hWnd, Timer* timer, Editor* editor = nullptr);
	void CreateCommandQueueAndSwapChain();
	void CreateDevice();
	void CreateDescriptorHeaps();
	void OnResize();
	// 创建根签名
	void CreateRootSignature();
	// 创建描述符堆
	void CreatePipesAndShaders();

	void CreateCBVAndSRVDescriptorHeaps();

	// 读取贴图
	void LoadTextures();
	void AddShapeGeometry();
	void AddShapeGeometry(MeshGeometry* geo);
	void RemoveShapeGeometry(std::wstring name);
	void BuildMaterials();
	void BuildLight();
	void AddLight(Light* light);
	void BuildAndGenerateObjects();
	void BuildRenderItems();
	// 创建框架资源
	void UpdateFrameResources();
	void AddRenderItem(std::wstring meshName, ObjectCollection* Obj, UINT renderLayerIndex);
	void RemoveRenderItem(std::wstring meshName, UINT renderLayerIndex);

	std::wstring GetMaterialName(std::wstring meshName);
	void SetMaterial(std::wstring meshName, std::wstring materialName);

	std::vector<std::wstring> GetMaterialNameList();
	//void SetStartIndexLocation(UINT value);
	//void SetBaseVertexLocation(INT value);

	//UINT GetStartIndexLocation();
	//INT GetBaseVertexLocation();

	ID3D12Resource* GetRenderTargetBuffer();
	ID3D12Resource* GetDepthStencilBuffer();
	ID3D12Device* GetDevice();
	IDXGISwapChain* GetSwapChain();
	ID3D12CommandQueue* GetCommandQueue();
	ID3D12GraphicsCommandList* GetCommandList();
	ID3D12GraphicsCommandList* GetThreadCommandList(int threadIndex);
	ID3D12GraphicsCommandList* GetCurrFrameResourceCommandList();
	CD3DX12_VIEWPORT GetViewport();
	int GetSwapChainBufferCount() { return SwapChainBufferCount; }
	DXGI_FORMAT GetBackBufferFormat() { return mBackBufferFormat; }
	DXGI_FORMAT GetDepthStencilFormat() { return mDepthStencilFormat; }
	void BegineThread();

	void UpdateCamera();
	// 更新对象CB
	void UpdateObjectCBs();
	void FreshenObjectCBs();
	// 更新材质缓冲区
	void UpdateMaterialCBs();
	void FreshenMaterialCBs();
	// 更新光照
	void UpdateLightCBs();
	void FreshenLightCBs();
	// 更新主要通道CB
	void UpdateMainPassCB();
	void Update();
	void RenderB();
	void RenderE();

	void DestroyRender();

	void CloseCommandListAndSynchronize();

	// 刷新命令队列（等待上一帧）
	void FlushCommandQueue();

	// 重置命令列表
	void ResetCommandList();
	// 关闭命令列表
	void CloseCommandList();

	bool IsCommandListClose();

	void Shutdown();

	// --------------------------------------------------------------------------
	HWND GethWnd();

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv()const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv()const;
	void SetSkyTexHeapIndex(UINT value);
	void SetDefHeapTexIndex(UINT value);
	UINT GetRtvDescriptorSize();
	UINT GetDsvDescriptorSize();
	UINT GetCbvSrvUavDescriptorSize();

	RenderItem* GetRenderItems(std::wstring name);

	void SetFPSRender(bool enable);

	// 相机功能
	// --------------------------------------------------------------------------
	
	// 获取相机位置
	DirectX::XMFLOAT3 GetPosition3f()const;
	// 设置相机位置
	void SetPosition3f(DirectX::XMFLOAT3 Position);
	// 获得相机旋转
	DirectX::XMFLOAT3 GetRotation3f()const;
	// 设置相机旋转
	void SetRotation3f(DirectX::XMFLOAT3 Rotation);
	// 获得相机速度
	float GetCameraSpeed();
	// 设置相机速度
	void SetCameraSpeed(float speed);

	// 获取视椎体属性。
	float GetNearZ()const;
	float GetFarZ()const;
	float GetFovY()const;
	float GetFovX()const;

	// 设置视椎体属性。
	void SetNearZ(float nearZ);
	void SetFarZ(float farZ);
	void SetFovY(float fovY);
	
	float GetViewportScale()const;

	void SetViewportScale(float scale);

	void RestoreScale();

	// 获取视图/项目矩阵。
	DirectX::XMMATRIX GetView()const;
	DirectX::XMMATRIX GetProj()const;

	void RotateCamera(float DeltaTime, DirectX::XMFLOAT2 angle);
	void MoveCamera(float DeltaTime, DirectX::XMFLOAT3 distance);

	// 获取静态采样
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::unordered_map<std::wstring, RenderItem*>& ritems);

private:
	HWND m_hwnd = nullptr;

	// Set true to use 4X MSAA (?.1.8).  The default is false.
	bool      m4xMsaaState = false;    // 4X MSAA enabled
	UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

	D3D_DRIVER_TYPE dxDriverType = D3D_DRIVER_TYPE_HARDWARE;
	D3D_FEATURE_LEVEL dxFeatureLevel = D3D_FEATURE_LEVEL_12_0;
	DXGI_SWAP_EFFECT dxSwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	ComPtr<IDXGIFactory4> dxgiFactory = nullptr;
	ComPtr<IDXGIAdapter1> DeviceAdapter = nullptr;
	ComPtr<IDXGISwapChain3> SwapChain = nullptr;
	static const UINT SwapChainBufferCount = 3;
	UINT mCurrBackBufferIndex = 0; // 记录当前后缓冲编号
	ComPtr<ID3D12CommandAllocator> MainCommandAllocator = nullptr; //命令分配器
	ComPtr<ID3D12GraphicsCommandList> MainCommandList = nullptr;
	ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
	ComPtr<ID3D12Resource> mDepthStencilBuffer; // 深度模板缓冲区
	ComPtr<ID3D12Device> d3dDevice = nullptr;
	ComPtr<ID3D12CommandQueue> CommandQueue = nullptr;
	ComPtr<ID3D12RootSignature> RootSignature = nullptr;
	ComPtr<ID3D12PipelineState> PipelineState[管道计数];
	bool CommandListClose = false;

	std::vector<D3D12_INPUT_ELEMENT_DESC> InputElementDescs;

	ComPtr<ID3D12DescriptorHeap> RtvHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> DsvHeap = nullptr;

	UINT RtvDescriptorSize = 0;
	UINT DsvDescriptorSize = 0;
	UINT CbvSrvUavDescriptorSize = 0;

	// 窗口大小和裁切大小
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;

	std::unordered_map<UINT, FrameResource> mFrameResources;
	FrameResource* CurrFrameResource = nullptr;

	ComPtr<ID3D12Resource> m_texture = nullptr;
	ComPtr<ID3D12Resource> mNormalMap = nullptr;

	ComPtr<ID3D12DescriptorHeap> CbvDescriptorHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> SrvDescriptorHeap = nullptr;
	UINT SrvDescriptorHeapIndex = 0;

	ObjectCollection skyObj;
	ObjectCollection boxObj;
	ObjectCollection plObj;
	ObjectCollection SphereObj[72];
	std::unordered_map<UINT, AggregateGraphicObj> AggrObject;
	UINT ObjCBCount = 0;

	std::unordered_map<std::wstring, MeshGeometry> mGeometries;
	std::unordered_map<std::wstring, Texture> mTextures;
	std::unordered_map<std::wstring, Material> mMaterials;
	std::unordered_map<std::wstring, Light> mLights;

	UINT SkyMapIndex = 0;

	UINT mSkyTexHeapIndex = 0;
	UINT mDefHeapTexIndex = 0;

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE otherTexDescriptor;

	//所有渲染项的列表。
	std::unordered_map<std::wstring, RenderItem> mAllRitems;
	bool FreshenAllObject = false;
	bool FreshenAllMaterial = false;
	bool FreshenAllLight = true;

	// Render items divided by PSO.
	std::unordered_map<std::wstring, RenderItem*> mRitemLayer[(UINT)渲染项目计数];

	//派生类应在派生构造函数中设置这些值以自定义起始值。
	DXGI_FORMAT mBackBufferFormat;
	DXGI_FORMAT mDepthStencilFormat;

	// 线程同步对象
	HANDLE workerBeginRecordCommand[NumContexts] = { nullptr };
	HANDLE workerFinishedRecordCommand[NumContexts] = { nullptr };
	// 同步对象。
	HANDLE threadHandles[NumContexts] = { nullptr };
	HANDLE m_fenceEvent = nullptr;
	ComPtr<ID3D12Fence> m_fence = nullptr;
	UINT64 m_fenceValue = 0;

	struct ThreadParameter
	{
		int threadIndex;
	};
	ThreadParameter threadParameters[NumContexts];

	void WorkerThread(int threadIndex);

	PassConstants mMainPassCB;

	Camera mCamera;

	Timer* m_Timer;
	TextRender* textR = nullptr;
	std::wstring Text = L"";
	bool renderFPS = true;
	//----------------------------------------------------------------
	//SkyFile skyFile;
	//Sky sky;
	//Box box;

	Editor* m_editor = nullptr;

public:
	// 编译着色器
	static ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);

	// 创建默认缓冲区
	static ComPtr<ID3D12Resource> CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		ComPtr<ID3D12Resource>& uploadBuffer);
};
