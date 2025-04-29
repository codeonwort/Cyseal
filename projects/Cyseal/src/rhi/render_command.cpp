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
// RenderCommandListBucket

RenderCommandListBucket::RenderCommandListBucket(RenderDevice* renderDevice)
{
	device = renderDevice;
	numRequested = 0;
}

RenderCommandListBucket::~RenderCommandListBucket()
{
	commandLists.clear();
	allocators.clear();
	numRequested = 0;
	device = nullptr;
}

void RenderCommandListBucket::reset()
{
	for (uint32 i = 0; i < numRequested; ++i)
	{
		allocators[i]->reset();
		commandLists[i]->reset(allocators[i].get());
	}
	numRequested = 0;
}

RenderCommandList* RenderCommandListBucket::requestFirst()
{
	CHECK(numRequested == 0);
	if (commandLists.size() == 0)
	{
		auto alloc = device->createRenderCommandAllocator();
		auto cmdList = device->createRenderCommandList();
		alloc->reset();
		cmdList->reset(alloc);
		allocators.push_back(UniquePtr<RenderCommandAllocator>(alloc));
		commandLists.push_back(UniquePtr<RenderCommandList>(cmdList));
	}
	return commandLists[0].get();
}

RenderCommandList* RenderCommandListBucket::flushAndRequestNext()
{
	CHECK(numRequested > 0);
	if (numRequested == (uint32)commandLists.size())
	{
		auto alloc = device->createRenderCommandAllocator();
		auto cmdList = device->createRenderCommandList();
		alloc->reset();
		cmdList->reset(alloc);
		allocators.push_back(UniquePtr<RenderCommandAllocator>(alloc));
		commandLists.push_back(UniquePtr<RenderCommandList>(cmdList));
	}
	++numRequested;
	return commandLists[numRequested - 1].get();
}

void RenderCommandListBucket::executeCommandLists(RenderCommandQueue* queue)
{
	for (uint32 i = 0; i < numRequested; ++i)
	{
		allocators[i]->markValid();
	}
	for (uint32 i = 0; i < numRequested; ++i)
	{
		RenderCommandList* cmdList = commandLists[i].get();
		cmdList->executeCustomCommands();
		queue->executeCommandList(cmdList);
		if (i != numRequested - 1)
		{
			device->flushCommandQueue();
		}
		cmdList->executeDeferredDealloc();
	}
}

// ---------------------------------------------------------------------
// EnqueueCustomRenderCommand

EnqueueCustomRenderCommand::EnqueueCustomRenderCommand(RenderCommandList::CustomCommandType inLambda)
{
	uint32 swapchainIx = gRenderDevice->getCreateParams().bDoubleBuffering
		? gRenderDevice->getSwapChain()->getNextBackbufferIndex()
		: gRenderDevice->getSwapChain()->getCurrentBackbufferIndex();

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
