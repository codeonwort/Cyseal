#include "vk_resource_view.h"

#if COMPILE_BACKEND_VULKAN

void VulkanConstantBufferView::writeToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes)
{
	// #wip-buffer: VulkanConstantBufferView::writeToGPU
	CHECK_NO_ENTRY();
}

#endif // COMPILE_BACKEND_VULKAN
