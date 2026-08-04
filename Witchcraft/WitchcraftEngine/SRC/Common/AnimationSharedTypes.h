#pragma once

#include <cstdint>
#include <xstring>
#include <vector>

#include <DirectXMath.h>

namespace Witchcraft::Animation
{
	struct BoneTranslationKey
	{
		float Time = 0.0f;
		DirectX::XMFLOAT3 Value = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	};

	struct BoneRotationKey
	{
		float Time = 0.0f;
		DirectX::XMFLOAT4 Value = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	};

	struct BoneScaleKey
	{
		float Time = 0.0f;
		DirectX::XMFLOAT3 Value = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	};

	struct BoneMatrixKey
	{
		float Time = 0.0f;
		DirectX::XMFLOAT4X4 Value = DirectX::XMFLOAT4X4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
	};

	struct BoneAnimationTrack
	{
		std::wstring BoneName;
		std::int32_t BoneIndex = -1;
		std::vector<BoneTranslationKey> TranslationKeys;
		std::vector<BoneRotationKey> RotationKeys;
		std::vector<BoneScaleKey> ScaleKeys;
		std::vector<BoneMatrixKey> MatrixKeys;
	};

	struct AnimationClipDesc
	{
		std::wstring Name;
		float Duration = 0.0f;
		float TicksPerSecond = 0.0f;
		bool Loop = true;
		std::vector<BoneAnimationTrack> Tracks;
	};
}
