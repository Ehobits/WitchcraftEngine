#pragma once

#include "D3DHelpers.h"
#include "D3D12_framework.h"
#include "Camera.h"
#include "Engine/EngineUtils.h"
#include "Engine/Timer.h"
#include "Texture.h"
#include "Material.h"
#include "Light.h"
#include "ShadowMap.h"
#include "AmbientOcclusion.h"
#include "TextRender.h"
#include "ModelAnalysis/ImportedAssetTypes.h"
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <map>
#include <unordered_map>

// 着色器编号
#define 天空着色器              UINT(0)
#define 不透明物体着色器         UINT(1)
#define 阴影着色器              UINT(2)
#define 阴影透明通道着色器       UINT(3)
#define 法线绘制着色器          UINT(4)
#define 环境遮蔽着色器          UINT(5)
#define 遮蔽模糊着色器          UINT(6)
#define 文字着色器			  UINT(7)
#define 着色器计数              UINT(8)

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
#define debugrt              UINT(3)
#define 渲染项目计数           UINT(4)

// 工作线程分阶段录制编号
#define 阴影工作阶段           UINT(0)
#define 不透明工作阶段         UINT(1)
#define 法线工作阶段           UINT(2)
#define 工作阶段计数           UINT(3)

// 聚合物体对象
struct AggregateGraphicObj
{
	// 子网格在共享顶点/索引缓冲区中的绘制范围。
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;
};

// 对象集合
struct ObjectCollection
{
	// 渲染项实际引用的材质与几何范围。
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

// 线程数
static const UINT NumContexts = 4;

//存储CPU生成框架命令列表所需的资源。
struct FrameResource
{
public:
	FrameResource();
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	// 为一帧分配常量缓冲和命令录制资源。
	void Create(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);

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
	ComPtr<ID3D12CommandAllocator> shadowThreadCommandAllocators[NumContexts] = { nullptr };
	ComPtr<ID3D12GraphicsCommandList> shadowThreadCommandLists[NumContexts] = { nullptr };
	ComPtr<ID3D12CommandAllocator> normalThreadCommandAllocators[NumContexts] = { nullptr };
	ComPtr<ID3D12GraphicsCommandList> normalThreadCommandLists[NumContexts] = { nullptr };

	//围栏值以将命令标记到该围栏点。
	//这可以让我们检查GPU是否仍在使用这些帧资源。
	UINT64 Fence = 0;
};

class Editor;
class SceneEntityBase;
class WitchcraECS;
struct Transform;
struct WMaterialFileData;

class D3DWindow
{
public:
	D3DWindow();
	~D3DWindow();

	// 初始化 D3D12 窗口、交换链、资源和默认场景数据。
	bool Create(HWND hWnd, Timer* timer, Editor* editor = nullptr);
	void CreateCommandQueueAndSwapChain();
	void CreateDevice();
	void CreateDescriptorHeaps();

	// 窗口尺寸或全屏状态变化时重建渲染目标。
	void OnResize(bool Fullscreen = false);

	void SetFullscreen();

	// 创建根签名
	void CreateRootSignature();

	// 创建顶点布局和管道
	void CreatePipesAndShaders();

	void CreateSRVDescriptorHeap();

	// 读取贴图
	void LoadTextures();
	void AddShapeGeometry();
	void AddShapeGeometry(MeshGeometry* geo);
	void RemoveShapeGeometry(std::wstring name);
	void BuildMaterials();
	void BuildLight();
	void AddLight(Light* light);
	void RebuildRenderItemsFromEntities(const std::vector<SceneEntityBase*>& rootEntities, WitchcraECS* ecs = nullptr);
	// 将不透明渲染项重新均分到工作线程批次中。
	void RebuildOpaqueThreadBatches();
	// 创建框架资源
	void CreateFrameResources();
	void AddRenderItem(std::wstring meshName, ObjectCollection* Obj, UINT renderLayerIndex);
	void AddRenderItem(std::wstring renderItemName, ObjectCollection* Obj, const std::wstring& geometryName,
		UINT renderLayerIndex, const DirectX::XMFLOAT4X4* worldTransform,
		const DirectX::XMFLOAT4X4* texTransform, const std::wstring* materialName);
	void RemoveRenderItem(std::wstring meshName, UINT renderLayerIndex);

