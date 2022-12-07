#include "render_command.h"
#include "render_device.h"
#include "swap_chain.h"

void RenderCommandList::enqueueCustomCommand(CustomCommandType lambda)
{
	customCommands.push_back(lambda);
}

void RenderCommandList::executeCustomCommands()
{
	for (CustomCommandType lambda : customCommands)
	{
		lambda(*this);
	}
	customCommands.clear();
}

void RenderCommandList::enqueueDeferredDealloc(void* addrToDelete)
{
	deferredDeallocs.push_back(addrToDelete);
}

void RenderCommandList::executeDeferredDealloc()
{
	for (void* addr : deferredDeallocs)
	{
		delete addr;
	}
	deferredDeallocs.clear();
}

// ---------------------------------------------------------------------

EnqueueCustomRenderCommand::EnqueueCustomRenderCommand(RenderCommandList::CustomCommandType inLambda)
{
	uint32 swapchainIx = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	RenderCommandList* commandList = gRenderDevice->getCommandList(swapchainIx);
	commandList->enqueueCustomCommand(inLambda);
}

#if 0
FlushRenderCommands::FlushRenderCommands()
{
	uint32 swapchainIx = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	RenderCommandAllocator* commandAllocator = gRenderDevice->getCommandAllocator(swapchainIx);
	RenderCommandList* commandList = gRenderDevice->getCommandList(swapchainIx);
	RenderCommandQueue* commandQueue = gRenderDevice->getCommandQueue();

	commandAllocator->reset();
	commandList->reset(commandAllocator);
	commandList->executeCustomCommands();
	commandList->close();
	commandQueue->executeCommandList(commandList);

	gRenderDevice->flushCommandQueue();
}
#endif
