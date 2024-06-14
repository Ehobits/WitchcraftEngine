#pragma once

#include "D3D12_framework.h"

#include "Helpers/MathHelpers.h"

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

struct CameraParameters
{
	float camNearZ = 0.1f;
	float camFarZ = 1000.0f;
	float camFov = 45.0f;
	float camSpeed = 8.0f;
	float camSensitivity = 0.001f;
};

#define MaxLights 16

struct LightData
{
	DirectX::XMFLOAT3 Strength = { 0.0f, 0.0f, 0.0f };
	float FalloffStart = 1.0f;                          // point/spot light only
	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
	float FalloffEnd = 10.0f;                           // point/spot light only
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
	float SpotPower = 64.0f;                            // spot light only
};

// 通道常量
struct PassConstants
{
	DirectX::XMFLOAT4X4 View = MathHelps::Identity;
	DirectX::XMFLOAT4X4 InvView = MathHelps::Identity;
	DirectX::XMFLOAT4X4 Proj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 InvProj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 ViewProj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 InvViewProj = MathHelps::Identity;
	DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	//float cbPerObjectPad1 = 0.0f;
	DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f }; // 渲染目标尺寸
	DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
	//float NearZ = 0.0f;
	//float FarZ = 0.0f;
};

struct LightConstants
{
	DirectX::XMFLOAT4 AmbientLight = { 1.0f, 1.0f, 1.0f, 1.0f }; // 环境光
	// 索引 [0, NUM_DIR_LIGHTS) 是定向灯；
	// 索引 [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) 是点光源；
	// 索引 [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS) 是每个对象最多 MaxLights 的聚光灯。
	LightData Lights[MaxLights];
};

// AO常量
struct AOConstants
{
	DirectX::XMFLOAT4X4 Proj;
	DirectX::XMFLOAT4X4 InvProj;
	DirectX::XMFLOAT4X4 ProjTex;
	DirectX::XMFLOAT4   OffsetVectors[14];

	// For SsaoBlur.hlsl
	DirectX::XMFLOAT4 BlurWeights[3];

	//DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f }; // 渲染目标尺寸
	DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };

	// Coordinates given in view space.
	float OcclusionRadius = 0.5f;
	float OcclusionFadeStart = 0.2f;
	float OcclusionFadeEnd = 2.0f;
	float SurfaceEpsilon = 0.05f;
};

// 对象常量
struct ObjectConstants
{
	DirectX::XMFLOAT4X4 WorldTransform = MathHelps::Identity;
	DirectX::XMFLOAT4X4 TexTransform = MathHelps::Identity;
	UINT     MaterialIndex = -1;
};

struct MeshGeometry
{
	std::wstring Name= L"空闲"; // 默认名称
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
};
