#pragma once

#include "D3DWindow.h"

// D3DWindow 几何体管理静态提供者。
// 将 D3DWindow 的 AddShapeGeometry / AddBillboardGeometry / AddTransformGizmoGeometry
// 等函数提取到此，通过 D3DWindow* 参数访问所需成员。
struct D3DWindowGeometryProvider
{
	static void AddShapeGeometry(D3DWindow* window);
	static void AddShapeGeometry(D3DWindow* window, MeshGeometry* geo);
	static void AddBillboardGeometry(D3DWindow* window);
	static void AddTransformGizmoGeometry(D3DWindow* window);
	static void RemoveShapeGeometry(D3DWindow* window, std::wstring name);
	static bool HasShapeGeometry(const D3DWindow* window, const std::wstring& name);
};
