#pragma once

#include "renderer.h"
#include "rhi/render_command.h"

// Renders nothing.
class NullRenderer : public Renderer
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void destroy() override;
	virtual void render(const SceneProxy* scene, const Camera* camera, const RendererOptions& renderOptions) override;

	virtual void recreateSceneTextures(uint32 sceneWidth, uint32 sceneHeight) override {}

	virtual void enqueueCustomCommands(std::vector<RenderCommandList::CustomCommandType>&& inCommands) override;

private:
	RenderDevice* device = nullptr;
	uint32 frameID = 0;

	std::vector<RenderCommandList::CustomCommandType> customCommands;
};
