#pragma once

#include "core/win/windows_application.h"
#include "render/renderer_options.h"
#include "world/scene.h"
#include "world/camera.h"

struct AppState
{
	RendererOptions rendererOptions;
	int32 selectedBufferVisualizationMode = 0;
	int32 selectedRayTracedShadowsMode = 0;
	int32 selectedIndirectSpecularMode = 0;
	int32 selectedPathTracingMode = 0;
	uint32 pathTracingNumFrames = 0;
};

class TestApplication : public WindowsApplication
{

protected:
	virtual bool onInitialize() override;
	virtual void onTick(float deltaSeconds) override;
	virtual void onTerminate() override;

	virtual void onWindowResize(uint32 newWidth, uint32 newHeight) override;

private:
	Scene scene;
	Camera camera;
	AppState appState;

	class World* world = nullptr;

	bool bViewportNeedsResize = false;
	uint32 newViewportWidth = 0;
	uint32 newViewportHeight = 0;

	float framesPerSecond = 0.0f;
};
