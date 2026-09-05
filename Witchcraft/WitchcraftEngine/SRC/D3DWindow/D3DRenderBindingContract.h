#pragma once

#include "D3D12_framework.h"

namespace D3DRenderBindingContract
{
	inline constexpr UINT ObjectCB = 0;
	inline constexpr UINT PassCB = 1;
	inline constexpr UINT LightCB = 2;
	inline constexpr UINT MaterialCB = 3;
	inline constexpr UINT SkinningCB = 4;
	inline constexpr UINT SkyTexTable = 5;
	inline constexpr UINT OtherTexTable = 6;
	inline constexpr UINT Shadow2DTable = 7;
	inline constexpr UINT PointLightShadowCubeTable = 8;
	inline constexpr UINT AmbientOcclusionTable = 9;
	inline constexpr UINT DirectionalShadowMaskTable = 10;
	inline constexpr UINT ReflectionTable = 11;
	inline constexpr UINT EnvironmentIblTable = 12;
	inline constexpr UINT MainRootParameterCount = 13;

	inline constexpr UINT SkyTexRegister = 0;
	inline constexpr UINT OtherTexRegister = 1;
	inline constexpr UINT ShadowRegisterStart = 13;
	inline constexpr UINT DirectionalShadowRegisterCount = 32u;
	inline constexpr UINT SpotShadowRegisterStart = ShadowRegisterStart + DirectionalShadowRegisterCount;
	inline constexpr UINT SpotShadowRegisterCount = 224u;
	inline constexpr UINT PointLightCubeRegisterStart = SpotShadowRegisterStart + SpotShadowRegisterCount;
	inline constexpr UINT PointLightCubeRegisterCount = 64;
	inline constexpr UINT AmbientOcclusionRegister = PointLightCubeRegisterStart + PointLightCubeRegisterCount;
	inline constexpr UINT DirectionalShadowMaskRegister = AmbientOcclusionRegister + 1;
	inline constexpr UINT ReflectionRegister = DirectionalShadowMaskRegister + 1;
	inline constexpr UINT EnvironmentIblRegisterStart = ReflectionRegister + 1;
}
