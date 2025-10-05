#pragma once

// Tracks resource states for issueing barriers in a render command list.
// RenderCommandList uses BarrierTacker.

#include "gpu_resource_barrier.h"

#include <map>

class GPUResource;
class Buffer;
class Texture;
class RenderCommandList;

// BufferBarrier without 'before' states.
struct BufferBarrierAuto
{
	EBarrierSync syncAfter;
	EBarrierAccess accessAfter;
	Buffer* buffer;
};

// TextureBarrier without 'before' states.
struct TextureBarrierAuto
{
	EBarrierSync syncAfter;
	EBarrierAccess accessAfter;
	EBarrierLayout layoutAfter;
	GPUResource* texture; // Must be a texture. Can't be (Texture*) due to swapchain images.
	BarrierSubresourceRange subresources;
	ETextureBarrierFlags flags;
};

// #wip: Should full-manual ver of RenderCommandList::barrier() save final state to this tracker?
// yes, because auto barrier after manual barrier need to know before state.
class BarrierTracker
{
public:
	BarrierTracker(RenderCommandList* inCommandList);
	~BarrierTracker();

	// Call after acquiring a command list and before recording any commands.
	void resetAll();

	BufferBarrier toBufferBarrier(const BufferBarrierAuto& halfBarrier);
	TextureBarrier toTextureBarrier(const TextureBarrierAuto& halfBarrier);

private:
	struct BufferState
	{
		EBarrierSync syncBefore;
		EBarrierAccess accessBefore;
	};
	struct TextureState
	{
		EBarrierSync syncBefore;
		EBarrierAccess accessBefore;
		EBarrierLayout layoutBefore;
		BarrierSubresourceRange subresources;
		ETextureBarrierFlags flags;
	};

	RenderCommandList* commandList = nullptr;
	std::map<Buffer*, BufferState> bufferStates;
	std::map<GPUResource*, TextureState> textureStates;
};
