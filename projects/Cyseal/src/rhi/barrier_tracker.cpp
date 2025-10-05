#include "barrier_tracker.h"

BarrierTracker::BarrierTracker()
{
}

BarrierTracker::~BarrierTracker()
{
}

void BarrierTracker::initialize(RenderCommandList* inCommandList)
{
	commandList = inCommandList;
}

void BarrierTracker::resetAll()
{
	bufferStates.clear();
	textureStates.clear();
}

BufferBarrier BarrierTracker::toBufferBarrier(const BufferBarrierAuto& halfBarrier) const
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

	return fullBarrier;
}

TextureBarrier BarrierTracker::toTextureBarrier(const TextureBarrierAuto& halfBarrier) const
{
	TextureState beforeState{
		.syncBefore   = EBarrierSync::NONE,
		.accessBefore = EBarrierAccess::NO_ACCESS,
		.layoutBefore = EBarrierLayout::Common, // #wip-tracker: Initial layoutBefore? undefined or common?
		.subresources = halfBarrier.subresources, // #wip-tracker: Initial BarrierSubresourceRange?
		.flags        = halfBarrier.flags // #wip-tracker: Initial ETextureBarrierFlags?
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

	// #wip-tracker: subresource range for auto barrier?
	CHECK(beforeState.subresources == halfBarrier.subresources);
	CHECK(beforeState.flags == halfBarrier.flags);

	return fullBarrier;
}

void BarrierTracker::applyBufferBarrier(const BufferBarrier& barrier)
{
	auto it = bufferStates.find(barrier.buffer);
	if (it == bufferStates.end())
	{
		// #wip-tracker-state: Enable it and remove unnecessary flush
#if 1
		if (barrier.syncBefore != EBarrierSync::NONE)
		{
			CHECK_NO_ENTRY(); // You don't need syncBefore.
		}
		if (barrier.accessBefore != EBarrierAccess::NO_ACCESS)
		{
			CHECK_NO_ENTRY(); // You don't need accessBefore.
		}
#endif
	}
	else
	{
		const BufferState& beforeState = it->second;
		CHECK(beforeState.syncBefore == barrier.syncBefore);
		CHECK(beforeState.accessBefore == barrier.accessBefore);
	}

	BufferState beforeState = BufferState{
		.syncBefore   = barrier.syncAfter,
		.accessBefore = barrier.accessAfter,
	};
	bufferStates.insert_or_assign(barrier.buffer, beforeState);
}

void BarrierTracker::applyTextureBarrier(const TextureBarrier & barrier)
{
	auto it = textureStates.find(barrier.texture);
	if (it == textureStates.end())
	{
		// #wip-tracker-state: Enable it and remove unnecessary flush
#if 1
		if (barrier.syncBefore != EBarrierSync::NONE)
		{
			CHECK_NO_ENTRY(); // You don't need syncBefore.
		}
		if (barrier.accessBefore != EBarrierAccess::NO_ACCESS)
		{
			CHECK_NO_ENTRY(); // You don't need accessBefore.
		}
		if (barrier.layoutBefore != EBarrierLayout::Undefined && barrier.layoutBefore != EBarrierLayout::Common)
		{
			CHECK_NO_ENTRY();
		}
#endif
	}
	else
	{
		const TextureState& beforeState = it->second;
		CHECK(beforeState.syncBefore == barrier.syncBefore);
		CHECK(beforeState.accessBefore == barrier.accessBefore);
		CHECK(beforeState.layoutBefore == barrier.layoutBefore);

		// #wip-tracker: Verify subresource range?
		CHECK(beforeState.subresources == barrier.subresources);
		CHECK(beforeState.flags == barrier.flags);
	}

	TextureState beforeState = TextureState{
		.syncBefore   = barrier.syncAfter,
		.accessBefore = barrier.accessAfter,
		.layoutBefore = barrier.layoutAfter,
		.subresources = barrier.subresources,
		.flags        = barrier.flags,
	};
	textureStates.insert_or_assign(barrier.texture, beforeState);
}
