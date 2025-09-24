#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "rhi/pipeline_state.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

class VulkanGraphicsPipelineState : public GraphicsPipelineState
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
	inline VkPipeline getVkPipeline() const { return vkPipeline; }
private:
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	VkRenderPass vkRenderPass = VK_NULL_HANDLE;
};

class VulkanComputePipelineState : public ComputePipelineState
{
public:
	~VulkanComputePipelineState();

	void initialize(VkDevice inVkDevice, const ComputePipelineDesc& inDesc);

	inline VkPipeline getVkPipeline() const { return vkPipeline; }

private:
	void createPipelineLayout(const ComputePipelineDesc& inDesc);

private:
	VkDevice vkDevice = VK_NULL_HANDLE;
	VkPipeline vkPipeline = VK_NULL_HANDLE;
	VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
