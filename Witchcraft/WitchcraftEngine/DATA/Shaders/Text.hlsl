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
	output.PosH = float4(vin.PosH.x + (vin.PosH.z * uv.x), vin.PosH.y - (vin.PosH.w * uv.y), 0, 1);
	output.TexC = float2(vin.TexC.x + (vin.TexC.z * uv.x), vin.TexC.y + (vin.TexC.w * uv.y));
	output.Color = vin.Color;

	return output;
}

float4 PS(VertexOut input) : SV_TARGET
{
	float4 output;
	output.rgb = input.Color.rgb;
	output.a = t1.Sample(s1, input.TexC).a * input.Color.a;
	return output;
}