#include "decode_vis_buffer_pass.h"

void DecodeVisBufferPass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;
}

void DecodeVisBufferPass::decodeVisBuffer(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const DecodeVisBufferPassInput& passInput)
{
	//
}
