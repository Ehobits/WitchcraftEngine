#pragma once

#include "Engine/EngineUtils.h"
#include "D3DWindow/D3DWindow.h"

class ModelSystem
{
public:
	bool Init(D3DWindow* dx);
	void Shutdown();
};