	std::wstring GetMaterialName(std::wstring meshName);
	void SetMaterial(std::wstring meshName, std::wstring materialName);
	// 为导入的模型创建一份材质，并自动准备所需纹理。
	std::wstring CreateMaterialFromImport(const std::wstring& Name, const ImportedMaterialInfo& materialInfo);
	// 从 .wmat 文件读取材质并按需创建运行时材质。
	std::wstring GetOrCreateMaterialFromWMaterialFile(const std::filesystem::path& materialFilePath);
	std::wstring GetMaterialFilePathByRuntimeMaterialName(const std::wstring& runtimeMaterialName) const;
	std::wstring GetSkyTexturePathByRuntimeMaterialName(const std::wstring& runtimeMaterialName) const;
	std::wstring GetOrCreateSkyMaterial(const std::wstring& skyTexturePath);

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
	DXGI_FORMAT GetBackBufferFormat() { return BackBufferFormat; }
	DXGI_FORMAT GetDepthStencilFormat() { return DepthStencilFormat; }
	
	// 创建后台工作线程，用于分阶段录制命令列表。
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
	void UpdateShadowTransform();
	void UpdateShadowPassCB();
	void UpdateAOCB();
	void Update();
	void RenderB();
	void RenderE();

	void DestroyRender();

	// 关闭主命令列表、提交到队列并等待 GPU 完成。
	void CloseCommandListAndSynchronize();

	// 刷新命令队列（等待上一帧）
	void FlushCommandQueue();

	// 重置命令列表
	void ResetCommandList();
	// 关闭命令列表
	void CloseCommandList();

	bool IsCommandListClose();

	// --------------------------------------------------------------------------
	HWND GethWnd();

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv()const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv()const;

	UINT GetRtvDescriptorSize();
	UINT GetDsvDescriptorSize();
	UINT GetCbvSrvUavDescriptorSize();
	DXGI_FORMAT GetIndexBufferFormat() const;
	AggregateGraphicObj* GetAggregateGraphicObj(const std::wstring& geometryName);

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
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:
	// 按 PSO 绘制一组 RenderItem。
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, ComPtr<ID3D12PipelineState> pipelineState, UINT pipelineNumber);
	static DirectX::XMFLOAT4X4 BuildWorldMatrixFromTransformData(const Transform& transform);
	static TextureType ResolveTextureTypeFromPath(const std::wstring& path);
	static std::wstring MakeUniqueName(const std::unordered_map<std::wstring, Material>& materials, const std::wstring& baseName);
	static std::wstring NormalizeAssetPath(const std::wstring& path);
	static std::vector<RenderItem*> CollectRenderItems(const std::map<std::wstring, RenderItem*>& renderItemMap);
	static ImportedTextureSource BuildImportedTextureSourceFromMaterialFile(const std::filesystem::path& materialFilePath, const std::wstring& textureName);
	static ImportedMaterialInfo ConvertMaterialFileDataToImportedInfo(const WMaterialFileData& materialData, const std::filesystem::path& materialFilePath);
	// 根据 pass 选择对应线程使用的命令分配器和命令列表。
	ID3D12CommandAllocator* GetWorkerCommandAllocator(UINT passIndex, int threadIndex);
	ID3D12GraphicsCommandList* GetWorkerCommandList(UINT passIndex, int threadIndex);
	// 通知工作线程开始录制指定 pass，并等待全部完成。
	void BeginWorkerPass(UINT passIndex);
	void WaitForWorkerPass();

private:

	HWND m_hwnd = nullptr;

	// Set true to use 4X MSAA (?.1.8).  The default is false.

	bool		m4xMsaaState = false;    // 4X MSAA enabled
	UINT		m4xMsaaQuality = 0;      // quality level of 4X MSAA
	BOOL		fullscreenState = false;
	UINT Width = 0;
	UINT Height = 0;

	D3D_DRIVER_TYPE dxDriverType = D3D_DRIVER_TYPE_HARDWARE;
	D3D_FEATURE_LEVEL dxFeatureLevel = D3D_FEATURE_LEVEL_11_1;
	DXGI_SWAP_EFFECT dxSwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	ComPtr<IDXGIFactory4> dxgiFactory = nullptr;
	ComPtr<IDXGIAdapter1> DeviceAdapter = nullptr;
	ComPtr<IDXGISwapChain3> SwapChain = nullptr;
	static const UINT SwapChainBufferCount = 3;

	UINT CurrBackBufferIndex = 0; // 记录当前后缓冲编号

