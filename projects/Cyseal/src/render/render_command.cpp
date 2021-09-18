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

// ---------------------------------------------------------------------

EnqueueCustomRenderCommand::EnqueueCustomRenderCommand(RenderCommandList::CustomCommandType inLambda)
{
	RenderCommandList* commandList = gRenderDevice->getCommandList();
	commandList->enqueueCustomCommand(inLambda);
}

FlushRenderCommands::FlushRenderCommands()
{
	RenderCommandAllocator* commandAllocator = gRenderDevice->getCommandAllocator();
	RenderCommandList* commandList = gRenderDevice->getCommandList();
	RenderCommandQueue* commandQueue = gRenderDevice->getCommandQueue();

	commandAllocator->reset();
	commandList->reset();
	commandList->executeCustomCommands();
	commandList->close();
	commandQueue->executeCommandList(commandList);

	gRenderDevice->flushCommandQueue();
}
