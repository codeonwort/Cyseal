#pragma once

#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/texture.h"
#include "render/util/volatile_descriptor.h"

struct DenoiserPluginInput
{
	uint32                 imageWidth;
	uint32                 imageHeight;
	ShaderResourceView*    sceneColorSRV;
	ShaderResourceView*    gbuffer0SRV;
	ShaderResourceView*    gbuffer1SRV;
};

class DenoiserPluginPass final
{
public:
	void initialize();

	bool isAvailable() const;

	void blitTextures(RenderCommandList* commandList, uint32 swapchainIndex, const DenoiserPluginInput& passInput);
	void executeDenoiser(RenderCommandList* commandList, Texture* dst);

private:
	void resizeTextures(uint32 newWidth, uint32 newHeight);

private:
	UniquePtr<ComputePipelineState> blitPipelineState;
	VolatileDescriptorHelper blitPassDescriptor;

	UniquePtr<Texture> colorTexture;
	UniquePtr<Texture> albedoTexture;
	UniquePtr<Texture> normalTexture;
	UniquePtr<Texture> denoisedTexture;

	UniquePtr<UnorderedAccessView> colorUAV;
	UniquePtr<UnorderedAccessView> albedoUAV;
	UniquePtr<UnorderedAccessView> normalUAV;
};
