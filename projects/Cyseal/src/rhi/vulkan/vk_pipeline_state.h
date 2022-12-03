#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "rhi/pipeline_state.h"
#include <vulkan/vulkan_core.h>

class VulkanPipelineLayout : public RootSignature
{
public:
	VulkanPipelineLayout(VkPipelineLayout inLayout)
		: vkPipelineLayout(inLayout)
	{}
	~VulkanPipelineLayout()
	{
		VkDevice vkDevice = static_cast<VulkanDevice*>(gRenderDevice)->getRaw();
		vkDestroyPipelineLayout(vkDevice, vkPipelineLayout, nullptr);
	}
	VkPipelineLayout getVkPipelineLayout() const { return vkPipelineLayout; }
private:
	VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
};

class VulkanGraphicsPipelineState : public PipelineState
{
public:
	VulkanGraphicsPipelineState(VkPipeline inVkPipeline, VkRenderPass inVkRenderPass)
		: vkPipeline(inVkPipeline)
		, vkRenderPass(inVkRenderPass)
	{}
	~VulkanGraphicsPipelineState()
	{
		VkDevice vkDevice = static_cast<VulkanDevice*>(gRenderDevice)->getRaw();
		vkDestroyPipeline(vkDevice, vkPipeline, nullptr);
		vkDestroyRenderPass(vkDevice, vkRenderPass, nullptr);
	}
private:
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	VkRenderPass vkRenderPass = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
