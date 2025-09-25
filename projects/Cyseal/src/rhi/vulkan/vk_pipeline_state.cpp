#include "vk_pipeline_state.h"
#include "vk_shader.h"

VulkanComputePipelineState::~VulkanComputePipelineState()
{
	vkDestroyPipeline(vkDevice, vkPipeline, nullptr);
	vkDestroyPipelineLayout(vkDevice, vkPipelineLayout, nullptr);
}

void VulkanComputePipelineState::initialize(VkDevice inVkDevice, const ComputePipelineDesc& inDesc)
{
	vkDevice = inVkDevice;

	VulkanShaderStage* shaderStage = static_cast<VulkanShaderStage*>(inDesc.cs);
	CHECK(shaderStage != nullptr);

	createPipelineLayout(inDesc);

	VkPipelineShaderStageCreateInfo shaderStageCreateInfo{
		.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext               = nullptr,
		.flags               = (VkPipelineShaderStageCreateFlagBits)0,
		.stage               = shaderStage->getVkShaderStage(),
		.module              = shaderStage->getVkShaderModule(),
		.pName               = shaderStage->getEntryPointA(),
		.pSpecializationInfo = nullptr, // #todo-vulkan: VkSpecializationInfo
	};

	VkComputePipelineCreateInfo pipelineCreateInfo{
		.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.pNext              = nullptr,
		.flags              = (VkPipelineCreateFlagBits)0, // #todo-vulkan: VkPipelineCreateFlagBits
		.stage              = shaderStageCreateInfo,
		.layout             = vkPipelineLayout,
		.basePipelineHandle = VK_NULL_HANDLE, // #todo-vulkan: basePipelineHandle
		.basePipelineIndex  = 0,
	};

	VkResult vkRet = vkCreateComputePipelines(
		vkDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &vkPipeline);
	CHECK(vkRet == VK_SUCCESS);
}

void VulkanComputePipelineState::createPipelineLayout(const ComputePipelineDesc& inDesc)
{
	VulkanShaderStage* computeShader = static_cast<VulkanShaderStage*>(inDesc.cs);
	const auto& setLayouts = computeShader->getVkDescriptorSetLayouts();
	const auto& pushConsts = computeShader->getVkPushConstantRanges();

	// #wip-vk: Create VkPipelineLayout
	VkPipelineLayoutCreateInfo createInfo{
		.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext                  = nullptr,
		.flags                  = (VkPipelineLayoutCreateFlags)0,
		.setLayoutCount         = (uint32_t)setLayouts.size(),
		.pSetLayouts            = setLayouts.data(),
		.pushConstantRangeCount = (uint32_t)pushConsts.size(),
		.pPushConstantRanges    = pushConsts.data(),
	};
	
	VkResult vkRet = vkCreatePipelineLayout(vkDevice, &createInfo, nullptr, &vkPipelineLayout);
	CHECK(vkRet == VK_SUCCESS);
}
