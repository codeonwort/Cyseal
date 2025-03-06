#pragma once

#include "world.h"
#include "mesh_splatting.h"

class StaticMesh;

class World1 : public World
{
public:
	virtual void onInitialize() override;
	virtual void onTick(float deltaSeconds) override;
	virtual void onTerminate() override;

private:
	void createResources();

	MeshSplatting meshSplatting;

	StaticMesh* pbrtMesh = nullptr;
	StaticMesh* ground = nullptr;
	StaticMesh* wallA = nullptr;
	StaticMesh* glassBox = nullptr;
};
