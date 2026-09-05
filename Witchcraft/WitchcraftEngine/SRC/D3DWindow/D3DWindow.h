#pragma once

#include "D3DHelpers.h"
#include "D3D12_framework.h"
#include "Camera.h"
#include "Common/BillboardSharedTypes.h"
#include "Common/GizmoSharedTypes.h"
#include "Common/SkeletonOverlaySharedTypes.h"
#include "Common/SceneEntityType.h"
#include "Engine/EngineUtils.h"
#include "Engine/Timer.h"
#include "Texture.h"
#include "Material.h"
#include "Light.h"
#include "PointLightShadowCubePool.h"
#include "RenderPasses/ShadowFrameBuilder.h"
#include "RenderPasses/ShadowPoolPlanner.h"
#include "RenderPasses/ShadowMapPass.h"
#include "RenderPasses/TextRenderPass.h"
#include "RenderPasses/AmbientOcclusionPass.h"
#include "RenderPasses/SharedNormalPrepass.h"
#include "RenderPasses/DirectionalShadowMaskPass.h"
#include "RenderPasses/InteractionOutlinePass.h"
#include "RenderPasses/SkinningComputePass.h"
#include "RenderPasses/VolumetricLightPass.h"
#include "RenderPasses/GizmoPass.h"
#include "RenderPasses/SkeletonOverlayPass.h"
#include "RenderPasses/SkinWeightVizPass.h"
#include "RenderPasses/ColorAdjustPass.h"
#include "RenderPasses/OITCompositePass.h"
#include "RenderPasses/FXAAPass.h"
#include "RenderPasses/RenderToTexture.h"
#include "D3DRenderBindingContract.h"
#include "ModelAnalysis/ImportedAssetTypes.h"

#include <array>
#include <filesystem>
#include <map>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

// =========================
// 运行时编号常量
// =========================
// 着色器编号
#define 天空着色器               UINT(0)
#define 不透明物体着色器          UINT(1)
#define 透明物体着色器            UINT(2)
#define 半透明物体着色器          UINT(3)
#define 阴影着色器               UINT(4)
#define 阴影透明通道着色器        UINT(5)
#define 法线绘制着色器            UINT(6)
#define 环境遮蔽着色器            UINT(7)
#define 遮蔽模糊着色器            UINT(8)
#define 蒙皮半透明着色器          UINT(9)
#define 着色器计数               UINT(10)

// 管道状态编号
#define 天空管道                UINT(0)
#define 不透明物体管道           UINT(1)
#define 透明物体管道             UINT(2)
#define 半透明物体管道           UINT(3)
#define 阴影管道                UINT(4)
#define 法线绘制管道             UINT(5)
#define 环境遮蔽管道             UINT(6)
#define 遮蔽模糊管道             UINT(7)
#define 体积光管道              UINT(8)
#define 透明合成管道             UINT(9)
#define 后处理管道              UINT(10)
#define 文字管道                UINT(11)
#define 管道计数                UINT(12)

// 渲染项目编号
#define 天空渲染项目             UINT(0)
#define 不透明物体渲染项目        UINT(1)
#define 透明物体渲染项目         UINT(2)
#define debugrt                UINT(3)
#define 渲染项目计数             UINT(4)

// 工作线程分阶段录制编号
#define 阴影工作阶段             UINT(0)
#define 不透明工作阶段           UINT(1)
#define 半透明工作阶段           UINT(2)
#define 透明工作阶段             UINT(3)
#define 法线工作阶段             UINT(4)
#define 工作阶段计数             UINT(5)

class SceneEntityBase;

// =========================
// 渲染数据结构
// =========================
// 聚合物体对象
struct AggregateGraphicObj
{
	// 子网格在共享顶点/索引缓冲区中的绘制范围。
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;
};

// 渲染对象集合:RenderItem 持有对材质与聚合网格片段的引用。
struct ObjectCollection
{
	// 渲染项实际引用的材质与几何范围。
	Material* Material = nullptr;
	AggregateGraphicObj* AggrObject = nullptr;
};

// 渲染项:描述一次 DrawIndexedInstanced 需要的最小信息。
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	// 对象的世界矩阵与纹理矩阵。
	XMFLOAT4X4 WorldTransform = MathHelps::Identity;
	XMFLOAT4X4 TexTransform = MathHelps::Identity;

	// 脏帧计数:对象常量需要同步到每个 FrameResource。
	UINT NumFramesDirty = 3;

	// 索引到与此渲染项的 ObjectCB 相对应的 GPU 常量缓冲区。
	UINT ObjCBIndex = -1;
	// 索引到与此渲染项的 SkinningCB 相对应的 GPU 常量缓冲区。
	UINT SkinningCBIndex = -1;
	// 本地空间包围盒,用于 CPU 侧视锥剔除。
	DirectX::BoundingBox LocalBounds = DirectX::BoundingBox(
		DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
		DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
	bool HasLocalBounds = false;
	bool DisableFrustumCulling = false;

	ObjectCollection* Obj = nullptr;
	MeshGeometry* Geo = nullptr;

	// 基本图元拓扑。
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	SceneEntityType SceneType = SceneEntityType::StaticScenery;
	SceneEntityBase* SourceEntity = nullptr;
	//SourceEntity用于呈现/蒙皮所有权。CullingSourceEntity只是渲染项需要层次感知截锥剔除时使用的ECS子树根。
	SceneEntityBase* CullingSourceEntity = nullptr;
	std::uint32_t ReflectionReceiverRenderToTextureId = 0;
	bool IsSkinned = false;
	bool IsBillboard = false;
	BillboardData Billboard;
	XMFLOAT4X4 BillboardAnchorTransform = MathHelps::Identity;
};

struct MainSceneBindingState
{
	ID3D12DescriptorHeap* const* DescriptorHeaps = nullptr;
	UINT DescriptorHeapCount = 0;
	D3D12_GPU_VIRTUAL_ADDRESS PassCBAddress = 0;
	D3D12_GPU_VIRTUAL_ADDRESS LightCBAddress = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE AmbientOcclusionDescriptor = {};
	D3D12_GPU_DESCRIPTOR_HANDLE DirectionalShadowMaskDescriptor = {};
	D3D12_GPU_DESCRIPTOR_HANDLE ReflectionDescriptor = {};
};
using MainScenePassContext = MainSceneBindingState;

struct DefaultDescriptorCatalog
{
	UINT SkyTexHeapIndex = 0;
	CD3DX12_GPU_DESCRIPTOR_HANDLE SkyTexDescriptor = {};
	CD3DX12_GPU_DESCRIPTOR_HANDLE OtherTexDescriptor = {};
	CD3DX12_GPU_DESCRIPTOR_HANDLE EnvironmentIblDescriptorTable = {};
	CD3DX12_GPU_DESCRIPTOR_HANDLE RenderToTextureFallbackDescriptor = {};
};

struct GizmoGpuVertex
{
	XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
	float HandleId = 0.0f;
};

