#pragma once

#include "world.h"

#include <vector>

class StaticMesh;

// World to test specular lighting.
class World_Specular : public World
{
public:
	virtual void onInitialize() override;
	virtual void onTick(float deltaSeconds) override;
	virtual void onTerminate() override;

private:
	StaticMesh* ground = nullptr;
	std::vector<StaticMesh*> boxes;
};
