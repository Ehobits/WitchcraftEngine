#pragma once

#include "BaseComponent.h"
#include "Common/BillboardSharedTypes.h"

class BillboardComponent : public BaseComponent
{
public:
	void SetMode(BillboardMode mode);
	BillboardMode GetMode() const;

	void SetFacingMode(BillboardFacingMode facingMode);
	BillboardFacingMode GetFacingMode() const;

	void SetSize(float width, float height);
	float GetWidth() const;
	float GetHeight() const;

	void SetScreenSize(float screenSize);
	float GetScreenSize() const;

	void SetOffset(const DirectX::XMFLOAT3& offset);
	const DirectX::XMFLOAT3& GetOffset() const;

	void SetColor(const DirectX::XMFLOAT4& color);
	const DirectX::XMFLOAT4& GetColor() const;

	void SetMaterialName(const std::wstring& materialName);
	const std::wstring& GetMaterialName() const;

	BillboardData BuildData() const;

	virtual ComponentType GetComponentType() override { return mComponentType; }

private:
	BillboardData mData;
	std::wstring mMaterialName;
	ComponentType mComponentType = ComponentType::Co_Billboard;
};
