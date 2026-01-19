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
	void createMeshSplatting();
	void createSkybox();
	void createPbrtResources();

	uint32 meshSplattingDelay = 0;
	MeshSplatting meshSplatting;

	StaticMesh* ground = nullptr;
	StaticMesh* wallA = nullptr;
	StaticMesh* glassBox = nullptr;

	uint32 pbrtLoadDelay = 0;
	std::vector<StaticMesh*> pbrtMeshes;
	std::vector<StaticMesh*> pbrtInstancedMeshes;
};
