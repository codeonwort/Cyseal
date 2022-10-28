#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "render/pipeline_state.h"
#include <vulkan/vulkan_core.h>

class VulkanGraphicsPipelineState : public PipelineState
{
public:
	VulkanGraphicsPipelineState(VkPipeline inVkPipeline)
		: vkPipeline(inVkPipeline)
	{}
	~VulkanGraphicsPipelineState()
	{
		VkDevice vkDevice = static_cast<VulkanDevice*>(gRenderDevice)->getRaw();
		vkDestroyPipeline(vkDevice, vkPipeline, nullptr);
	}
private:
	VkPipeline vkPipeline = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
