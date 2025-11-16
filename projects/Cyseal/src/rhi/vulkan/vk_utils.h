#pragma once

#if COMPILE_BACKEND_VULKAN

#include "core/assertion.h"
#include "core/int_types.h"
#include <vector>

#include <Volk/volk.h>

struct QueueFamilyIndices
{
	int graphicsFamily = -1;
	int presentFamily = -1;
	bool isComplete()
	{
		return graphicsFamily >= 0 && presentFamily >= 0;
	}
};

struct SwapChainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physDevice, VkSurfaceKHR surfaceKHR);

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

VkFormat findSupportedFormat(
	VkPhysicalDevice physDevice,
	const std::vector<VkFormat>& candidates,
	VkImageTiling tiling,
	VkFormatFeatureFlags features);

VkFormat findDepthFormat(VkPhysicalDevice physDevice);

uint32 findMemoryType(
	VkPhysicalDevice physicalDevice,
	uint32 typeFilter,
	VkMemoryPropertyFlags properties);

void createImage(
	VkPhysicalDevice physDevice,
	VkDevice device,
	uint32 width,
	uint32 height,
	VkFormat format,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	VkMemoryPropertyFlags properties,
	VkImage& image,
	VkDeviceMemory& imageMemory);

inline bool hasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT
		|| format == VK_FORMAT_D24_UNORM_S8_UINT;
}

inline VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool)
{
	VkCommandBufferAllocateInfo allocInfo{
		.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext              = nullptr,
		.commandPool        = commandPool,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBufferBeginInfo beginInfo{
		.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext            = nullptr,
		.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};
	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
	vkBeginCommandBuffer(commandBuffer, &beginInfo);
	return commandBuffer;
}

inline void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer)
{
	VkSubmitInfo submitInfo{
		.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext                = nullptr,
		.waitSemaphoreCount   = 0,
		.pWaitDstStageMask    = nullptr,
		.commandBufferCount   = 1,
		.pCommandBuffers      = &commandBuffer,
		.signalSemaphoreCount = 0,
		.pSignalSemaphores    = nullptr,
	};
	vkEndCommandBuffer(commandBuffer);
	vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);
	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

// #todo-vulkan: Delete this weirdo
void findImageBarrierFlags(
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkFormat format,
	VkPipelineStageFlags* outSrcStageMask,
	VkPipelineStageFlags* outDstStageMask,
	VkAccessFlags* outSrcAccessMask,
	VkAccessFlags* outDstAccessMask,
	VkImageAspectFlags* outAspectMask);

// #todo-vulkan: Delete this weirdo
void transitionImageLayout(
	VkDevice device,
	VkCommandPool commandPool,
	VkQueue graphicsQueue,
	VkImage image,
	VkFormat format,
	VkImageLayout oldLayout,
	VkImageLayout newLayout);

#endif // COMPILE_BACKEND_VULKAN
