#include "render_command.h"
#include "render_device.h"
#include "swap_chain.h"

void RenderCommandList::executeDeferredDealloc()
{
	for (auto deallocFn : deferredDeallocs)
	{
		deallocFn();
	}
	deferredDeallocs.clear();
}