struct SkeletonOverlayGpuVertex
{
	XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

static const std::wstring TranslateGizmoGeometryName = L"__EditorTranslateGizmoGeo";
static const std::wstring RotateGizmoGeometryName = L"__EditorRotateGizmoGeo";
static const std::wstring ScaleGizmoGeometryName = L"__EditorScaleGizmoGeo";
static constexpr UINT MaxSkinBonesPerDraw = 256u;
static constexpr UINT MaxSkeletonOverlayVertices = 8192u;
static constexpr UINT MaxSkeletonOverlayIndices = 16384u;
static constexpr UINT MaxRenderToTextureCount = 4u;
static constexpr UINT DefaultRenderToTextureWidth = 1024u;
static constexpr UINT DefaultRenderToTextureHeight = 1024u;
// 普通透视投影无法跨过 180 度；RTT 镜面动态扩展视场时只允许接近它。
static constexpr float MaxRenderToTextureReflectionFovY = 3.0f;

struct SkinningConstants
{
	DirectX::XMFLOAT4X4 BoneMatrices[MaxSkinBonesPerDraw];
};

struct SkinnedDeformCacheEntry
{
	std::wstring RenderItemName;
	std::wstring SkinnedMeshAssetPath;
	SceneEntityBase* SourceEntity = nullptr;
	SceneEntityBase* RuntimeOwnerEntity = nullptr;
	ImportedSkinnedMeshData SourceMeshData;
	std::uint64_t LastPaletteRevision = 0;
	UINT SkinningCBIndex = UINT(-1);
	bool AssetLoaded = false;
	bool GpuResourcesReady = false;
	UINT VertexCount = 0;
	UINT VertexBufferByteSize = 0;
	ComPtr<ID3D12Resource> SourceVertexBuffer = nullptr;
	std::array<ComPtr<ID3D12Resource>, 3> DeformedVertexBuffers = {};
	std::array<D3D12_VERTEX_BUFFER_VIEW, 3> DeformedVertexBufferViews = {};
	std::array<D3D12_RESOURCE_STATES, 3> DeformedVertexBufferStates =
	{
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COMMON
	};
};

struct D3DWindowAOConfig
{
	bool Enabled = true;
	float Strength = 0.32f;
	float Radius = 0.05f;
	float FadeStart = 0.2f;
	float FadeEnd = 2.0f;
	float SurfaceEpsilon = 0.02f;
	float BlurSigma = 2.5f;
};

struct D3DWindowShadowMaskConfig
{
	bool Enabled = true;
	// 当前 CSM 调试阶段先禁止主 PBR 使用屏幕空间方向光阴影遮罩。
	// 遮罩 pass 依赖屏幕深度重建和跨级联模糊，容易把深度/边界问题误判为 shadow map 投影问题。
	bool UseInMainPbr = false;
	// x: enable, y/z/w: cascade 0/1/2 blur radius in screen pixels.
	// Keep cascade 0 sharp; blur coarser cascades to hide low-resolution shadow blotches.
	float Cascade0BlurRadius = 0.0f;
	float Cascade1BlurRadius = 2.0f;
	float Cascade2BlurRadius = 3.0f;
};

enum class ShadowUpdateRequestType : UINT
{
	None = 0,
	// 重绘静态 + 动态投射体,并把静态结果回写到缓存层。
	FullUpdate,
	// 从静态缓存恢复 working slice,仅补动态投射体。
	DynamicOnlyUpdate
};

struct ShadowUpdateRequest
{
	ShadowUpdateRequestType Type = ShadowUpdateRequestType::None;
	ShadowPoolPlanner::PoolKind Pool = ShadowPoolPlanner::PoolKind::None;
	int BaseShadowMapIndex = -1;
	UINT SlotCount = 0;
};

struct ShadowLightRuntimeState
{
	std::uint64_t ShadowParameterHash = 0;
	std::uint64_t StaticCasterHash = 0;
	bool HasShadowParameterHash = false;
	bool HasStaticCasterHash = false;
	int BaseShadowMapIndex = -1;
	UINT SlotCount = 0;
	ShadowPoolPlanner::PoolKind Pool = ShadowPoolPlanner::PoolKind::None;
};

struct ShadowCache2DResource
{
	UINT Width = 0;
	UINT Height = 0;
	ComPtr<ID3D12Resource> Resource = nullptr;
	// 当前静态缓存资源状态。缓存层只参与 copy,不直接给 shader 采样。
	D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_GENERIC_READ;
};

struct ShadowCacheCubeResource
{
	UINT FaceSize = 0;
	ComPtr<ID3D12Resource> Resource = nullptr;
	// 当前静态缓存资源状态。缓存层只参与 copy,不直接给 shader 采样。
	D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_GENERIC_READ;
};

#include "UploadBuffer.h"

// =========================
// 帧资源
// =========================
// 工作线程数量
static const UINT NumContexts = 4;

// 每帧命令录制与常量缓冲资源。
struct FrameResource
{
public:
	FrameResource();
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	// 为一帧分配常量缓冲和命令录制资源。
	void Create(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);

	// 每帧独立常量缓冲,避免 CPU/GPU 并发写冲突。
	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
	std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
	std::unique_ptr<UploadBuffer<SkinningConstants>> SkinningCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> VolumetricLightObjectCB = nullptr;
	std::unique_ptr<UploadBuffer<LightConstants>> LightCB = nullptr;
	std::unique_ptr<UploadBuffer<AOConstants>> AOCB = nullptr;
	std::unique_ptr<UploadBuffer<PostProcessConstants>> PostProcessCB = nullptr;

	ComPtr<ID3D12Resource> mCopyTexture = nullptr;
	ComPtr<ID3D12Resource> mPostProcessSceneColor = nullptr;
	ComPtr<ID3D12Resource> mColorAdjustSceneColor = nullptr;
	ComPtr<ID3D12Resource> mInteractionOutlineMask = nullptr;
	ComPtr<ID3D12Resource> mTransparentOitAccum = nullptr;
	ComPtr<ID3D12Resource> mTransparentOitReveal = nullptr;

	ComPtr<ID3D12CommandAllocator> BeginCommandAllocator = nullptr;
	ComPtr<ID3D12CommandAllocator> MidCommandAllocator = nullptr;
	ComPtr<ID3D12CommandAllocator> AoCommandAllocator = nullptr;
	ComPtr<ID3D12CommandAllocator> EndCommandAllocator = nullptr;
	ComPtr<ID3D12GraphicsCommandList> BeginCommandList = nullptr;
	ComPtr<ID3D12GraphicsCommandList> MidCommandList = nullptr;
	ComPtr<ID3D12GraphicsCommandList> AoCommandList = nullptr;
	ComPtr<ID3D12GraphicsCommandList> EndCommandList = nullptr;

