#pragma once

#include "core/win/windows_application.h"
#include "world/scene.h"
#include "world/camera.h"
#include "world/gpu_resource_asset.h"
#include <vector>

class StaticMesh;

class TestApplication : public WindowsApplication
{

protected:
	virtual bool onInitialize() override;
	virtual void onTick(float deltaSeconds) override;
	virtual void onTerminate() override;

	virtual void onWindowResize(uint32 newWidth, uint32 newHeight) override;

private:
	void createResources();
	void destroyResources();

private:
	Scene scene;
	Camera camera;

	std::vector<StaticMesh*> balls;
	std::vector<vec3> ballOriginalPos;
	StaticMesh* ground = nullptr;
	StaticMesh* wallA = nullptr;

	bool bViewportNeedsResize = false;
	uint32 newViewportWidth = 0;
	uint32 newViewportHeight = 0;

	float framesPerSecond = 0.0f;
};
