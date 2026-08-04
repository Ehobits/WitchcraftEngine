#pragma once

#include "D3D12_framework.h"
#include "Helpers/MathHelpers.h"
#include "Common/TransformSharedTypes.h"
#include "PassConstants.h"
#include "LightConstants.h"
#include "PostProcessConstants.h"

// ------------------------------
// DXTraceW函数
// ------------------------------
// 在调试输出窗口中输出格式化错误信息，可选的错误窗口弹出(已汉化)
// [In]strFile			当前文件名，通常传递宏__FILEW__
// [In]hlslFileName     当前行号，通常传递宏__LINE__
// [In]hr				函数执行出现问题时返回的HRESULT值
// [In]strMsg			用于帮助调试定位的字符串，通常传递L#x(可能为NULL)
// [In]bPopMsgBox       如果为TRUE，则弹出一个消息弹窗告知错误信息
// 返回值: 形参hr
HRESULT WINAPI DXTraceW(_In_z_ const WCHAR* strFile, _In_ DWORD dwLine, _In_ HRESULT hr, _In_opt_ const WCHAR* strMsg, _In_ bool bPopMsgBox);

// ------------------------------
// ThrowIfFailed宏
// ------------------------------
// Debug模式下的错误提醒与追踪
#if defined(DEBUG) | defined(_DEBUG)
#ifndef ThrowIfFailed
#define ThrowIfFailed(x)												\
	{																	\
		HRESULT hr__ = (x);												\
		if(FAILED(hr__))												\
		{																\
			DXTraceW(__FILEW__, (DWORD)__LINE__, hr__, L#x, true);		\
		}																\
	}
#endif
#else
#ifndef ThrowIfFailed
#define ThrowIfFailed(x) (x)
#endif 
#endif

inline UINT CalculateConstantBufferByteSize(UINT byteSize)
{
	// 需要计算对齐恒定的缓冲区大小。
	return (byteSize + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
}
DirectX::XMFLOAT4X4 BuildWorldMatrixFromTransformData(const Transform& transform);
void BuildSkyRenderTransforms(const Transform* transform, DirectX::XMFLOAT4X4* outWorldTransform, DirectX::XMFLOAT4X4* outTexTransform);
void SetD3DObjectName(ID3D12Object* object, const wchar_t* name);
std::wstring BuildD3DFrameObjectName(const wchar_t* prefix, UINT frameIndex);
std::wstring BuildD3DFrameThreadObjectName(const wchar_t* prefix, UINT frameIndex, UINT threadIndex);

struct CameraParameters
{
	float camNearZ = 0.1f;
	float camFarZ = 1000.0f;
	float camFov = 45.0f;
	float camSpeed = 8.0f;
	float camSensitivity = 0.001f;
};

// 对象常量（被所有 Pass 共享的逐对象 CB）
struct ObjectConstants
{
	DirectX::XMFLOAT4X4 WorldTransform = MathHelps::Identity;
	DirectX::XMFLOAT4X4 TexTransform = MathHelps::Identity;
};

struct MeshGeometry
{
	std::wstring Name = L"空闲"; // 默认名称
	// 系统内存副本。使用 Blob 因为顶点/索引格式可以是通用的。
	// 由客户端来适当地进行转换。
	ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	UINT VertexByteStride = 0;
};

// 转换跟踪资源状态
void TransitionTrackedResourceState(
	ID3D12GraphicsCommandList* cmdList,
	ID3D12Resource* resource,
	D3D12_RESOURCE_STATES& currentState,
	D3D12_RESOURCE_STATES targetState);

// 编译着色器
ComPtr<ID3DBlob> CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target);
