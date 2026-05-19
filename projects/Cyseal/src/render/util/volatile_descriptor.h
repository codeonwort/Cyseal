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
	void initialize(RenderDevice* inRenderDevice, const wchar_t* inPassName, uint32 historyCount, uint32 uniformTotalSize, uint32 uniformChunkCount = 1);

	// Manually reset internal smart pointers.
	// The destructor will reset them anyway, so use this function if you need to destroy manually at certain point.
	void destroy();

	// If uniformTotalSize is zero, then uniform buffer resources are not created.
	// It uses gRenderDevice and does not take RenderDevice parameter.
	void initialize(const wchar_t* inPassName, uint32 historyCount, uint32 uniformTotalSize, uint32 uniformChunkCount = 1);
	
	/// <summary>
	/// Recreate descriptor heap if current one is smaller than required.
	/// </summary>
	/// <param name="historyIndex"></param>
	/// <param name="maxDescriptors"></param>
	/// <returns>Current descriptor heap or recreated one.</returns>
	DescriptorHeap* resizeDescriptorHeap(uint32 historyIndex, uint32 maxDescriptors);
	
	inline DescriptorHeap* getDescriptorHeap(uint32 historyIndex) const { return descriptorHeap.at(historyIndex); }
	
	inline ConstantBufferView* getUniformCBV(uint32 historyIndex)
	{
		CHECK(uniformCBVs.bufferCount() > 0); // uniformTotalSize was 0 in initialize().
		CHECK(chunkCount == 1);
		return uniformCBVs.at(historyIndex, 0);
	}

	inline ConstantBufferView* getUniformChunkCBV(uint32 historyIndex, uint32 chunkIndex)
	{
		CHECK(uniformCBVs.bufferCount() > 0); // uniformTotalSize was 0 in initialize().
		CHECK(chunkCount > 1 && chunkIndex < chunkCount);
		return uniformCBVs.at(historyIndex, chunkIndex);
	}

	// Exposed for buffer barriers. Note that it's a single buffer containing multiple sections.
	inline Buffer* getUnifiedUniformBuffer() const { return uniformMemory.get(); }

public:
	/// Overloaded version that uses frameInfo.frameIndex. Be aware that you shall not use this in temporal passes.
	DescriptorHeap* resizeDescriptorHeap(const FrameInfo& frameInfo, uint32 maxDescriptors) { return resizeDescriptorHeap(frameInfo.frameIndex, maxDescriptors); }

	/// Overloaded version that uses frameInfo.frameIndex. Be aware that you shall not use this in temporal passes.
	inline DescriptorHeap* getDescriptorHeap(const FrameInfo& frameInfo) const { return descriptorHeap.at(frameInfo.frameIndex); }

	/// Overloaded version that uses frameInfo.frameIndex. Be aware that you shall not use this in temporal passes.
	inline ConstantBufferView* getUniformCBV(const FrameInfo& frameInfo) { return getUniformCBV(frameInfo.frameIndex); }

	/// Overloaded version that uses frameInfo.frameIndex. Be aware that you shall not use this in temporal passes.
	inline ConstantBufferView* getUniformChunkCBV(const FrameInfo& frameInfo, uint32 chunkIndex) { return getUniformChunkCBV(frameInfo.frameIndex, chunkIndex); }

private:
	RenderDevice* renderDevice = nullptr;
	std::wstring passName;
	
	std::vector<uint32> totalDescriptor;
	BufferedUniquePtr<DescriptorHeap> descriptorHeap;
	
	// Temp dedicated memory for uniforms
	UniquePtr<Buffer> uniformMemory;
	UniquePtr<DescriptorHeap> uniformDescriptorHeap;
	BufferedUniquePtrVec<ConstantBufferView> uniformCBVs;
	uint32 chunkCount;
};
