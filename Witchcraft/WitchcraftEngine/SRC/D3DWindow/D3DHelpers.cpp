#include "D3DHelpers.h"

DirectX::XMFLOAT4X4 BuildWorldMatrixFromTransformData(const Transform& transform)
{
	DirectX::XMFLOAT4X4 worldTransform = MathHelps::Identity;
	DirectX::XMVECTOR zero = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	DirectX::XMStoreFloat4x4(&worldTransform,
		DirectX::XMMatrixAffineTransformation(
			DirectX::XMLoadFloat3(&transform.scale),
			zero,
			DirectX::XMQuaternionRotationRollPitchYaw(
				transform.rotation.x * MathHelps::Pi / 45.0f / 4.0f,
				transform.rotation.y * MathHelps::Pi / 45.0f / 4.0f,
				transform.rotation.z * MathHelps::Pi / 45.0f / 4.0f),
		DirectX::XMLoadFloat3(&transform.position)));
	return worldTransform;
}

DirectX::XMFLOAT4X4 BuildWorldInverseTransposeMatrixFromWorldTransform(const DirectX::XMFLOAT4X4& worldTransform)
{
	DirectX::XMFLOAT4X4 worldInvTranspose = MathHelps::Identity;
	const DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&worldTransform);
	const DirectX::XMVECTOR determinant = DirectX::XMMatrixDeterminant(world);
	if (DirectX::XMVectorGetX(determinant) == 0.0f)
		return worldInvTranspose;

	DirectX::XMStoreFloat4x4(&worldInvTranspose, DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, world)));
	return worldInvTranspose;
}

DirectX::XMFLOAT4X4 BuildWorldInverseTransposeMatrixFromWorldTransform(const DirectX::XMMATRIX& worldTransform)
{
	DirectX::XMFLOAT4X4 worldTransformData = MathHelps::Identity;
	DirectX::XMStoreFloat4x4(&worldTransformData, worldTransform);
	return BuildWorldInverseTransposeMatrixFromWorldTransform(worldTransformData);
}

void BuildSkyRenderTransforms(const Transform* transform, DirectX::XMFLOAT4X4* outWorldTransform, DirectX::XMFLOAT4X4* outTexTransform)
{
	if (outWorldTransform == nullptr || outTexTransform == nullptr)
		return;

	if (transform == nullptr)
	{
		DirectX::XMStoreFloat4x4(outWorldTransform, DirectX::XMMatrixScaling(8000.0f, 8000.0f, 8000.0f));
		DirectX::XMStoreFloat4x4(outTexTransform, DirectX::XMMatrixIdentity());
		return;
	}

	DirectX::XMStoreFloat4x4(outWorldTransform, DirectX::XMMatrixScaling(
		8000.0f * transform->scale.x,
		8000.0f * transform->scale.y,
		8000.0f * transform->scale.z));
	DirectX::XMStoreFloat4x4(outTexTransform, DirectX::XMMatrixRotationRollPitchYaw(
		transform->rotation.x * MathHelps::Pi / 45.0f / 4.0f,
		transform->rotation.y * MathHelps::Pi / 45.0f / 4.0f,
		transform->rotation.z * MathHelps::Pi / 45.0f / 4.0f));
}

void SetD3DObjectName(ID3D12Object* object, const wchar_t* name)
{
	if (object != nullptr && name != nullptr)
		object->SetName(name);
}

std::wstring BuildD3DFrameObjectName(const wchar_t* prefix, UINT frameIndex)
{
	return std::wstring(prefix != nullptr ? prefix : L"D3D对象") + L"_帧" + std::to_wstring(frameIndex);
}

std::wstring BuildD3DFrameThreadObjectName(const wchar_t* prefix, UINT frameIndex, UINT threadIndex)
{
	return BuildD3DFrameObjectName(prefix, frameIndex) + L"_线程" + std::to_wstring(threadIndex);
}

HRESULT WINAPI DXTraceW(_In_z_ const WCHAR* strFile, _In_ DWORD dwLine, _In_ HRESULT hr,
	_In_opt_ const WCHAR* strMsg, _In_ bool bPopMsgBox)
{
	WCHAR strBufferFile[MAX_PATH];
	WCHAR strBufferLine[128];
	WCHAR strBufferError[300];
	WCHAR strBufferMsg[1024];
	WCHAR strBufferHR[40];
	WCHAR strBuffer[3000];

	swprintf_s(strBufferLine, 128, L"%lu", dwLine);
	if (strFile)
	{
		swprintf_s(strBuffer, 3000, L"%ls(%ls): ", strFile, strBufferLine);
		OutputDebugStringW(strBuffer);
	}

	size_t nMsgLen = (strMsg) ? wcsnlen_s(strMsg, 1024) : 0;
	if (nMsgLen > 0)
	{
		OutputDebugStringW(strMsg);
		OutputDebugStringW(L" ");
	}
	// Windows SDK 8.0起DirectX的错误信息已经集成进错误码中，可以通过FormatMessageW获取错误信息字符串
	// 不需要分配字符串内存
	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		strBufferError, 256, nullptr);

	WCHAR* errorStr = wcsrchr(strBufferError, L'\r');
	if (errorStr)
	{
		errorStr[0] = L'\0';	// 擦除FormatMessageW带来的换行符(把\r\n的\r置换为\0即可)
	}

	swprintf_s(strBufferHR, 40, L" (0x%0.8x)", hr);
	wcscat_s(strBufferError, strBufferHR);
	swprintf_s(strBuffer, 3000, L"错误码含义：%ls", strBufferError);
	OutputDebugStringW(strBuffer);

	OutputDebugStringW(L"\n");

	if (bPopMsgBox)
	{
		wcscpy_s(strBufferFile, MAX_PATH, L"");
		if (strFile)
			wcscpy_s(strBufferFile, MAX_PATH, strFile);

		wcscpy_s(strBufferMsg, 1024, L"");
		if (nMsgLen > 0)
			swprintf_s(strBufferMsg, 1024, L"当前调用：%ls\n", strMsg);

		swprintf_s(strBuffer, 3000, L"文件名：%ls\n行号：%ls\n错误码含义：%ls\n%ls您需要调试当前应用程序吗？",
			strBufferFile, strBufferLine, strBufferError, strBufferMsg);

		int nResult = MessageBoxW(GetForegroundWindow(), strBuffer, L"错误", MB_YESNO | MB_ICONERROR);
		if (nResult == IDYES)
			DebugBreak();
	}

	return hr;
}

// 转换跟踪资源状态
void TransitionTrackedResourceState(
	ID3D12GraphicsCommandList* cmdList,
	ID3D12Resource* resource,
	D3D12_RESOURCE_STATES& currentState,
	D3D12_RESOURCE_STATES targetState)
{
	if (currentState == targetState)
		return;

	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		resource,
		currentState,
		targetState);
	cmdList->ResourceBarrier(1, &barrier);
	currentState = targetState;
}

// 编译着色器
ComPtr<ID3DBlob> CompileShader(
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
