#include "render_command.h"
#include "render_device.h"
#include "swap_chain.h"

RenderCommandQueue::~RenderCommandQueue()
{
}

RenderCommandAllocator::~RenderCommandAllocator()
{
}

RenderCommandList::~RenderCommandList()
{
}

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
	RenderCommandList* commandList = gRenderDevice->getCommandList();
	commandList->enqueueCustomCommand(inLambda);
}

FlushRenderCommands::FlushRenderCommands()
{
	uint32 backbufferIx = gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

	RenderCommandAllocator* commandAllocator = gRenderDevice->getCommandAllocator(backbufferIx);
	RenderCommandList* commandList = gRenderDevice->getCommandList();
	RenderCommandQueue* commandQueue = gRenderDevice->getCommandQueue();

	commandAllocator->reset();
	commandList->reset(commandAllocator);
	commandList->executeCustomCommands();
	commandList->close();
	commandQueue->executeCommandList(commandList);

	gRenderDevice->flushCommandQueue();
}
