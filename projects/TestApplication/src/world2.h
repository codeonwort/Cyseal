#pragma once

#include "world.h"

class StaticMesh;

class World2 : public World
{
public:
	virtual void onInitialize() override;
	virtual void onTick(float deltaSeconds) override;
	virtual void onTerminate() override;

private:
	StaticMesh* ground = nullptr;
};
