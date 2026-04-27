#pragma once

#include "core/win/windows_application.h"
#include "render/renderer_options.h"
#include "world/scene.h"
#include "world/camera.h"

struct AppState
{
	// Rendering related
	RendererOptions rendererOptions;
	int32 displayScale                    = 100;
	int32 renderResolutionScale           = 100;
	int32 selectedIndirectDrawMode        = (int32)EIndirectDrawMode::PopulateOnGPU;
	int32 selectedBufferVisualizationMode = 0;
	int32 selectedRayTracedShadowsMode    = 0;
	int32 selectedIndirectDiffuseMode     = 0;
	int32 selectedIndirectSpecularMode    = 0;
	int32 selectedPathTracingMode         = 0;
	int32 selectedPathTracingKernel       = 0;
	uint32 pathTracingNumFrames           = 0;
	int32 pathTracingMaxFrames            = 64;
	// World management
	int32 currentWorldIndex               = 0; // EWorldIndex
};

class TestApplication : public WindowsApplication
{

protected:
	virtual bool onInitialize() override;
	virtual void onTick(float deltaSeconds) override;
	virtual void onTerminate() override;

	virtual void onWindowResize(uint32 newWidth, uint32 newHeight) override;

private:
	void resetSceneAndCamera();

private:
	Scene scene;
	Camera camera;
	AppState appState;

	class World* world = nullptr;
	bool bChangeWorld = false;

	bool bViewportNeedsResize = false;
	uint32 newViewportWidth = 0;
	uint32 newViewportHeight = 0;

	float framesPerSecond = 0.0f;
};
