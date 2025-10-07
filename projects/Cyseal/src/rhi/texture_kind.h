#pragma once

// Base class for texture and swapchain image.

#include "gpu_resource.h"
#include "barrier_tracker.h"

class TextureKind : public GPUResource
{
public:
	TextureKind()
	{
		lastBarrier = BarrierTracker::TextureStateSet::createUnused();
	}

	// Use only when a barrier tracker in a command list has no history for this texture.
	inline const BarrierTracker::TextureStateSet& internal_getLastBarrierState() const { return lastBarrier; }
	// Use only when a command list is closed.
	inline void internal_setLastBarrierState(const BarrierTracker::TextureStateSet& newState) { lastBarrier = newState; }

private:
	// This is used only for two cases:
	//   1. Before beginning recording of a command list.
	//   2. After finishing recording of a command list.
	// Intermediate states are tracked by that command list.
	BarrierTracker::TextureStateSet lastBarrier;
};