	ComPtr<ID3D12CommandAllocator> threadCommandAllocators[NumContexts] = { nullptr };
	ComPtr<ID3D12GraphicsCommandList> threadCommandLists[NumContexts] = { nullptr };
	ComPtr<ID3D12CommandAllocator> translucentThreadCommandAllocators[NumContexts] = { nullptr };
	ComPtr<ID3D12GraphicsCommandList> translucentThreadCommandLists[NumContexts] = { nullptr };
	ComPtr<ID3D12CommandAllocator> transparentThreadCommandAllocators[NumContexts] = { nullptr };
	ComPtr<ID3D12GraphicsCommandList> transparentThreadCommandLists[NumContexts] = { nullptr };
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
struct CameraRenderRequest;
struct Transform;
struct WMaterialFileData;

// 刷权重可视化几何体重建所需的数据(与 WModel 格式解耦)
struct BrushVizMeshSlice
{
	const void* SourcePtr = nullptr;
	std::wstring Id;
	const Vertex* Vertices = nullptr;
	const std::uint32_t* Indices = nullptr;
	const Witchcraft::Animation::VertexBoneInfluence4* Skinning = nullptr;
	UINT VertexCount = 0;
	UINT IndexCount = 0;
};

struct BrushVizNode
{
	Transform LocalTransform = {};
	std::wstring MeshRef;
	std::vector<BrushVizNode> Children;
};

struct DeferredGeometryReleaseEntry
{
	MeshGeometry Geometry;
	UINT64 SafeFence = 0;
};

class D3DWindow
{
	friend struct D3DWindowGeometryProvider;
public:
	// =========================
	// 生命周期 / 初始化
	// =========================
	D3DWindow();
	~D3DWindow();

	// 初始化 D3D12 窗口、交换链、资源和默认场景数据。
	bool Create(HWND hWnd, Timer* timer, Editor* editor = nullptr);
	void CreateCommandQueueAndSwapChain();
	void CreateDevice();
	void CreateDescriptorHeaps();

	// 窗口尺寸或全屏状态变化时重建渲染目标。
	void OnResize(bool Fullscreen = false);

	bool SerEditorDrawd();

	// 创建根签名
	void CreateRootSignature();

	// 创建着色器与 PSO
	void CreatePipesAndShaders();

	void CreateSRVDescriptorHeap();

	// =========================
	// 资源构建 / 场景注册
	// =========================
	// 读取默认贴图
	void LoadTextures();
	void AddShapeGeometry();
	void AddShapeGeometry(MeshGeometry* geo);
	void AddTransformGizmoGeometry();
	void AddBillboardGeometry();
	void RemoveShapeGeometry(std::wstring name);
	bool HasShapeGeometry(const std::wstring& name) const;
	bool SetGeometryVertexColor(const std::wstring& name, const DirectX::XMFLOAT4& color);
	void BuildMaterials();
	void BuildLight();
	void ClearLights();
	void SetAmbientColor(const DirectX::XMFLOAT4& ambientColor);
	void AddLight(Light* light);
	void ResetSceneRuntimeRenderState();
	void RebuildRenderItemsFromEntities(WitchcraECS* ecs);
	void AddRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs);
	void RemoveRenderItemsFromEntity(SceneEntityBase* entity);
	void RemoveRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs);
	void UpdateRenderItemsTransformFromEntity(SceneEntityBase* entity, WitchcraECS* ecs);
	bool PrepareRenderItemEntityOperation(WitchcraECS* ecs, bool syncTransforms);
	void EraseRenderItemFromAllLayers(const std::wstring& renderItemName);
	void RefreshRenderItemCachesAfterStructuralChange(bool reindexObjectCBIndices);
	void CollectLayerRenderItemsForThreadBatch(
		const std::map<std::wstring, RenderItem*>& sourceLayer,
		std::vector<std::pair<std::wstring, RenderItem*>>& outItems) const;
	// 将不透明渲染项重新均分到工作线程批次中。
	void RebuildOpaqueThreadBatches();
	// 创建/扩容帧资源
	void CreateFrameResources();
	void AddRenderItem(std::wstring renderItemName, ObjectCollection* objectCollection, const std::wstring& geometryName,
		UINT renderLayerIndex, const DirectX::XMFLOAT4X4* worldTransform,
		const DirectX::XMFLOAT4X4* texTransform, const std::wstring* materialName,
		const DirectX::BoundingBox* localBounds = nullptr,
		SceneEntityType sceneType = SceneEntityType::StaticScenery,
		bool rebuildOpaqueBatches = true);
	void RemoveRenderItem(std::wstring renderItemName, UINT renderLayerIndex);

	std::wstring GetMaterialName(std::wstring renderItemName);
	void SetMaterial(std::wstring renderItemName, std::wstring materialName);
	bool SetMaterialDiffuseRenderToTexture(const std::wstring& materialName, std::uint32_t renderToTextureId);
	bool SetMaterialReflection(
		const std::wstring& materialName,
		bool enableReflection,
		MaterialReflectionSource source,
		std::uint32_t renderToTextureId);
	void SetEntityReflectionReceiverRenderToTexture(SceneEntityBase* entity, std::uint32_t renderToTextureId);
	// 根据材质属性自动推导渲染层(天空/debug 层会保持不变)。
	UINT ResolveRenderLayerIndexByMaterial(UINT currentRenderLayerIndex, const std::wstring& materialName) const;
	// 将已有渲染项切换到指定渲染层(仅移动层索引,不重建对象数据)。
	void MoveRenderItemToLayer(const std::wstring& renderItemName, UINT targetRenderLayerIndex);
	// 为导入的模型创建一份材质,并自动准备所需纹理。
	std::wstring CreateMaterialFromImport(const std::wstring& name, const ImportedMaterialInfo& materialInfo);
	// 创建仅颜色参数的运行时材质(不绑定任何贴图、不读取材质文件)。
	std::wstring CreateColorMaterial(const std::wstring& name, const DirectX::XMFLOAT4& diffuseColor, float roughness, float metallic, float opacity);
	// 从 .wmat 文件读取材质并按需创建运行时材质。
	std::wstring GetOrCreateMaterialFromWMaterialFile(const std::filesystem::path& materialFilePath);
	std::wstring GetMaterialFilePathByMaterialName(const std::wstring& MaterialName) const;
	std::wstring GetSkyTexturePathByMaterialName(const std::wstring& MaterialName) const;
	std::wstring GetOrCreateSkyMaterial(const std::wstring& skyTexturePath);
	Material* GetMaterialByMaterialName(const std::wstring& MaterialName);
	const Material* GetMaterialByMaterialName(const std::wstring& MaterialName) const;
	bool ApplyMaterialPbrTexturesFromWMaterialData(
		const std::wstring& MaterialName,
		const std::filesystem::path& materialFilePath,
		const WMaterialFileData& materialData);
	void NotifyMaterialChanged(const std::wstring& MaterialName);

	std::vector<std::wstring> GetMaterialNameList();

	// =========================
	// D3D 对象访问
	// =========================
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

	struct WindowInfo
	{
		HWND m_hWnd = nullptr;

