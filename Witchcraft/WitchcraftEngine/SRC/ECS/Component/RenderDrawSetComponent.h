#pragma once

#include <string>
#include <vector>

#include "BaseComponent.h"

class SceneEntityBase;

struct RenderDrawSlice
{
	std::wstring RenderItemName;
	std::wstring GeometryName;
	std::wstring MaterialName;
	SceneEntityBase* TransformSourceEntity = nullptr;
	UINT RenderLayerIndex = 0;
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;
	bool IsSkinned = false;
};

class RenderDrawSetComponent : public BaseComponent
{
public:
	void Clear() { mDraws.clear(); }
	void SetDraws(const std::vector<RenderDrawSlice>& draws) { mDraws = draws; }
	void SetDraws(std::vector<RenderDrawSlice>&& draws) { mDraws = std::move(draws); }
	void AddDraw(const RenderDrawSlice& draw) { mDraws.push_back(draw); }

	const std::vector<RenderDrawSlice>& GetDraws() const { return mDraws; }
	std::vector<RenderDrawSlice>& GetDraws() { return mDraws; }

	virtual ComponentType GetComponentType() override { return mComponentType; }

private:
	std::vector<RenderDrawSlice> mDraws;
	ComponentType mComponentType = ComponentType::Co_RenderDrawSet;
};
