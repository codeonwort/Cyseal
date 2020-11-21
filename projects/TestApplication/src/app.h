#pragma once

#include "app_base.h"
#include "world/scene.h"
#include "world/camera.h"
#include <vector>

class StaticMesh;

class Application : public AppBase
{

protected:
	virtual bool onInitialize() override;
	virtual bool onUpdate(float dt) override;
	virtual bool onTerminate() override;

private:
	void createResources();
	void destroyResources();

private:
	Scene scene;
	Camera camera;

	std::vector<StaticMesh*> staticMeshes;

};
