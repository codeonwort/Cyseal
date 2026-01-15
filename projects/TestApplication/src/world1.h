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
	void prepareScene();
	void createTestMeshes();
	void createSkybox();
	void createPbrtResources();

	MeshSplatting meshSplatting;
	StaticMesh* ground = nullptr;
	StaticMesh* wallA = nullptr;
	StaticMesh* glassBox = nullptr;

	std::vector<StaticMesh*> pbrtMeshes;
	std::vector<StaticMesh*> pbrtInstancedMeshes;
};
