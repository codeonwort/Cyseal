#pragma once

#include "app.h"

#include "world/scene.h"
#include "world/camera.h"

class World
{
public:
	World() {}
	virtual ~World() {}

	void preinitialize(Scene* inScene, Camera* inCamera, AppState* inAppState)
	{
		scene = inScene;
		camera = inCamera;
		appState = inAppState;
	}

	virtual void onInitialize() = 0;
	virtual void onTick(float deltaSeconds) = 0;
	virtual void onTerminate() = 0;

protected:
	Scene* scene = nullptr;
	Camera* camera = nullptr;
	AppState* appState = nullptr;
};
