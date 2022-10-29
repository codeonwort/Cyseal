#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "render/pipeline_state.h"
#include <vulkan/vulkan_core.h>

class VulkanGraphicsPipelineState : public PipelineState
{
public:
	VulkanGraphicsPipelineState(
		VkPipeline inVkPipeline,
		VkPipelineLayout inVkPipelineLayout,
		VkRenderPass inVkRenderPass)
		: vkPipeline(inVkPipeline)
		, vkPipelineLayout(inVkPipelineLayout)
		, vkRenderPass(inVkRenderPass)
	{}
	~VulkanGraphicsPipelineState()
	{
		VkDevice vkDevice = static_cast<VulkanDevice*>(gRenderDevice)->getRaw();
		vkDestroyPipelineLayout(vkDevice, vkPipelineLayout, nullptr);
		vkDestroyPipeline(vkDevice, vkPipeline, nullptr);
		vkDestroyRenderPass(vkDevice, vkRenderPass, nullptr);
	}
private:
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
	VkRenderPass vkRenderPass = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