		BOOL fullscreenState = false;
		UINT Width = 0;
		UINT Height = 0;
	};
	const WindowInfo GetWindowInfo() { return WinInfo; }

	// =========================
	// 帧更新 / 渲染主循环
	// =========================
	// 创建后台工作线程,用于分阶段录制命令列表。
	void BeginWorkerThreads();

	void UpdateCamera();
	// 更新对象CB
	void UpdateObjectCBs();
	void UpdateSkinningCBs();
	void UpdateSkinnedDeformationCaches();
	void DispatchSkinnedDeformationPass(ID3D12GraphicsCommandList* cmdList);
	void FreshenObjectCBs();
	void FreshenObjectCBs(const std::wstring& renderItemName);
	void UpdateBillboardRenderItemsForCamera();
	// 更新材质缓冲区
	void UpdateMaterialCBs();
	void FreshenMaterialCBs();
	// 更新光照
	void UpdateLightCBs();
	void FreshenLightCBs();
	std::uint64_t BuildStaticShadowCasterHashForLight(const Light& light) const;
	// 更新主要通道CB
	void UpdateMainPassCBs();
	void UpdateShadowTransform();
	void UpdateShadowPassCBs();
	void UpdateAOCB();
	void UpdatePostProcessCBs();
	void UpdateFrameDescriptors();
	void UpdateFrameStateForRender();
	void SyncRenderToTextureTargetsFromCameraRequests();
	UINT ResolveDefaultSkyTextureHeapIndex();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrvHandle(UINT heapIndex) const;
	DefaultDescriptorCatalog BuildDefaultDescriptorCatalog();
	MainScenePassContext BuildMainScenePassContext(
		ID3D12DescriptorHeap* const* descriptorHeaps = nullptr,
		UINT descriptorHeapCount = 0);
	void BuildBrdfLutTexture(ID3D12GraphicsCommandList* cmdList);
	void BindSceneSrvDescriptorTables(
		ID3D12GraphicsCommandList* cmdList,
		const MainScenePassContext& bindingState);
	void BindMainScenePassCommonState(
		ID3D12GraphicsCommandList* cmdList,
		const MainScenePassContext& bindingState,
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);
	bool TryResolveRenderToTextureMirrorPlane(
		std::uint32_t renderToTextureId,
		DirectX::XMVECTOR* outPoint,
		DirectX::XMVECTOR* outNormal,
		DirectX::XMVECTOR* outTangentUp) const;
	CameraRenderRequest BuildRenderToTextureMirrorCameraRequest(
		const CameraRenderRequest& request,
		const RenderToTexture& renderToTexture) const;
	DirectX::XMMATRIX BuildRenderToTextureProjectionMatrix(
		const CameraRenderRequest& request,
		const RenderToTexture& renderToTexture) const;
	DirectX::XMMATRIX BuildRenderToTextureReflectionViewProjTexMatrix(
		const CameraRenderRequest& request,
		const RenderToTexture& renderToTexture) const;
	PassConstants BuildRenderToTexturePassConstants(
		const CameraRenderRequest& request,
		const RenderToTexture& renderToTexture) const;
	void UpdateDebugText();
	void CollectRenderFrameItemSnapshots();
	void ResolveRenderFramePlanFlags();
	void BuildRenderFramePlan();
	void Update();
	void RenderB();
	void RecordRenderTailPasses(
		ID3D12GraphicsCommandList* endCommandList,
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
		ID3D12DescriptorHeap* const* srvDescriptorHeaps,
		UINT srvHeapCount,
		bool useRecordedFramePlan,
		bool hasOpaqueRenderItems,
		bool hasTransparentRenderItems,
		bool hasAoRenderItems,
		bool hasShadowCasterRenderItems,
		bool hasOitResources,
		bool enableVolumetricLightPass,
		bool allowNoSkyPostProcessTail,
		bool allowNoSkyMainGeometrySubmission,
		bool useNoSkyMinimalUiTail,
		bool renderFPS,
		bool renderEditor);
	void RenderE();

	void DestroyRender();

	// 关闭主命令列表、提交到队列并等待 GPU 完成。
	void CloseCommandListAndSynchronize();

	// 刷新命令队列(等待上一帧)
	void FlushCommandQueue();

	// 重置命令列表
	void ResetCommandList();
	// 关闭命令列表
	void CloseCommandList();

	bool IsCommandListClosed();

	// =========================
	// 运行时参数
	// =========================
	HWND GetHwnd();

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv()const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv()const;

	UINT GetRtvDescriptorSize();
	UINT GetDsvDescriptorSize();
	UINT GetCbvSrvUavDescriptorSize();
	DXGI_FORMAT GetIndexBufferFormat() const;
	AggregateGraphicObj* GetAggregateGraphicObj(const std::wstring& geometryName);
	RenderToTexture* EnsureRenderToTexture(const RenderToTextureDesc& desc);
	RenderToTexture* FindRenderToTexture(std::uint32_t id);
	const RenderToTexture* FindRenderToTexture(std::uint32_t id) const;

	RenderItem* GetRenderItem(const std::wstring& name);
	void RebindRenderItemGeometry(const std::wstring& renderItemName, ObjectCollection* objectCollection, const std::wstring& geometryName);

	void SetFPSRender(bool enable);
	bool IsFPSRender() const;
	float GetShadowOpacity() const;
	void SetShadowOpacity(float opacity);
	float GetShadowSoftness() const;
	void SetShadowSoftness(float softness);
	bool IsAOEnabled() const;
	void SetAOEnabled(bool enable);
	float GetAOStrength() const;
	void SetAOStrength(float strength);
	float GetAORadius() const;
	void SetAORadius(float radius);
	float GetAOFadeStart() const;
	void SetAOFadeStart(float fadeStart);
	float GetAOFadeEnd() const;
	void SetAOFadeEnd(float fadeEnd);
	float GetAOSurfaceEpsilon() const;
	void SetAOSurfaceEpsilon(float epsilon);
	float GetAOBlurSigma() const;
	void SetAOBlurSigma(float sigma);
	bool IsFXAAEnabled() const;
	void SetFXAAEnabled(bool enable);
	float GetFXAAContrastThreshold() const;
	void SetFXAAContrastThreshold(float threshold);
	float GetFXAARelativeThreshold() const;
	void SetFXAARelativeThreshold(float threshold);
	float GetFXAASpanMax() const;
	void SetFXAASpanMax(float spanMax);
	DirectX::XMFLOAT3 GetColorAdjustWhiteBalance() const;
	void SetColorAdjustWhiteBalance(const DirectX::XMFLOAT3& whiteBalance);
	float GetColorAdjustContrast() const;
	void SetColorAdjustContrast(float contrast);
	float GetColorAdjustSaturation() const;
	void SetColorAdjustSaturation(float saturation);
	float GetEnvironmentDiffuseIntensity() const;
	void SetEnvironmentDiffuseIntensity(float intensity);
	float GetEnvironmentSpecularIntensity() const;
	void SetEnvironmentSpecularIntensity(float intensity);
	bool IsEnvironmentBrdfLutEnabled() const;
	void SetEnvironmentBrdfLutEnabled(bool enable);
	void SetGizmoRenderData(const GizmoRenderData& renderData);
	void ClearGizmoRenderData();
	void SetSkeletonOverlayRenderData(const SkeletonOverlayRenderData& renderData);
	void ClearSkeletonOverlayRenderData();
	void InvalidateSkinnedDeformCache(SceneEntityBase* entity);
	SkinWeightVizPass& GetSkinWeightVizPass() { return mSkinWeightVizPass; }
	void SetSkinWeightVisualizationEnabled(bool enable);
	bool IsSkinWeightVisualizationEnabled() const;
	void SetSkinWeightVisualizationTarget(SceneEntityBase* entity);
	SceneEntityBase* GetSkinWeightVisualizationTarget() const;
	void BuildBrushWeightVisualization();
	void RebuildBrushWeightVisualizationGeometry(const std::vector<BrushVizMeshSlice>& meshes, const BrushVizNode& rootNode);
	const std::unordered_map<const void*, UINT>& GetBrushWeightVizMeshOffsets() const;
	void IncrementalUpdateBrushWeightVB(const std::vector<std::tuple<const void*, UINT, Witchcraft::Animation::VertexBoneInfluence4>>& changes);
	const GizmoPickMeshData& GetTransformGizmoPickMesh(GizmoMode mode) const;

