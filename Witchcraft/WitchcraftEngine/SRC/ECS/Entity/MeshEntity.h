#include "../WitchcraECS.h"
#include "../Component/BaseComponent.h"
#include <map>

struct MeshEntity : public SceneEntityBase
{
public:
	bool          enabledComponent = true;
	bool          present = false; // 当前的

public:
	void SetAll(D3DWindow* dx);
	void Update();

	void Destroy();

private:
	std::map<std::wstring, BaseComponent*> mComponents;
private:
	EntityType mEntityType = EntityType::En_Mesh;
};