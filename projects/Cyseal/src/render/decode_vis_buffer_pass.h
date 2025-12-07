#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "util/volatile_descriptor.h"

struct DecodeVisBufferPassInput
{
	uint32                  textureWidth;
	uint32                  textureHeight;
	Texture*                barycentricTexture;
	UnorderedAccessView*    barycentricUAV;
};

class DecodeVisBufferPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void decodeVisBuffer(RenderCommandList* commandList, uint32 swapchainIndex, const DecodeVisBufferPassInput& passInput);

private:
	RenderDevice* device = nullptr;
};
