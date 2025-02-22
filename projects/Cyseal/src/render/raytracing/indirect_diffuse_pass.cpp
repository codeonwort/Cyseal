#include "indirect_diffuse_pass.h"

void IndirectDiffusePass::initialize()
{

}

bool IndirectDiffusePass::isAvailable() const
{
	return false;
}

void IndirectDiffusePass::renderIndirectDiffuse(RenderCommandList* commandList, uint32 swapchainIndex, const IndirectDiffuseInput& passInput)
{

}

void IndirectDiffusePass::resizeTextures(RenderCommandList* commandList, uint32 newWidth, uint32 newHeight)
{

}

void IndirectDiffusePass::resizeHitGroupShaderTable(uint32 swapchainIndex, uint32 maxRecords)
{

}
