#include "barrier_tracker.h"
#include "buffer.h"
#include "texture_kind.h"

// ------------------------------------------------------------------
// BarrierTracker

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

void BarrierTracker::flushFinalStates()
{
	for (const auto& it : bufferStates)
	{
		it.first->internal_setLastBarrierState(it.second);
	}
	for (const auto& it : textureStates)
	{
		it.first->internal_setLastBarrierState(it.second);
	}
}

BufferBarrier BarrierTracker::toBufferBarrier(const BufferBarrierAuto& halfBarrier) const
{
	BufferState beforeState;

	auto it = bufferStates.find(halfBarrier.buffer);
	if (it != bufferStates.end())
	{
		beforeState = it->second;
	}
	else
	{
		beforeState = halfBarrier.buffer->internal_getLastBarrierState();
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
		const TextureStateSet& stateSet = it->second;
		if (halfBarrier.subresources.isHolistic())
		{
			CHECK(stateSet.bHolistic); // #wip-tracker-state
			beforeState = stateSet.globalState;
		}
		else
		{
			const TextureState* localState = stateSet.localStateIncluding(halfBarrier.subresources);
			if (localState != nullptr)
			{
				beforeState = *localState;
			}
			else
			{
				beforeState = stateSet.globalState;
			}
		}
		// applyTextureBarrier() will handle split or append for localStates.
	}
	else
	{
		const BarrierTracker::TextureStateSet& lastStateSet = halfBarrier.texture->internal_getLastBarrierState();
		if (lastStateSet.bHolistic)
		{
			beforeState = lastStateSet.globalState;
		}
		else
		{
			const BarrierTracker::TextureState* lastLocalState = lastStateSet.localStateIncluding(halfBarrier.subresources);
			beforeState = (lastLocalState != nullptr) ? (*lastLocalState) : lastStateSet.globalState;
		}
	}

	// #wip-tracker: What to do on ETextureBarrierFlags mismatch?
	CHECK(beforeState.flags == halfBarrier.flags);

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

	return fullBarrier;
}

