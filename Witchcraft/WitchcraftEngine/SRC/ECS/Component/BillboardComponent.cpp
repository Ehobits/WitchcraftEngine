#include "BillboardComponent.h"

#include <algorithm>

void BillboardComponent::SetMode(BillboardMode mode)
{
	mData.Mode = mode;
}

BillboardMode BillboardComponent::GetMode() const
{
	return mData.Mode;
}

void BillboardComponent::SetFacingMode(BillboardFacingMode facingMode)
{
	mData.FacingMode = facingMode;
}

BillboardFacingMode BillboardComponent::GetFacingMode() const
{
	return mData.FacingMode;
}

void BillboardComponent::SetSize(float width, float height)
{
	mData.Width = (std::max)(width, 0.001f);
	mData.Height = (std::max)(height, 0.001f);
}

float BillboardComponent::GetWidth() const
{
	return mData.Width;
}

float BillboardComponent::GetHeight() const
{
	return mData.Height;
}

void BillboardComponent::SetScreenSize(float screenSize)
{
	mData.ScreenSize = (std::max)(screenSize, 1.0f);
}

float BillboardComponent::GetScreenSize() const
{
	return mData.ScreenSize;
}

void BillboardComponent::SetOffset(const DirectX::XMFLOAT3& offset)
{
	mData.Offset = offset;
}

const DirectX::XMFLOAT3& BillboardComponent::GetOffset() const
{
	return mData.Offset;
}

void BillboardComponent::SetColor(const DirectX::XMFLOAT4& color)
{
	mData.Color = color;
}

const DirectX::XMFLOAT4& BillboardComponent::GetColor() const
{
	return mData.Color;
}

void BillboardComponent::SetMaterialName(const std::wstring& materialName)
{
	mMaterialName = materialName;
}

const std::wstring& BillboardComponent::GetMaterialName() const
{
	return mMaterialName;
}

BillboardData BillboardComponent::BuildData() const
{
	return mData;
}