	ComPtr<ID3D12CommandAllocator> MainCommandAllocator = nullptr; //命令分配器
	ComPtr<ID3D12GraphicsCommandList> MainCommandList = nullptr;
	ComPtr<ID3D12Resource> SwapChainBuffer[SwapChainBufferCount];
	ComPtr<ID3D12Resource> DepthStencilBuffer; // 深度模板缓冲区
	ComPtr<ID3D12Device> d3dDevice = nullptr;
	ComPtr<ID3D12CommandQueue> CommandQueue = nullptr;
	ComPtr<ID3D12RootSignature> RootSignature = nullptr;
	ComPtr<ID3D12PipelineState> PipelineState[管道计数];
	ComPtr<ID3D12PipelineState> debugPipelineState;
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

	// 每个后备缓冲对应一套帧资源。
	std::unordered_map<UINT, FrameResource> mFrameResources;
	FrameResource* CurrFrameResource = nullptr;
	UINT FrameResourceObjectCapacity = 0;
	UINT FrameResourceMaterialCapacity = 0;

	ComPtr<ID3D12DescriptorHeap> SrvDescriptorHeap = nullptr;
	UINT SrvDescriptorHeapIndex = 0;

	std::unordered_map<std::wstring, AggregateGraphicObj> AggrObject;
	UINT ObjCBCount = 0;

	// 几何、纹理组、材质注册表。
	std::unordered_map<std::wstring, MeshGeometry> Geometries;
	// 延迟释放的几何资源：先从可见注册表移除，等 GPU 完成后再真正释放。
	std::vector<MeshGeometry> DeferredReleaseGeometries;
	std::unordered_map<std::wstring, std::vector<Texture>> TextureGroups;
	std::unordered_map<std::wstring, Material> Materials;
	std::unordered_map<std::wstring, std::wstring> MaterialByFilePath;
	std::unordered_map<std::wstring, std::wstring> SkyMaterialByTexturePath;

	DirectX::XMFLOAT4 AmbientColor;

	std::unordered_map<std::wstring, Light> Lights;
	std::vector<XMFLOAT3> RotatedLightDirections;

	XMFLOAT4X4 LightView = MathHelps::Identity;
	XMFLOAT4X4 LightProj = MathHelps::Identity;

	std::vector<XMFLOAT4X4> ShadowTransform = { MathHelps::Identity };
	XMFLOAT3 LightPosW;

	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GPUTexDescriptor;

	UINT SkyMapIndex = 0;

	UINT SkyTexHeapIndex = 0;

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE shadowMapDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE ambientOcclusionDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE otherTexDescriptor;

	// 独立的阴影与 AO 子系统。
	ShadowMap shadowMap;
	AmbientOcclusion ambientOcclusion;

	//所有渲染项的列表。
	std::map<std::wstring, RenderItem> AllRitems;
	bool FreshenAllObject = false;
	bool FreshenAllMaterial = false;
	bool FreshenAllLight = true;

	// 按 PSO 划分渲染项目。
	std::map<std::wstring, RenderItem*> RitemLayer[(UINT)渲染项目计数];
	std::vector<RenderItem*> OpaqueThreadBatches[NumContexts];

	//派生类应在派生构造函数中设置这些值以自定义起始值。
	DXGI_FORMAT IndexBufferFormat = DXGI_FORMAT_R32_UINT;
	DXGI_FORMAT BackBufferFormat;
	DXGI_FORMAT DepthStencilFormat;

	// 线程同步对象
	HANDLE workerBeginRecordCommand[NumContexts] = { nullptr };
	HANDLE workerFinishedRecordCommand[NumContexts] = { nullptr };
	// 同步对象。
	HANDLE threadHandles[NumContexts] = { nullptr };
	HANDLE fenceEvent = nullptr;
	ComPtr<ID3D12Fence> fence = nullptr;
	UINT64 fenceValue = 0;

	struct ThreadParameter
	{
		int threadIndex;
	};
	ThreadParameter threadParameters[NumContexts];
	void ClearRenderItems();
	void AppendRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs = nullptr);

	void WorkerThread(int threadIndex);
	std::atomic<UINT> CurrentWorkerPass = 不透明工作阶段;

	PassConstants MainPassCB; // 主渲染 pass 常量。
	PassConstants ShadowPassCB; // 阴影 pass 常量。

	Camera mCamera;

	Timer* mTimer;
	TextRender* textR = nullptr;
	std::wstring Text = L"";
	bool renderFPS = false;

	Editor* mEditor = nullptr;

public:
	// 编译着色器
	static ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);

	// 创建默认堆资源，并通过 upload buffer 完成一次性上传。
	static ComPtr<ID3D12Resource> CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		ComPtr<ID3D12Resource>& uploadBuffer);
};
