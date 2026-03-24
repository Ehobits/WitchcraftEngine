#pragma once

#include <Windows.h>

//娣诲姞WRL鏀寔 鏂逛究浣跨敤COM
#include <wrl.h>
using namespace Microsoft::WRL;

// D3D12
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <d3d12video.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>

using namespace DirectX;
using namespace DirectX::PackedVector;


#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

#include "ResourceUploadBatch.h"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"

// C 杩愯鏃跺ご鏂囦欢
#include <algorithm>
#include <array>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cassert>
#include <filesystem>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

