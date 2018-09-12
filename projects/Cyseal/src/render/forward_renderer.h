#pragma once

#include "renderer.h"

class ForwardRenderer : public Renderer
{

public:
	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void render(const SceneProxy* scene, const Camera* camera) override;

private:
	RenderDevice* device;

};
