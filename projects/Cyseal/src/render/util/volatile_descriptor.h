#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"
#include "render/scene_render_pass.h"

// #todo-rhi: Messy detail that was supposed to be handled by RHI layer.
// Descriptors are usually allocated from multiple global descriptor heaps,
// but each render pass needs a single descriptor heap that contains all descriptors it needs.
// Let such a heap be 'volatile heap'. A render pass copies those descriptors from global heaps
// to the volatile heap and issue drawcalls or compute dispatches.
class VolatileDescriptorHelper
{
public:
	// If uniformTotalSize is zero, then uniform buffer resources are not created.
	void initialize(RenderDevice* inRenderDevice, const wchar_t* inPassName, uint32 maxFramesInFlight, uint32 uniformTotalSize, uint32 uniformChunkCount = 1);

	// Manually reset internal smart pointers.
	// The destructor will reset them anyway, so use this function if you need to destroy manually at certain point.
	void destroy();

	// If uniformTotalSize is zero, then uniform buffer resources are not created.
	// It uses gRenderDevice and does not take RenderDevice parameter.
	void initialize(const wchar_t* inPassName, uint32 maxFramesInFlight, uint32 uniformTotalSize, uint32 uniformChunkCount = 1);
	
	/// <summary>
	/// Recreate descriptor heap if current one is smaller than required.
	/// </summary>
	/// <param name="frameIndex"></param>
	/// <param name="maxDescriptors"></param>
	/// <returns>Current descriptor heap or recreated one.</returns>
	DescriptorHeap* resizeDescriptorHeap(uint32 frameIndex, uint32 maxDescriptors);

	/// <summary>
	/// Convenience method that takes frameInfo and uses frameInfo.frameIndex.
	/// #wip: Hmm I was so close to make a mistake that in a temporal render pass I just pass frameInfo instead of (frameInfo.frameID % 2).
	///       It's wrong because frameInfo.frameIndex could result in always zero.
	///       Should I just remove all overloaded functions that take FrameInfo to avoid any mistakes?
	/// </summary>
	/// <param name="frameInfo"></param>
	/// <param name="maxDescriptors"></param>
	/// <returns></returns>
	DescriptorHeap* resizeDescriptorHeap(const FrameInfo& frameInfo, uint32 maxDescriptors);
	
	inline DescriptorHeap* getDescriptorHeap(uint32 frameIndex) const { return descriptorHeap.at(frameIndex); }
	inline DescriptorHeap* getDescriptorHeap(const FrameInfo& frameInfo) const { return descriptorHeap.at(frameInfo.frameIndex); }
	
	inline ConstantBufferView* getUniformCBV(uint32 frameIndex)
	{
		CHECK(uniformCBVs.bufferCount() > 0); // uniformTotalSize was 0 in initialize().
		CHECK(chunkCount == 1);
		return uniformCBVs.at(frameIndex, 0);
	}
	inline ConstantBufferView* getUniformCBV(const FrameInfo& frameInfo)
	{
		return getUniformCBV(frameInfo.frameIndex);
	}

	inline ConstantBufferView* getUniformChunkCBV(uint32 frameIndex, uint32 chunkIndex)
	{
		CHECK(uniformCBVs.bufferCount() > 0); // uniformTotalSize was 0 in initialize().
		CHECK(chunkCount > 1 && chunkIndex < chunkCount);
		return uniformCBVs.at(frameIndex, chunkIndex);
	}
	inline ConstantBufferView* getUniformChunkCBV(const FrameInfo& frameInfo, uint32 chunkIndex)
	{
		return getUniformChunkCBV(frameInfo.frameIndex, chunkIndex);
	}

	// Exposed for buffer barriers. Note that it's a single buffer containing multiple sections.
	inline Buffer* getUnifiedUniformBuffer() const { return uniformMemory.get(); }

private:
	RenderDevice* renderDevice = nullptr;
	std::wstring passName;
	
	std::vector<uint32> totalDescriptor; // size = maxFramesInFlight
	BufferedUniquePtr<DescriptorHeap> descriptorHeap; // size = maxFramesInFlight
	
	// Temp dedicated memory for uniforms
	UniquePtr<Buffer> uniformMemory;
	UniquePtr<DescriptorHeap> uniformDescriptorHeap;
	BufferedUniquePtrVec<ConstantBufferView> uniformCBVs; // size = maxFramesInFlight
	uint32 chunkCount;
};