void BarrierTracker::applyBufferBarrier(const BufferBarrier& barrier)
{
	auto it = bufferStates.find(barrier.buffer);
	if (it == bufferStates.end())
	{
		// #wip-tracker-state: Enable it and remove unnecessary flush
#if 0
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

void BarrierTracker::applyTextureBarrier(const TextureBarrier& barrier)
{
	auto it = textureStates.find(barrier.texture);
	if (it == textureStates.end())
	{
		// #wip-tracker-state: Enable it and remove unnecessary flush
#if 0
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
		TextureState initGlobalState{
			.syncBefore   = EBarrierSync::NONE,
			.accessBefore = EBarrierAccess::NO_ACCESS,
			.layoutBefore = EBarrierLayout::Undefined,
			.subresources = BarrierSubresourceRange::allMips(),
			.flags        = ETextureBarrierFlags::None,
		};
		textureStates.insert({ barrier.texture, TextureStateSet::createGlobalState(initGlobalState)});
	}

	it = textureStates.find(barrier.texture);
	CHECK(it != textureStates.end());
	TextureStateSet& stateSet = it->second;

	TextureState beforeState{
		.syncBefore   = barrier.syncAfter,
		.accessBefore = barrier.accessAfter,
		.layoutBefore = barrier.layoutAfter,
		.subresources = barrier.subresources,
		.flags        = barrier.flags,
	};

	if (stateSet.bHolistic)
	{
		if (barrier.subresources.isHolistic())
		{
			stateSet.globalState = beforeState;
		}
		else
		{
			CHECK(stateSet.localStates.size() == 0);
			stateSet.bHolistic = false;
			stateSet.localStates.emplace_back(beforeState);
		}
	}
	else
	{
		if (barrier.subresources.isHolistic())
		{
			stateSet = TextureStateSet::createGlobalState(beforeState);
		}
		else
		{
			if (!stateSet.replaceLocalState(barrier))
			{
				if (!stateSet.splitLocalState(barrier))
				{
					stateSet.localStates.emplace_back(beforeState);
				}
			}
		}
	}
}

// ------------------------------------------------------------------
// BarrierTracker::TextureStateSet

// Successful only if there is a local state with exactly same subresource range.
bool BarrierTracker::TextureStateSet::replaceLocalState(const TextureBarrier& barrier)
{
	CHECK(barrier.subresources.isHolistic() == false);
	for (TextureState& sub : localStates)
	{
		if (sub.subresources == barrier.subresources)
		{
			CHECK(sub.syncBefore == barrier.syncBefore);
			CHECK(sub.accessBefore == barrier.accessBefore);
			CHECK(sub.layoutBefore == barrier.layoutBefore);
			CHECK(sub.flags == barrier.flags);

			sub = TextureState{
				.syncBefore = barrier.syncAfter,
				.accessBefore = barrier.accessAfter,
				.layoutBefore = barrier.layoutAfter,
				.subresources = barrier.subresources,
				.flags = barrier.flags,
			};
			return true;
		}
	}
	return false;
}

const BarrierTracker::TextureState* BarrierTracker::TextureStateSet::localStateIncluding(const BarrierSubresourceRange& range) const
{
	for (const TextureState& localState : localStates)
	{
		if (isSubRange(localState, range))
		{
			return &localState;
		}
	}
	return nullptr;
}

// Successful if there is a local state whose subresource range contains barrier's subresource range.
bool BarrierTracker::TextureStateSet::splitLocalState(const TextureBarrier& barrier)
{
	bool append1 = false, append2 = false;
	TextureState newLocalState1, newLocalState2;

	for (TextureState& localState : localStates)
	{
		if (isSubRange(localState, barrier.subresources))
		{
			// #wip-tracker-state: All the combinations giving me headache
			CHECK(barrier.subresources.numMipLevels == 0);
			CHECK(barrier.subresources.numArraySlices == 0);
			CHECK(barrier.subresources.numPlanes == 0);
			// Exact match should be processed by replaceLocalState().
			CHECK(barrier.subresources.indexOrFirstMipLevel != localState.subresources.indexOrFirstMipLevel
				|| barrier.subresources.numMipLevels != localState.subresources.numMipLevels);

			if (localState.subresources.indexOrFirstMipLevel == barrier.subresources.indexOrFirstMipLevel)
			{
				newLocalState1 = localState;
				newLocalState1.subresources.indexOrFirstMipLevel += barrier.subresources.numMipLevels;
				newLocalState1.subresources.numMipLevels -= barrier.subresources.numMipLevels;

				localState = TextureState{
					.syncBefore   = barrier.syncAfter,
					.accessBefore = barrier.accessAfter,
					.layoutBefore = barrier.layoutAfter,
					.subresources = barrier.subresources,
					.flags = barrier.flags,
				};

				append1 = true;
				break;
			}
			else if (localState.subresources.indexOrFirstMipLevel + localState.subresources.numMipLevels
				== barrier.subresources.indexOrFirstMipLevel + barrier.subresources.numMipLevels)
			{
				newLocalState1 = localState;
				newLocalState1.subresources.numMipLevels -= barrier.subresources.numMipLevels;

				localState = TextureState{
					.syncBefore   = barrier.syncAfter,
					.accessBefore = barrier.accessAfter,
					.layoutBefore = barrier.layoutAfter,
					.subresources = barrier.subresources,
					.flags = barrier.flags,
				};

				append1 = true;
				break;
			}
			else
			{
				newLocalState1 = localState;
				newLocalState1.subresources.numMipLevels
					= barrier.subresources.indexOrFirstMipLevel - localState.subresources.indexOrFirstMipLevel;

				newLocalState2 = localState;
				newLocalState2.subresources.indexOrFirstMipLevel
					= barrier.subresources.indexOrFirstMipLevel + barrier.subresources.numMipLevels;
				newLocalState2.subresources.numMipLevels
					= localState.subresources.numMipLevels - newLocalState1.subresources.numMipLevels - barrier.subresources.numMipLevels;

				localState = TextureState{
					.syncBefore   = barrier.syncAfter,
					.accessBefore = barrier.accessAfter,
					.layoutBefore = barrier.layoutAfter,
					.subresources = barrier.subresources,
					.flags = barrier.flags,
				};

				append1 = append2 = true;
				break;
			}
		}
	}

	if (append1)
	{
		localStates.emplace_back(newLocalState1);
	}
	if (append2)
	{
		localStates.emplace_back(newLocalState2);
	}
	return append1 || append2;
}

bool BarrierTracker::TextureStateSet::isSubRange(const TextureState& sub, const BarrierSubresourceRange& range)
{
	bool match = sub.subresources == range;
	bool all = sub.subresources.indexOrFirstMipLevel == 0xffffffff;
	bool mip = (sub.subresources.numMipLevels != 0)
		&& (sub.subresources.indexOrFirstMipLevel <= range.indexOrFirstMipLevel)
		&& (range.indexOrFirstMipLevel + range.numMipLevels <= sub.subresources.indexOrFirstMipLevel + sub.subresources.numMipLevels);
	bool slice = (sub.subresources.numArraySlices != 0)
		&& (sub.subresources.firstArraySlice <= range.firstArraySlice)
		&& (range.firstArraySlice + range.numArraySlices <= sub.subresources.firstArraySlice + sub.subresources.numArraySlices);
	bool plane = (sub.subresources.numPlanes != 0)
		&& (sub.subresources.firstPlane <= range.firstPlane)
		&& (range.firstPlane + range.numPlanes <= sub.subresources.firstPlane + sub.subresources.numPlanes);
	return (match || all || mip || slice || plane);
}
