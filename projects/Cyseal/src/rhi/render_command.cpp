#include "render_command.h"
#include "render_device.h"
#include "swap_chain.h"

// ---------------------------------------------------------------------
// RenderCommandList

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

void RenderCommandList::executeDeferredDealloc()
{
	for (auto deallocFn : deferredDeallocs)
	{
		deallocFn();
	}
	deferredDeallocs.clear();
}

// ---------------------------------------------------------------------
// EnqueueCustomRenderCommand

EnqueueCustomRenderCommand::EnqueueCustomRenderCommand(RenderCommandList::CustomCommandType inLambda)
{
	uint32 swapchainIx = gRenderDevice->getCreateParams().bDoubleBuffering
		? gRenderDevice->getSwapChain()->getNextBackbufferIndex()
		: gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	// #wip: hack
	if (gRenderDevice->getSwapChain()->getCurrentBackbufferIndex() == 0xffffffff)
	{
		swapchainIx = gRenderDevice->getCreateParams().bDoubleBuffering
			? 1
			: 0;
	}

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
