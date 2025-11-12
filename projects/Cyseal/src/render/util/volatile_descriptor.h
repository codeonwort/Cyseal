#pragma once

#include "core/int_types.h"
#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/gpu_resource_view.h"

// #todo-rhi: Messy detail that was supposed to be handled by RHI layer.
// Descriptors are usually allocated from multiple global descriptor heaps,
// but each render pass needs a single descriptor heap that contains all descriptors it needs.
// Let such a heap be 'volatile heap'. A render pass copies those descriptors from global heaps
// to the volatile heap and issue drawcalls or compute dispatchs.
class VolatileDescriptorHelper
{
public:
	// If uniformTotalSize is zero, then uniform buffer resources are not created.
	void initialize(RenderDevice* inRenderDevice, const wchar_t* inPassName, uint32 swapchainCount, uint32 uniformTotalSize);

	// Manually reset internal smart pointers.
	// The destructor will reset them anyway, so use this function if you need to destroy manually at certain point.
	void destroy();

	// If uniformTotalSize is zero, then uniform buffer resources are not created.
	// It uses gRenderDevice and does not take RenderDevice parameter.
	void initialize(const wchar_t* inPassName, uint32 swapchainCount, uint32 uniformTotalSize);
	
	void resizeDescriptorHeap(uint32 swapchainIndex, uint32 maxDescriptors);
	
	inline DescriptorHeap* getDescriptorHeap(uint32 swapchainIndex) const { return descriptorHeap.at(swapchainIndex); }
	
	inline ConstantBufferView* getUniformCBV(uint32 swapchainIndex) const
	{
		CHECK(uniformCBVs.size() > 0); // uniformTotalSize was 0 in initialize().
		return uniformCBVs.at(swapchainIndex);
	}

private:
	RenderDevice* renderDevice = nullptr;
	std::wstring passName;
	
	std::vector<uint32> totalDescriptor; // size = swapchain count
	BufferedUniquePtr<DescriptorHeap> descriptorHeap; // size = swapchain count
	
	// Temp dedicated memory for uniforms
	UniquePtr<Buffer> uniformMemory;
	UniquePtr<DescriptorHeap> uniformDescriptorHeap;
	BufferedUniquePtr<ConstantBufferView> uniformCBVs; // size = swapchain count
};
