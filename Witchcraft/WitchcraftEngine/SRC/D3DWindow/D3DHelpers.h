#pragma once

#include "D3D12_framework.h"
#include "Helpers/MathHelpers.h"
#include "Common/TransformSharedTypes.h"

// ------------------------------
// DXTraceW鍑芥暟
// ------------------------------
// 鍦ㄨ皟璇曡緭鍑虹獥鍙ｄ腑杈撳嚭鏍煎紡鍖栭敊璇俊鎭紝鍙€夌殑閿欒绐楀彛寮瑰嚭(宸叉眽鍖?
// [In]strFile			褰撳墠鏂囦欢鍚嶏紝閫氬父浼犻€掑畯__FILEW__
// [In]hlslFileName     褰撳墠琛屽彿锛岄€氬父浼犻€掑畯__LINE__
// [In]hr				鍑芥暟鎵ц鍑虹幇闂鏃惰繑鍥炵殑HRESULT鍊?
// [In]strMsg			鐢ㄤ簬甯姪璋冭瘯瀹氫綅鐨勫瓧绗︿覆锛岄€氬父浼犻€扡#x(鍙兘涓篘ULL)
// [In]bPopMsgBox       濡傛灉涓篢RUE锛屽垯寮瑰嚭涓€涓秷鎭脊绐楀憡鐭ラ敊璇俊鎭?
// 杩斿洖鍊? 褰㈠弬hr
HRESULT WINAPI DXTraceW(_In_z_ const WCHAR* strFile, _In_ DWORD dwLine, _In_ HRESULT hr, _In_opt_ const WCHAR* strMsg, _In_ bool bPopMsgBox);

// ------------------------------
// ThrowIfFailed瀹?
// ------------------------------
// Debug妯″紡涓嬬殑閿欒鎻愰啋涓庤拷韪?
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
	// 闇€瑕佽绠楀榻愭亽瀹氱殑缂撳啿鍖哄ぇ灏忋€?
	return (byteSize + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
}
DirectX::XMFLOAT4X4 BuildWorldMatrixFromTransformData(const Transform& transform);
void BuildSkyRenderTransforms(const Transform* transform, DirectX::XMFLOAT4X4* outWorldTransform, DirectX::XMFLOAT4X4* outTexTransform);

struct CameraParameters
{
	float camNearZ = 0.1f;
	float camFarZ = 1000.0f;
	float camFov = 45.0f;
	float camSpeed = 8.0f;
	float camSensitivity = 0.001f;
};

struct LightData
{
	DirectX::XMFLOAT3 Color = { 0.0f, 0.0f, 0.0f }; // 棰滆壊
	float Type = 0.0f;	// x: 0=鐜鍏? 1=瀹氬悜鍏? 2=鐐瑰厜, 3=鑱氬厜
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	float ShadowMapIndex = -1.0f;
	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
	float Power = 1.0f;
};

// 閫氶亾甯搁噺
struct PassConstants
{
	DirectX::XMFLOAT4X4 View = MathHelps::Identity;
	DirectX::XMFLOAT4X4 InvView = MathHelps::Identity;
	DirectX::XMFLOAT4X4 Proj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 InvProj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 ViewProj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 InvViewProj = MathHelps::Identity;
	DirectX::XMFLOAT4X4 ViewProjTex = MathHelps::Identity;
	DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	float cbPerObjectPad0 = 0.0f;
	DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f }; // 娓叉煋鐩爣灏哄
	DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 ShadowTransform[256] = { MathHelps::Identity };
	DirectX::XMFLOAT2 ShadowSettings = { 0.65f, 1.5f };
	DirectX::XMFLOAT2 cbPerObjectPad2 = { 0.0f, 0.0f };
	UINT LightConst = 0;
};

struct LightConstants
{
	DirectX::XMFLOAT4 AmbientColor;
	LightData Lights[256];
};

// AO甯搁噺
struct AOConstants
{
	DirectX::XMFLOAT4X4 Proj;
	DirectX::XMFLOAT4X4 InvProj;
	DirectX::XMFLOAT4X4 ProjTex;
	DirectX::XMFLOAT4   OffsetVectors[14];

	// For SsaoBlur.hlsl
	DirectX::XMFLOAT4 BlurWeights[3];
	DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };

	// Coordinates given in view space.
	float OcclusionRadius = 0.5f;
	float OcclusionFadeStart = 0.2f;
	float OcclusionFadeEnd = 2.0f;
	float SurfaceEpsilon = 0.05f;
};

// 瀵硅薄甯搁噺
struct ObjectConstants
{
	DirectX::XMFLOAT4X4 WorldTransform = MathHelps::Identity;
	DirectX::XMFLOAT4X4 TexTransform = MathHelps::Identity;
};

struct MeshGeometry
{
	std::wstring Name= L"绌洪棽"; // 榛樿鍚嶇О
	// 绯荤粺鍐呭瓨鍓湰銆備娇鐢?Blob 鍥犱负椤剁偣/绱㈠紩鏍煎紡鍙互鏄€氱敤鐨勩€?
	// 鐢卞鎴风鏉ラ€傚綋鍦拌繘琛岃浆鎹€?
	ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
};
