#pragma once

#include "mesh_splatting.h"

#include "core/win/windows_application.h"
#include "render/renderer.h"
#include "world/scene.h"
#include "world/camera.h"
#include "world/gpu_resource_asset.h"

#include <vector>

class StaticMesh;

struct AppState
{
	RendererOptions rendererOptions;
	int32 selectedBufferVisualizationMode = 0;
	uint32 pathTracingNumFrames = 0;

	vec3 cameraLocation;
	float cameraRotationY;
};

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
	AppState appState;

	MeshSplatting meshSplatting;
	
	StaticMesh* pbrtMesh = nullptr;
	StaticMesh* ground = nullptr;
	StaticMesh* wallA = nullptr;

	bool bViewportNeedsResize = false;
	uint32 newViewportWidth = 0;
	uint32 newViewportHeight = 0;

	float framesPerSecond = 0.0f;
};
