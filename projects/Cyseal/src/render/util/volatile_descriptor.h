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
	void initialize(const wchar_t* inPassName, uint32 swapchainCount, uint32 uniformTotalSize);
	
	void resizeDescriptorHeap(uint32 swapchainIndex, uint32 maxDescriptors);
	
	inline DescriptorHeap* getDescriptorHeap(uint32 swapchainIndex) const { return descriptorHeap.at(swapchainIndex); }
	
	inline ConstantBufferView* getUniformCBV(uint32 swapchainIndex) const { return uniformCBVs.at(swapchainIndex); }

private:
	std::wstring passName;
	
	std::vector<uint32> totalDescriptor; // size = swapchain count
	BufferedUniquePtr<DescriptorHeap> descriptorHeap; // size = swapchain count
	
	// Temp dedicated memory for uniforms
	UniquePtr<Buffer> uniformMemory;
	UniquePtr<DescriptorHeap> uniformDescriptorHeap;
	BufferedUniquePtr<ConstantBufferView> uniformCBVs; // size = swapchain count
};
