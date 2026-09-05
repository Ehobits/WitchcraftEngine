#pragma once

#include "../D3D12_framework.h"
#include "../UploadBuffer.h"
#include "D3DPassContext.h"
#include "Common/SkinningSharedTypes.h"
#include <functional>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <memory>

struct BrushVizMeshSlice;
struct BrushVizNode;
struct SkinningConstants;
class WitchcraECS;
class SceneEntityBase;
struct DeferredGeometryReleaseEntry;
class Editor;

// SkinWeightVizPass：蒙皮权重可视化的编辑器渲染通道。
// 从 D3DWindow 中提取的独立 Pass，管理刷权重几何体和绘制逻辑。
class SkinWeightVizPass
{
public:
	void Initialize(ID3D12Device* device);

	void CreatePipesAndShaders(D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc);

	void SetRenderData() {} // 无逐帧渲染数据

	void Draw(const D3DPassContext& context);

	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return mPipelineState.Get(); }

	void SetSharedRootSignature(ID3D12RootSignature* rs) { mRootSignature = rs; }

	// 外部注入的依赖（由 D3DWindow 设置）
	void SetDevice(ID3D12Device* device) { mDevice = device; }
	void SetECS(WitchcraECS* ecs) { mECS = ecs; }
	void SetSrvDescriptorHeap(ID3D12DescriptorHeap* heap) { mSrvHeap = heap; }
	void SetOtherTexDescriptor(D3D12_GPU_DESCRIPTOR_HANDLE desc) { mOtherTexDescriptor = desc; }
	void SetDeferredReleaseQueue(std::vector<DeferredGeometryReleaseEntry>* queue) { mDeferredReleaseQueue = queue; }
	void SetComputeDeferredReleaseFenceFn(std::function<UINT64()> fn) { mComputeFenceFn = std::move(fn); }
	void SetResolveSkinningOwnerFn(std::function<SceneEntityBase*(WitchcraECS*, SceneEntityBase*)> fn) { mResolveSkinnningFn = std::move(fn); }
	void SetFrameResourcePassCB(UploadBuffer<PassConstants>* cb) { mFramePassCB = cb; }

	void SetEditor(Editor* editor) { mEditor = editor; }

	// 刷权重可视化控制
	void SetEnabled(bool enable) { mEnabled = enable; }
	bool IsEnabled() const { return mEnabled; }
	void SetTargetEntity(SceneEntityBase* entity) { mTargetEntity = entity; }
	SceneEntityBase* GetTargetEntity() const { return mTargetEntity; }
	void BuildVisualization() { mNeedsBuild = true; }
	bool NeedsBuild() const { return mNeedsBuild; }
	void ClearNeedsBuild() { mNeedsBuild = false; }
	void TryRebuildFromEditor();

	// 重建几何体
	void RebuildGeometry(const std::vector<BrushVizMeshSlice>& meshes, const BrushVizNode& rootNode);

	// 增量更新刷权重 VB
	void IncrementalUpdateVB(const std::vector<std::tuple<const void*, UINT, Witchcraft::Animation::VertexBoneInfluence4>>& changes);

	// 获取 mesh 偏移表
	const std::unordered_map<const void*, UINT>& GetMeshOffsets() const { return mMeshOffsets; }

private:
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPipelineState = nullptr;
	ComPtr<ID3DBlob> mVertexShader = nullptr;
	ComPtr<ID3DBlob> mPixelShader = nullptr;

	// 外部注入
	ID3D12Device* mDevice = nullptr;
	WitchcraECS* mECS = nullptr;
	Editor* mEditor = nullptr;
	ID3D12DescriptorHeap* mSrvHeap = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE mOtherTexDescriptor = {};
	std::vector<DeferredGeometryReleaseEntry>* mDeferredReleaseQueue = nullptr;
	std::function<UINT64()> mComputeFenceFn;
	std::function<SceneEntityBase*(WitchcraECS*, SceneEntityBase*)> mResolveSkinnningFn;
	UploadBuffer<PassConstants>* mFramePassCB = nullptr;

	// 刷权重可视化状态
	bool mEnabled = false;
	SceneEntityBase* mTargetEntity = nullptr;

	// 刷权重可视化几何体
	MeshGeometry mVisualizationGeometry;
	std::unordered_map<const void*, UINT> mMeshOffsets;
	BYTE* mMappedVertexData = nullptr;
	UINT64 mVertexBufferByteSize = 0;
	bool mNeedsBuild = true;

	// 渲染专用资源
	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
	std::unique_ptr<UploadBuffer<SkinningConstants>> mSkinningCB = nullptr;
};
