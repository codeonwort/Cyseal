#include "render_command.h"
#include "render_device.h"

RenderCommandQueue::~RenderCommandQueue()
{
}

RenderCommandAllocator::~RenderCommandAllocator()
{
}

RenderCommandList::~RenderCommandList()
{
}

CustomRenderCommand::CustomRenderCommand(RenderCommandLambda inLambda)
	: lambda(inLambda)
{
	gRenderDevice->getCommandAllocator()->reset();
	gRenderDevice->getCommandList()->reset();

	lambda();

	gRenderDevice->getCommandList()->close();
	gRenderDevice->getCommandQueue()->executeCommandList(gRenderDevice->getCommandList());
	gRenderDevice->flushCommandQueue();
}
