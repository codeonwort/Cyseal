#pragma once

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

// Tracks resource states for issueing barriers in a render command list.
// RenderCommandList implmentations use BarrierTacker internally.
// BarrierTracker itself only track and verify resource states. Actual barrier API is still called by render command list.
class BarrierTracker final
{
public:
	BarrierTracker();
	~BarrierTracker();

	void initialize(RenderCommandList* inCommandList);

	// Call after acquiring a command list and before recording any commands.
	void resetAll();

	// Convert half-auto barrier to full barrier.
	BufferBarrier toBufferBarrier(const BufferBarrierAuto& halfBarrier) const;
	// Convert half-auto barrier to full barrier.
	TextureBarrier toTextureBarrier(const TextureBarrierAuto& halfBarrier) const;

	// Verify full barrier and update internal state tracker.
	void applyBufferBarrier(const BufferBarrier& barrier);
	// Verify full barrier and update internal state tracker.
	void applyTextureBarrier(const TextureBarrier& barrier);

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
