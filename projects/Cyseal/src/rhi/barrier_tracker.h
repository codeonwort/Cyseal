#pragma once

#include "gpu_resource_barrier.h"

#include <vector>
#include <map>

class Buffer;
class TextureKind;
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
	TextureKind* texture;
	BarrierSubresourceRange subresources;
	ETextureBarrierFlags flags;

	static TextureBarrierAuto toCopySource(
		TextureKind* texture,
		BarrierSubresourceRange subresources = BarrierSubresourceRange::allMips(),
		ETextureBarrierFlags flags = ETextureBarrierFlags::None)
	{
		return TextureBarrierAuto{
			EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, EBarrierLayout::CopySource,
			texture, subresources, flags
		};
	}
	static TextureBarrierAuto toCopyDest(
		TextureKind* texture,
		BarrierSubresourceRange subresources = BarrierSubresourceRange::allMips(),
		ETextureBarrierFlags flags = ETextureBarrierFlags::None)
	{
		return TextureBarrierAuto{
			EBarrierSync::COPY, EBarrierAccess::COPY_DEST, EBarrierLayout::CopyDest,
			texture, subresources, flags
		};
	}
	static TextureBarrierAuto toRenderTarget(
		TextureKind* texture,
		BarrierSubresourceRange subresources = BarrierSubresourceRange::allMips(),
		ETextureBarrierFlags flags = ETextureBarrierFlags::None)
	{
		return TextureBarrierAuto{
			EBarrierSync::RENDER_TARGET, EBarrierAccess::RENDER_TARGET, EBarrierLayout::RenderTarget,
			texture, subresources, flags
		};
	}
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

	// Let buffers and textures store their last barrier state.
	void flushFinalStates();

	// Convert half-auto barrier to full barrier.
	BufferBarrier toBufferBarrier(const BufferBarrierAuto& halfBarrier) const;
	// Convert half-auto barrier to full barrier.
	TextureBarrier toTextureBarrier(const TextureBarrierAuto& halfBarrier) const;

	// Verify full barrier and update internal state tracker.
	void applyBufferBarrier(const BufferBarrier& barrier);
	// Verify full barrier and update internal state tracker.
	void applyTextureBarrier(const TextureBarrier& barrier);

public:
	struct BufferState
	{
		EBarrierSync syncBefore;
		EBarrierAccess accessBefore;

		inline static BufferState createUnused()
		{
			return BufferState{ EBarrierSync::NONE, EBarrierAccess::NO_ACCESS };
		}
	};
	struct TextureState
	{
		EBarrierSync syncBefore;
		EBarrierAccess accessBefore;
		EBarrierLayout layoutBefore;
		BarrierSubresourceRange subresources;
		ETextureBarrierFlags flags;
	};
	struct TextureStateSet
	{
		bool bHolistic = true; // true if all subresources are in same state.
		TextureState globalState; // Used if bHolistic == true
		std::vector<TextureState> localStates; // Used if bHolistic == false

		inline static TextureStateSet createGlobalState(const TextureState& globalState)
		{
			return TextureStateSet{
				.bHolistic   = true,
				.globalState = globalState,
				.localStates = {}
			};
		}
		inline static TextureStateSet createUnused()
		{
			TextureState globalState{
					.syncBefore   = EBarrierSync::NONE,
					.accessBefore = EBarrierAccess::NO_ACCESS,
					.layoutBefore = EBarrierLayout::Common,
					.subresources = BarrierSubresourceRange::allMips(),
					.flags        = ETextureBarrierFlags::None,
			};
			return createGlobalState(globalState);
		}

		// Successful only if there is a local state with exactly same subresource range.
		bool replaceLocalState(const TextureBarrier& barrier);
		
		// Find local state whose subresource range contains the given range.
		const TextureState* localStateIncluding(const BarrierSubresourceRange& range) const;

		// Successful if there is a local state whose subresource range contains barrier's subresource range.
		bool splitLocalState(const TextureBarrier& barrier);

		// targetTexture: texture related to this TextureStateSet instance.
		void convertToHolisticIfPossible(TextureKind* targetTexture);

		static bool isSubRange(const TextureState& sub, const BarrierSubresourceRange& range);
	};

private:
	RenderCommandList* commandList = nullptr;
	std::map<Buffer*, BufferState> bufferStates;
	std::map<TextureKind*, TextureStateSet> textureStates;
};
