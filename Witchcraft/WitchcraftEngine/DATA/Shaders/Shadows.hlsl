#include "Core.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;
	
	// Transform to world space.
	float4 posW = mul(float4(vin.PosL, 1.0f), g_WorldTransform);

	// Transform to homogeneous clip space.
	vout.PosH = mul(posW, g_ViewProj);
	
	// Output vertex attributes for interpolation across triangle.
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), g_TexTransform);
	vout.TexC = texC;
	
	return vout;
}

//这仅用于阿尔法剪切几何体，以便阴影正确显示。
//不需要采样纹理的几何体可以使用NULL像素着色器进行深度传递。
void PS(VertexOut pin) 
{

}


