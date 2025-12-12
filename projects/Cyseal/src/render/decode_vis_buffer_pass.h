#pragma once

#include "scene_render_pass.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/pipeline_state.h"
#include "util/volatile_descriptor.h"

class GPUScene;

struct DecodeVisBufferPassInput
{
	uint32                  textureWidth;
	uint32                  textureHeight;
	GPUScene*               gpuScene;
	ConstantBufferView*     sceneUniformBuffer;
	Texture*                sceneDepthTexture;
	ShaderResourceView*     sceneDepthSRV;
	Texture*                visBufferTexture;
	ShaderResourceView*     visBufferSRV;
	Texture*                barycentricTexture;
	UnorderedAccessView*    barycentricUAV;
	Texture*                visGBuffer0;
	Texture*                visGBuffer1;
	UnorderedAccessView*    visGBuffer0UAV;
	UnorderedAccessView*    visGBuffer1UAV;
};

class DecodeVisBufferPass final : public SceneRenderPass
{
public:
	void initialize(RenderDevice* inRenderDevice);

	void decodeVisBuffer(RenderCommandList* commandList, uint32 swapchainIndex, const DecodeVisBufferPassInput& passInput);

private:
	RenderDevice* device = nullptr;

	UniquePtr<ComputePipelineState> decodePipeline;
	VolatileDescriptorHelper        decodePassDescriptor;
};