	// =========================
	// 相机接口
	// =========================

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
	// 切换视锥剔除参考视角锁定:锁定后不再跟随当前相机移动。
	void ToggleFrustumCullingReferenceLock();
	bool IsFrustumCullingReferenceLocked() const;

	void RotateCamera(float DeltaTime, DirectX::XMFLOAT2 angle);
	void MoveCamera(float DeltaTime, DirectX::XMFLOAT3 distance);

	// 获取静态采样
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 8> GetStaticSamplers();

private:
	static D3DWindow* s_app;
	static HANDLE CreateAutoResetEventHandle();
	static HANDLE CreateManualResetEventHandle();
	static bool AreMatrixElementsNearlyEqual(const DirectX::XMFLOAT4X4& lhs, const DirectX::XMFLOAT4X4& rhs, float epsilon = 1e-4f);
	static bool CompareShadowCasterEntryByName(const std::pair<std::wstring, const RenderItem*>& lhs, const std::pair<std::wstring, const RenderItem*>& rhs);
	static bool StaticShadowCacheResourcesMatchLayout(
		const std::vector<ShadowCache2DResource>& staticShadowCache2DResources,
		const std::vector<UINT>& texture2DSizes,
		const std::vector<ShadowCacheCubeResource>& staticPointLightShadowCubeResources,
		const std::vector<UINT>& pointLightCubeSizes);
	static bool CompareRenderItemPairByName(const std::pair<std::wstring, RenderItem*>& lhs, const std::pair<std::wstring, RenderItem*>& rhs);
	static void SplitRenderItemsToThreadBatches(
		std::vector<std::pair<std::wstring, RenderItem*>>& items,
		std::vector<RenderItem*> (&targetBatches)[NumContexts]);
	static void BuildCameraFrustumCornersWorldSpace(const DirectX::XMMATRIX& invViewProj, DirectX::XMFLOAT3 outCorners[8]);
	static bool ResolvePointLightCubeIndex(
		UINT firstPointCubeSlot,
		UINT shadowSlotIndex,
		UINT cubeCount,
		UINT* outCubeIndex);
	ID3D12PipelineState* ResolvePipelineStateForRenderItem(
		const RenderItem* renderItem,
		ID3D12PipelineState* defaultPipelineState,
		UINT pipelineNumber) const;
	static DirectX::BoundingBox BuildRelaxedFrustumCullingBounds(const DirectX::BoundingBox& worldBounds);
	static bool DoesCullingBoundsIntersectFrustum(
		const DirectX::BoundingBox& worldBounds,
		const DirectX::BoundingFrustum& worldFrustum);
	bool TryBuildEntityWorldCullingBounds(
		SceneEntityBase* entity,
		WitchcraECS* ecs,
		DirectX::BoundingBox* outWorldBounds) const;
	bool IsEntitySubtreeVisibleInFrustum(
		SceneEntityBase* entity,
		WitchcraECS* ecs,
		const DirectX::BoundingFrustum& worldFrustum,
		std::unordered_set<SceneEntityBase*>& visitedEntities,
		bool* outAnyCullingBounds) const;
	bool ShouldCullRenderItemByMainCameraFrustum(
		const RenderItem& renderItem,
		const DirectX::BoundingFrustum& worldFrustum) const;
	void AppendRenderItemsFromEntityRecursive(SceneEntityBase* entity, WitchcraECS* ecs);
	void RemoveRenderItemsFromEntityRecursive(SceneEntityBase* entity, WitchcraECS* ecs);
	void TraverseEntityHierarchy(
		SceneEntityBase* entity,
		WitchcraECS* ecs,
		const std::function<void(SceneEntityBase*)>& visitor);
	void ReindexRenderItemObjectCBIndices();
	// 按 PSO 绘制一组 RenderItem。
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems, ComPtr<ID3D12PipelineState> pipelineState, UINT pipelineNumber, UINT passCBIndex = 0);
	void UpdateRenderItemsTransformFromEntityRecursive(SceneEntityBase* entity, WitchcraECS* ecs);
	static bool BuildEntityRenderTransforms(SceneEntityBase* entity, WitchcraECS* ecs, DirectX::XMFLOAT4X4* outWorldTransform, DirectX::XMFLOAT4X4* outTexTransform);
	static void BuildStandardEntityRenderTransforms(SceneEntityBase* entity, WitchcraECS* ecs, DirectX::XMFLOAT4X4* outWorldTransform, DirectX::XMFLOAT4X4* outTexTransform);
	// 构建告示牌的全局变换
	static DirectX::XMFLOAT4X4 BuildBillboardWorldTransform(
		const DirectX::XMFLOAT4X4& anchorTransform,
		const BillboardData& billboard,
		const DirectX::XMFLOAT3& cameraPosition,
		const DirectX::XMMATRIX& viewMatrix,
		const DirectX::XMMATRIX& projMatrix,
		float renderTargetHeight);
	static std::vector<RenderItem*> CollectRenderItems(const std::map<std::wstring, RenderItem*>& renderItemMap);
	std::vector<RenderItem*> CollectSelectedRenderItems();
	void CreateImportedMaterialTextureSlot(std::vector<Texture>& textureGroup, UINT slotIndex, const ImportedTextureSource& source, Texture* fallback, const std::wstring& uniqueMaterialName, const std::wstring& slotName, ResourceUploadBatch& resourceUpload);
	void CreateMaterialTextureSlot(
		std::vector<Texture>& textureGroup,
		UINT slotIndex,
		const ImportedTextureSource& source,
		Texture* fallbackTexture,
		const std::wstring& MaterialName,
		const wchar_t* slotName,
		ResourceUploadBatch& resourceUpload,
		std::vector<Texture>* existingTextureGroup);
	void DrawOutlinePassItems(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12PipelineState* pipelineState,
		UINT pipelineTag,
		const std::vector<RenderItem*>& selectedOutlineItems);
	void DrawSkeletonOverlayPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);
	void DrawGizmoPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);
	void DrawSkinWeightVisualizationPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);
	static D3D12_GPU_VIRTUAL_ADDRESS PrepareVolumetricLightDrawObjectCB(
		UploadBuffer<ObjectConstants>* volumetricObjectCB,
		D3D12_GPU_VIRTUAL_ADDRESS objectCBBaseAddress,
		UINT objectCBByteSize,
		UINT drawIndex,
		UINT lightIndex);
	// 根据 pass 选择对应线程使用的命令分配器和命令列表。
	ID3D12CommandAllocator* GetWorkerCommandAllocator(UINT workerPhaseIndex, int threadIndex);
	ID3D12GraphicsCommandList* GetWorkerCommandList(UINT workerPhaseIndex, int threadIndex);
	void EnsureShadowMapResources(UINT requiredShadowMapCount);
	// 构建后处理场景颜色描述符
	void BuildPostProcessSceneColorDescriptors();
	// 构建环境 IBL 资源描述符表(diffuse irradiance / specular prefilter / BRDF LUT)。
	void BuildEnvironmentLightingDescriptors();
	// 构建色彩调整中间颜色描述符
	void BuildColorAdjustSceneColorDescriptors();
	// 构建交互描边遮罩描述符
	void BuildInteractionOutlineMaskDescriptors();
	// 构建共享场景输入描述符(每帧 2 个 SRV: normal + scene depth)。
	void BuildSharedSceneInputDescriptors();
	void BuildDirectionalShadowMaskDescriptors();
	// 构建透明 OIT SRV 描述符
	void BuildTransparentOitDescriptors();
	// 通知工作线程开始录制指定工作阶段,并等待全部完成。
	void BeginWorkerPass(UINT workerPhaseIndex);
	void WaitForWorkerPass();
	// 一次完整的"工作阶段"提交:唤醒录制 -> 等待完成 -> 收集并提交该阶段命令列表。
	void ExecuteWorkerPassAndSubmit(UINT workerPhaseIndex);
	UINT ResolveWorkerPhaseIndexFromWaitResult(DWORD waitResult) const;
	// 绑定工作场景通用状态
	void BindWorkerScenePassCommonState(
		ID3D12GraphicsCommandList* workerCommandList,
		ID3D12DescriptorHeap* const* srvDescriptorHeaps,
		UINT srvHeapCount);
	void RecordWorkerShadowPass(
		int threadIndex,
		ID3D12GraphicsCommandList* workerCommandList,
		const std::vector<RenderItem*>& opaqueRenderBatch,
		const std::vector<RenderItem*>& transparentRenderBatch,
		const std::vector<RenderItem*>& staticShadowCasterRenderItems,
		const std::vector<RenderItem*>& dynamicShadowCasterRenderItems,
		ID3D12DescriptorHeap* const* srvDescriptorHeaps,
		UINT srvHeapCount);
	void RecordWorkerShadow2DPassEntries(
		int threadIndex,
		ID3D12GraphicsCommandList* workerCommandList,
		const std::vector<RenderItem*>& staticShadowItems,
		const std::vector<RenderItem*>& dynamicShadowItems,
		const std::vector<ShadowRenderEntry>& shadowEntries,
		const ShadowMapPass::ShadowMapLayout& shadowLayout,
		int directionalLightType);
	void RecordWorkerPointLightShadowCubePassEntries(
		int threadIndex,
		ID3D12GraphicsCommandList* workerCommandList,
		const std::vector<RenderItem*>& staticShadowItems,
		const std::vector<RenderItem*>& dynamicShadowItems,
		const std::vector<ShadowRenderEntry>& shadowEntries,
		const ShadowMapPass::ShadowMapLayout& shadowLayout);
	void RecordWorkerNormalPass(
		int threadIndex,
		ID3D12GraphicsCommandList* workerCommandList,
		const std::vector<RenderItem*>& opaqueRenderBatch,
		const std::vector<RenderItem*>& transparentRenderBatch,
		ID3D12DescriptorHeap* const* srvDescriptorHeaps,
		UINT srvHeapCount);
	void RecordWorkerOpaquePass(
		ID3D12GraphicsCommandList* workerCommandList,
		const std::vector<RenderItem*>& opaqueRenderBatch,
		const CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
		const CD3DX12_CPU_DESCRIPTOR_HANDLE& dsvHandle,
		ID3D12DescriptorHeap* const* srvDescriptorHeaps,
		UINT srvHeapCount);
	void RecordWorkerTranslucentPass(
		ID3D12GraphicsCommandList* workerCommandList,
		const std::vector<RenderItem*>& transparentRenderBatch,
		const CD3DX12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
		const CD3DX12_CPU_DESCRIPTOR_HANDLE& dsvHandle,
		ID3D12DescriptorHeap* const* srvDescriptorHeaps,
		UINT srvHeapCount);
	void RecordWorkerTransparentPass(
		ID3D12GraphicsCommandList* workerCommandList,
		const std::vector<RenderItem*>& transparentRenderBatch,
		const CD3DX12_CPU_DESCRIPTOR_HANDLE& dsvHandle,
		ID3D12DescriptorHeap* const* srvDescriptorHeaps,
		UINT srvHeapCount);
	// 从共享 SRV 堆中申请连续描述符槽位;失败时返回 false 且不修改索引。
	bool TryReserveSrvDescriptorSlots(UINT count, const wchar_t* context, UINT* outBaseIndex = nullptr);
	// 计算延迟释放资源可安全回收的 fence 值(保守留足多帧缓冲)。
	UINT64 ComputeDeferredReleaseFence() const;
	// 仅回收已被 GPU 完成的延迟释放资源。
	void DrainDeferredReleasesByCompletedFence();
	bool EnsureSkinnedDeformCacheEntry(RenderItem& renderItem, const std::wstring& renderItemName);
	bool TryBuildRenderToTextureDescriptorHandles(
		UINT slotIndex,
		CD3DX12_CPU_DESCRIPTOR_HANDLE* outCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE* outGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE* outCpuRtv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE* outCpuDsv) const;
	UINT GetRenderToTexturePassCBIndex(UINT slotIndex) const;
	bool TryGetRenderToTextureSlotIndex(std::uint32_t id, UINT* outSlotIndex) const;
	void RenderCameraRequestsToRenderTextures(ID3D12GraphicsCommandList* cmdList);
	void RenderOpaqueItemsToRenderTexture(
		ID3D12GraphicsCommandList* cmdList,
		const CameraRenderRequest& request,
		RenderToTexture* renderToTexture,
		UINT passCBIndex);
	static std::filesystem::path ResolveProjectAssetPath(const std::wstring& assetPath);

	// =========================
	// 平台与设备状态
	// =========================
	WindowInfo WinInfo;

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
	D3D12_RESOURCE_STATES DepthStencilBufferState = D3D12_RESOURCE_STATE_COMMON;
	ComPtr<ID3D12Device> d3dDevice = nullptr;
	ComPtr<ID3D12CommandQueue> CommandQueue = nullptr;
	ComPtr<ID3D12RootSignature> RootSignature = nullptr;
	SkinningComputePass mSkinningComputePass;
	RenderToTextureManager mRenderToTextureManager;
	std::unordered_map<std::uint32_t, UINT> mRenderToTextureSlotById;
	std::unordered_map<std::uint32_t, DirectX::XMFLOAT4X4> mRenderToTextureViewProjTexById;
	std::uint32_t mActiveRenderToTextureTargetId = 0;
	bool mRenderingRenderToTexturePass = false;
	std::unordered_map<SceneEntityBase*, SkinnedDeformCacheEntry> mSkinnedDeformCache;
	ComPtr<ID3D12PipelineState> PipelineState[管道计数];
	ComPtr<ID3D12PipelineState> DirectionalCascadeShadowPipelineState[4];
	ShadowFrameBuilder::StableDirectionalCascadeStates mStableDirectionalCascadeStates;
	GizmoPass mGizmoPass;
	ComPtr<ID3D12PipelineState> SkinnedOpaquePipelineState = nullptr;
	ComPtr<ID3D12PipelineState> SkinnedTransparentPipelineState = nullptr;
	ComPtr<ID3D12PipelineState> SkinnedTransparentNearOpaquePipelineState = nullptr;
	ComPtr<ID3D12PipelineState> SkinnedShadowPipelineState = nullptr;
	ComPtr<ID3D12PipelineState> SkinnedDirectionalCascadeShadowPipelineState[4];
	ComPtr<ID3D12PipelineState> SkinnedNormalPipelineState = nullptr;
	SkinWeightVizPass mSkinWeightVizPass;
	ComPtr<ID3D12PipelineState> debugPipelineState;
	ComPtr<ID3DBlob> vertexShader[着色器计数];
	ComPtr<ID3DBlob> pixelShader[着色器计数];
	bool CommandListClose = false;

	std::vector<D3D12_INPUT_ELEMENT_DESC> InputElementDescs;
	std::vector<D3D12_INPUT_ELEMENT_DESC> SkinnedInputElementDescs;

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
	UINT SrvDescriptorHeapCapacity = 0;
	UINT RenderToTextureSrvStartIndex = UINT(-1);
	UINT RenderToTextureRtvStartIndex = UINT(-1);
	UINT RenderToTextureDsvStartIndex = UINT(-1);
	bool RenderToTextureDescriptorsReserved = false;
	ComPtr<ID3D12Resource> mBrdfLutTexture = nullptr;
	ComPtr<ID3D12Resource> mBrdfLutUploadBuffer = nullptr;
	std::array<D3D12_RESOURCE_STATES, SwapChainBufferCount> CopyTextureStates =
	{
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COMMON
	};

	// =========================
	// 引擎资源注册表
	// =========================
	std::unordered_map<std::wstring, AggregateGraphicObj> AggrObject;
	UINT ObjCBCount = 0;

	// 几何、纹理组、材质注册表。
	std::unordered_map<std::wstring, MeshGeometry> Geometries;
	// 延迟释放的几何资源:先从可见注册表移除,等 GPU 完成后再真正释放。
	std::vector<DeferredGeometryReleaseEntry> DeferredReleaseGeometries;
	struct DeferredPipelineReleaseEntry
	{
		ComPtr<ID3D12PipelineState> State = nullptr;
		UINT64 SafeFence = 0;
	};
	// 延迟释放的 PSO:运行时重建管线后,旧 PSO 需等 GPU 完成后再释放。
	std::vector<DeferredPipelineReleaseEntry> DeferredReleasePipelineStates;
	std::unordered_map<std::wstring, std::vector<Texture>> TextureGroups;
	std::unordered_map<std::wstring, Material> Materials;
	std::unordered_map<std::wstring, std::wstring> MaterialByFilePath;
	std::unordered_map<std::wstring, std::wstring> SkyMaterialByTexturePath;

	DirectX::XMFLOAT4 AmbientColor;

	std::unordered_map<std::wstring, Light> Lights;
	std::vector<Light> LightsCache;
	std::vector<std::wstring> LightsCacheNames;
	std::vector<int> LightShadowMapIndices;
	ShadowPoolPlanner::ShadowPoolPlan CurrentShadowPoolPlan;
	std::vector<ShadowUpdateRequest> CurrentShadowUpdateRequests;
	std::unordered_map<std::wstring, ShadowLightRuntimeState> ShadowLightRuntimeStates;
	std::vector<ShadowRenderEntry> ShadowRenderEntries;
	std::vector<XMFLOAT3> RotatedLightDirections;
	D3DWindowShadowConfig ShadowConfig;

	std::vector<XMFLOAT4X4> ShadowTransform = { MathHelps::Identity };
	std::vector<D3D12_RESOURCE_STATES> WorkingShadowMapStates;
	std::vector<D3D12_RESOURCE_STATES> WorkingPointLightShadowCubeStates;
	std::vector<ShadowCache2DResource> StaticShadowCache2DResources;
	std::vector<ShadowCacheCubeResource> StaticPointLightShadowCubeResources;
	UINT ShadowMapHeapStartIndex = 0;
	UINT SharedNormalPrepassHeapStartIndex = 0;
	UINT AmbientOcclusionHeapStartIndex = 0;
	UINT DirectionalShadowMaskHeapStartIndex = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GPUTexDescriptor;

	UINT SkyMapIndex = 0;

	UINT SkyTexHeapIndex = 0;
	UINT NullTextureHeapIndex = 0;
	UINT EnvironmentIblHeapStartIndex = UINT(-1);
	bool EnvironmentIblDescriptorsInitialized = false;

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE environmentIblDescriptorTable;
	CD3DX12_GPU_DESCRIPTOR_HANDLE shadow2DDescriptorTable;
	CD3DX12_GPU_DESCRIPTOR_HANDLE ambientOcclusionDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE directionalShadowMaskDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE otherTexDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE postProcessSceneColorDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE colorAdjustSceneColorDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE interactionOutlineMaskDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE transparentOitAccumDescriptor;
	CD3DX12_GPU_DESCRIPTOR_HANDLE transparentOitRevealDescriptor;
	UINT PostProcessSceneColorRtvStartIndex = 0;
	UINT PostProcessSceneColorHeapStartIndex = 0;
	bool PostProcessSceneColorDescriptorsInitialized = false;
	UINT ColorAdjustSceneColorRtvStartIndex = 0;
	UINT ColorAdjustSceneColorHeapStartIndex = 0;
	bool ColorAdjustSceneColorDescriptorsInitialized = false;
	UINT InteractionOutlineMaskRtvStartIndex = 0;
	UINT InteractionOutlineMaskHeapStartIndex = 0;
	bool InteractionOutlineMaskDescriptorsInitialized = false;
	UINT DirectionalShadowMaskRtvStartIndex = 0;
	bool DirectionalShadowMaskDescriptorsInitialized = false;
	UINT SharedNormalPrepassDepthDsvIndex = 0;
	UINT SharedSceneInputHeapStartIndex = 0;
	bool SharedSceneInputDescriptorsInitialized = false;
	UINT TransparentOitHeapStartIndex = 0;
	bool TransparentOitDescriptorsInitialized = false;
	DXGI_FORMAT TransparentOitAccumFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	DXGI_FORMAT TransparentOitRevealFormat = DXGI_FORMAT_R16_FLOAT;

	struct RenderFramePlan
	{
		bool Valid = false;
		UINT BackBufferIndex = 0;
		bool HasSkyRenderItems = false;
		bool HasOpaqueRenderItems = false;
		bool HasTransparentRenderItems = false;
		bool HasAoRenderItems = false;
		bool HasShadowCasterRenderItems = false;
		bool HasStaticShadowCasterRenderItems = false;
		bool HasDynamicShadowCasterRenderItems = false;
		bool HasOitResources = false;
	};
	RenderFramePlan CurrentRenderFramePlan;

	PointLightShadowCubePool pointLightShadowCubePool;  // 点光源 cubemap 阴影资源池
	ShadowMapPass shadowMapPass; // 独立的阴影子系统。
	SharedNormalPrepass sharedNormalPrepass;
	AmbientOcclusionPass ambientOcclusion;
	DirectionalShadowMaskPass mDirectionalShadowMaskPass;
	InteractionOutlinePass interactionOutlinePass;
	VolumetricLightPass volumetricLightPass;
	ColorAdjustPass mColorAdjustPass;
	OITCompositePass mOITCompositePass;
	FXAAPass mFXAAPass;
	SkeletonOverlayPass mSkeletonOverlayPass;

	std::unique_ptr<UploadBuffer<ObjectConstants>> SkeletonOverlayObjectCB = nullptr;
	std::array<std::unique_ptr<UploadBuffer<SkeletonOverlayGpuVertex>>, SwapChainBufferCount> SkeletonOverlayVertexBuffers = {};
	std::array<std::unique_ptr<UploadBuffer<std::uint32_t>>, SwapChainBufferCount> SkeletonOverlayIndexBuffers = {};
	std::unique_ptr<UploadBuffer<ObjectConstants>> GizmoObjectCB = nullptr;
	GizmoPickMeshData TranslateGizmoPickMesh;
	GizmoPickMeshData RotateGizmoPickMesh;
	GizmoPickMeshData ScaleGizmoPickMesh;

	// 点光源 cubemap 参数缓存
	std::vector<ShadowFrameBuildResult::PointLightCubeParams> CurrentPointLightCubeParams;
	CD3DX12_GPU_DESCRIPTOR_HANDLE pointLightShadowCubeDescriptor;
	D3DWindowShadowMaskConfig ShadowMaskConfig;

	// =========================
	// 渲染项与批次
	// =========================
	//所有渲染项的列表。
	std::map<std::wstring, RenderItem> AllRitems;
	std::unordered_map<std::wstring, ObjectCollection> BillboardObjectCollectionCache;
	std::unordered_map<std::wstring, AggregateGraphicObj> BillboardAggrObjectCache;
	std::unordered_map<std::wstring, ObjectCollection> DrawSetObjectCollectionCache;
	std::unordered_map<std::wstring, AggregateGraphicObj> DrawSetAggrObjectCache;
	bool FreshenAllObject = false;
	std::unordered_set<std::wstring> DirtyObjectCBItems;
	bool FreshenAllMaterial = false;
	bool FreshenAllLight = true;

	// 按 PSO 划分渲染项目。
	std::map<std::wstring, RenderItem*> RitemLayer[(UINT)渲染项目计数];
	std::vector<RenderItem*> OpaqueThreadBatches[NumContexts];
	std::vector<RenderItem*> TransparentThreadBatches[NumContexts];
	std::vector<RenderItem*> FrameSkyRenderItems;
	std::vector<RenderItem*> FrameDebugRenderItems;
	std::vector<RenderItem*> FrameSelectedOutlineRenderItems;
	std::vector<RenderItem*> FrameStaticShadowCasterRenderItems;
	std::vector<RenderItem*> FrameDynamicShadowCasterRenderItems;
	//派生类(如果有)应在派生构造函数中设置这些值以自定义起始值。
	DXGI_FORMAT IndexBufferFormat = DXGI_FORMAT_R32_UINT;
	DXGI_FORMAT BackBufferFormat;
	DXGI_FORMAT DepthStencilFormat;

	// =========================
	// 线程同步与命令执行
	// =========================
	// 线程同步对象:按 pass 分开唤醒,避免额外维护"当前 pass"全局状态。
	HANDLE workerBeginRecordCommand[工作阶段计数][NumContexts] = { nullptr };
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
	// 从实体附加渲染项
	void AppendRenderItemsFromEntity(SceneEntityBase* entity, WitchcraECS* ecs);

	void WorkerThread(int threadIndex);

	PassConstants MainPassCB; // 主渲染 pass 常量。
	PassConstants ShadowPassCB; // 阴影 pass 常量。
	PostProcessConstants MainPostProcessCB; // 后处理参数(FXAA 等)。
	D3DWindowAOConfig AOConfig;
	static constexpr const wchar_t* DefaultBillboardGeometryName = L"billboardQuadGeo";

	Camera mCamera;
	mutable std::mutex mFrustumCullingReferenceMutex;
	bool mFrustumCullingReferenceLocked = false;
	bool mHasLiveFrustumCullingReference = false;
	DirectX::XMFLOAT4X4 mLiveFrustumCullingView = MathHelps::Identity;
	DirectX::XMFLOAT4X4 mLiveFrustumCullingProj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 mLockedCullingView = MathHelps::Identity;
	DirectX::XMFLOAT4X4 mLockedCullingProj = MathHelps::Identity;

	Timer* mTimer;
	TextRenderPass* textR = nullptr;
	std::wstring Text = L"";
	bool renderFPS = false;

	bool renderEditor = true;
	Editor* mEditor = nullptr;
	// 非拥有指针:记录最近一次由外部显式传入的 ECS,
	// 仅用于把旧接口转发到新的带 ecs 接口。
	WitchcraECS* mLastExternalECS = nullptr;

public:
	// 创建默认堆资源,并通过 upload buffer 完成一次性上传。
	static ComPtr<ID3D12Resource> CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		ComPtr<ID3D12Resource>& uploadBuffer);
};
