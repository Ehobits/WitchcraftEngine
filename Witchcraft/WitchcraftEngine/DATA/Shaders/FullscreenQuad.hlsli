#ifndef WITCHCRAFT_FULLSCREEN_QUAD_HLSLI
#define WITCHCRAFT_FULLSCREEN_QUAD_HLSLI

float2 BuildFullscreenQuadTexCoord(uint vertexId)
{
	static const float2 gTexCoords[6] =
	{
		float2(0.0f, 1.0f),
		float2(0.0f, 0.0f),
		float2(1.0f, 0.0f),
		float2(0.0f, 1.0f),
		float2(1.0f, 0.0f),
		float2(1.0f, 1.0f)
	};

	return gTexCoords[vertexId];
}

float4 BuildFullscreenQuadPositionH(float2 texC)
{
	// NDC空间内的四层覆盖屏幕。
	return float4(2.0f * texC.x - 1.0f, 1.0f - 2.0f * texC.y, 0.0f, 1.0f);
}

#endif
