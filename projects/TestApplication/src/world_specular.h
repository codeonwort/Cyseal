#pragma once

#include "world.h"

#include <vector>

class StaticMesh;

// World to test specular lighting.
class World_Specular : public World
{
	struct BoxSpawnParams
	{
		vec3 scale      = vec3(1, 1, 1);
		vec3 position   = vec3(0, 0, 0);
		vec3 albedo     = vec3(0.9f, 0.9f, 0.9f);
		float roughness = 1.0f;
	};

public:
	virtual void onInitialize() override;
	virtual void onTick(float deltaSeconds) override;
	virtual void onTerminate() override;

	virtual void onRenderGUI() override;

private:
	StaticMesh* ground = nullptr;
	std::vector<StaticMesh*> boxes;

	std::vector<BoxSpawnParams> boxSpawnParams;
};
