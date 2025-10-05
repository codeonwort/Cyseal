#include "barrier_tracker.h"

BarrierTracker::BarrierTracker(RenderCommandList* inCommandList)
	: commandList(inCommandList)
{
}

BarrierTracker::~BarrierTracker()
{
}

void BarrierTracker::resetAll()
{
	bufferStates.clear();
	textureStates.clear();
}

BufferBarrier BarrierTracker::toBufferBarrier(const BufferBarrierAuto& halfBarrier)
{
	BufferState beforeState{
		.syncBefore   = EBarrierSync::NONE,
		.accessBefore = EBarrierAccess::NO_ACCESS,
	};
	auto it = bufferStates.find(halfBarrier.buffer);
	if (it != bufferStates.end())
	{
		beforeState = it->second;
	}

	BufferBarrier fullBarrier = {
		.syncBefore   = beforeState.syncBefore,
		.syncAfter    = halfBarrier.syncAfter,
		.accessBefore = beforeState.accessBefore,
		.accessAfter  = halfBarrier.accessAfter,
		.buffer       = halfBarrier.buffer,
	};

	beforeState = BufferState{
		.syncBefore   = halfBarrier.syncAfter,
		.accessBefore = halfBarrier.accessAfter,
	};
	bufferStates.insert_or_assign(halfBarrier.buffer, beforeState);

	return fullBarrier;
}

TextureBarrier BarrierTracker::toTextureBarrier(const TextureBarrierAuto& halfBarrier)
{
	TextureState beforeState{
		.syncBefore   = EBarrierSync::NONE,
		.accessBefore = EBarrierAccess::NO_ACCESS,
		.layoutBefore = EBarrierLayout::Common,
		.subresources = halfBarrier.subresources, // #wip: Initial BarrierSubresourceRange?
		.flags        = halfBarrier.flags // #wip: Initial ETextureBarrierFlags?
	};
	auto it = textureStates.find(halfBarrier.texture);
	if (it != textureStates.end())
	{
		beforeState = it->second;
	}

	TextureBarrier fullBarrier = {
		.syncBefore   = beforeState.syncBefore,
		.syncAfter    = halfBarrier.syncAfter,
		.accessBefore = beforeState.accessBefore,
		.accessAfter  = halfBarrier.accessAfter,
		.layoutBefore = beforeState.layoutBefore,
		.layoutAfter  = halfBarrier.layoutAfter,
		.texture      = halfBarrier.texture,
		.subresources = halfBarrier.subresources,
		.flags        = halfBarrier.flags
	};

	// #wip: subresource range for auto barrier?
	CHECK(beforeState.subresources == halfBarrier.subresources);
	CHECK(beforeState.flags == halfBarrier.flags);

	beforeState = TextureState{
		.syncBefore   = halfBarrier.syncAfter,
		.accessBefore = halfBarrier.accessAfter,
		.layoutBefore = halfBarrier.layoutAfter,
		.subresources = halfBarrier.subresources,
		.flags        = halfBarrier.flags,
	};
	textureStates.insert_or_assign(halfBarrier.texture, beforeState);

	return fullBarrier;
}
