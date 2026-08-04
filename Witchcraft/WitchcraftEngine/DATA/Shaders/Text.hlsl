Texture2D t1 : register(t0);
SamplerState s1 : register(s0);

struct VertexIn
{
	float4 PosH : POSITION;
	float4 Color : COLOR;
	float4 TexC : TEXCOORD;
};

struct VertexOut
{
	float4 PosH : SV_POSITION;
	float4 Color : COLOR0;
	float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin, uint vertexID : SV_VertexID)
{
	VertexOut output;

	float2 uv = float2(vertexID & 1, (vertexID >> 1) & 1);
	float2 uvSize = float2(vin.TexC.z - vin.TexC.x, vin.TexC.w - vin.TexC.y);
	output.PosH = float4(vin.PosH.x + (vin.PosH.z * uv.x), vin.PosH.y - (vin.PosH.w * uv.y), 0, 1);
	output.TexC = float2(vin.TexC.x + (uvSize.x * uv.x), vin.TexC.y + (uvSize.y * uv.y));
	output.Color = vin.Color;

	return output;
}

float4 PS(VertexOut input) : SV_TARGET
{
	float4 output;
	// 文字图集是 R8_UNORM 单通道纹理，字形覆盖度写在 red 通道里。
	// 这里如果去读 alpha，会因为默认分量映射而几乎恒为 1，结果整块四边形都变成白方块。
	float glyphCoverage = t1.Sample(s1, input.TexC).r;
	output.rgb = input.Color.rgb;
	output.a = glyphCoverage * input.Color.a;
	return output;
}
