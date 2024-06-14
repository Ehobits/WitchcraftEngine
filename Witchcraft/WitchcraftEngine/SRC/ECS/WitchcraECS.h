#include "Engine/EngineUtils.h"
#include "D3DWindow/D3D12_framework.h"

class ProjectSceneSystem;

class WitchcraECS
{
public:
	WitchcraECS();
	~WitchcraECS();

	bool Init();

	void CreateEntity();
};
