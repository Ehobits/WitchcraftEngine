struct SkinningComputeVertexIn
{
	float3 PositionL;
	float4 Color;
	float3 NormalL;
	float2 TexCoord;
	float3 TangentL;
	float3 BitangentL;
	uint4 BoneIndices;
	float4 BoneWeights;
};

struct SkinningComputeVertexOut
{
	float3 PositionL;
	float4 Color;
	float3 NormalL;
	float2 TexCoord;
	float3 TangentL;
	float3 BitangentL;
};

// 蒙皮常数
cbuffer SkinningConstants : register(b0)
{
	float4x4 BoneMatrices[256];
};

cbuffer SkinningDispatchConstants : register(b1)
{
	uint VertexCount;
	uint Padding0;
	uint Padding1;
	uint Padding2;
};

StructuredBuffer<SkinningComputeVertexIn> InputVertices : register(t0);
RWStructuredBuffer<SkinningComputeVertexOut> OutputVertices : register(u0);

static const uint G_SKIN_BONE_MATRIX_COUNT = 256u;

uint4 ClampSkinBoneIndices(uint4 boneIndices)
{
	const uint maxBoneIndex = G_SKIN_BONE_MATRIX_COUNT - 1u;
	return min(boneIndices, uint4(maxBoneIndex, maxBoneIndex, maxBoneIndex, maxBoneIndex));
}

bool IsFiniteFloat3(float3 value)
{
	return all(value == value) && all(abs(value) < 1.0e20f);
}

// 从权重构建蒙皮矩阵
float4x4 BuildSkinningMatrixFromWeights(uint4 boneIndices, float4 boneWeights)
{
	const uint4 safeBoneIndices = ClampSkinBoneIndices(boneIndices);
	float4x4 skinningMatrix = 0.0f;
	// 这里需要考虑到原来的骨骼矩阵
	skinningMatrix += BoneMatrices[safeBoneIndices.x] * boneWeights.x;
	skinningMatrix += BoneMatrices[safeBoneIndices.y] * boneWeights.y;
	skinningMatrix += BoneMatrices[safeBoneIndices.z] * boneWeights.z;
	skinningMatrix += BoneMatrices[safeBoneIndices.w] * boneWeights.w;
	return skinningMatrix;
}

float3 SkinPositionL(float3 positionL, float4x4 skinningMatrix)
{
	const float3 skinnedPosition = mul(float4(positionL, 1.0f), skinningMatrix).xyz;
	return IsFiniteFloat3(skinnedPosition) ? skinnedPosition : positionL;
}

float3 SkinDirectionL(float3 directionL, float4x4 skinningMatrix)
{
	const float3 skinnedDirection = mul(directionL, (float3x3)skinningMatrix);
	return IsFiniteFloat3(skinnedDirection) ? skinnedDirection : directionL;
}

float3 SafeNormalize(float3 value, float3 fallbackValue)
{
	const float lengthSquared = dot(value, value);
	if (lengthSquared <= 1e-10f)
		return fallbackValue;

	return value * rsqrt(lengthSquared);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	const uint vertexIndex = dispatchThreadId.x;
	if (vertexIndex >= VertexCount)
		return;

	const SkinningComputeVertexIn inputVertex = InputVertices[vertexIndex];

	float weightSum =
		inputVertex.BoneWeights.x +
		inputVertex.BoneWeights.y +
		inputVertex.BoneWeights.z +
		inputVertex.BoneWeights.w;

	float4 skinnedPosition = float4(inputVertex.PositionL, 1.0f);
	float3 skinnedNormal = inputVertex.NormalL;
	float3 skinnedTangent = inputVertex.TangentL;
	float3 skinnedBitangent = inputVertex.BitangentL;

	if (weightSum > 0.00001f)
	{
		const float4x4 skinMatrix = BuildSkinningMatrixFromWeights(inputVertex.BoneIndices, inputVertex.BoneWeights);
		skinnedPosition = float4(SkinPositionL(inputVertex.PositionL, skinMatrix), 1.0f);
		skinnedNormal = SkinDirectionL(inputVertex.NormalL, skinMatrix);
		skinnedTangent = SkinDirectionL(inputVertex.TangentL, skinMatrix);
		skinnedBitangent = SkinDirectionL(inputVertex.BitangentL, skinMatrix);
	}

	SkinningComputeVertexOut outputVertex;
	outputVertex.PositionL = skinnedPosition.xyz;
	outputVertex.Color = inputVertex.Color;
	outputVertex.NormalL = SafeNormalize(skinnedNormal, inputVertex.NormalL);
	outputVertex.TexCoord = inputVertex.TexCoord;
	outputVertex.TangentL = SafeNormalize(skinnedTangent, inputVertex.TangentL);
	outputVertex.BitangentL = SafeNormalize(skinnedBitangent, inputVertex.BitangentL);
	OutputVertices[vertexIndex] = outputVertex;
}
