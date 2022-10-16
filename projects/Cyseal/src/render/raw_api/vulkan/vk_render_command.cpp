#include "vk_render_command.h"
#include "vk_utils.h"

//////////////////////////////////////////////////////////////////////////
// VulkanRenderCommandQueue

void VulkanRenderCommandQueue::initialize(RenderDevice* renderDevice)
{
	deviceWrapper = static_cast<VulkanDevice*>(renderDevice);
	vkGraphicsQueue = deviceWrapper->getVkGraphicsQueue();
}

void VulkanRenderCommandQueue::executeCommandList(RenderCommandList* commandList)
{
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VulkanRenderCommandList* vkCmdList = static_cast<VulkanRenderCommandList*>(commandList);

	VkSemaphore waitSemaphores[] = { deviceWrapper->getVkImageAvailableSemoaphre() };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &(vkCmdList->currentCommandBuffer);

	VkSemaphore signalSemaphores[] = { deviceWrapper->getVkRenderFinishedSemoaphre() };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	VkResult ret = vkQueueSubmit(vkGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	CHECK(ret == VK_SUCCESS);
}

//////////////////////////////////////////////////////////////////////////
// VulkanRenderCommandAllocator

void VulkanRenderCommandAllocator::initialize(RenderDevice* renderDevice)
{
	VulkanDevice* rawDevice = static_cast<VulkanDevice*>(renderDevice);
	vkDevice = rawDevice->getRaw();
	auto vkPhysicalDevice = rawDevice->getVkPhysicalDevice();
	auto vkSurfaceKHR = rawDevice->getVkSurface();

	{
		QueueFamilyIndices queueFamilyIndices = findQueueFamilies(vkPhysicalDevice, vkSurfaceKHR);
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
		poolInfo.flags = 0;

		VkResult ret = vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &vkCommandPool);
		CHECK(ret == VK_SUCCESS);
	}
	{
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = vkCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		VkResult ret = vkAllocateCommandBuffers(vkDevice, &allocInfo, &vkCommandBuffer);
		CHECK(ret == VK_SUCCESS);
	}
}

void VulkanRenderCommandAllocator::reset()
{
	vkResetCommandBuffer(vkCommandBuffer, 0);
	//vkResetCommandPool(vkDevice, vkCommandPool, 0);
	//throw std::logic_error("The method or operation is not implemented.");
}

//////////////////////////////////////////////////////////////////////////
// VulkanRenderCommandList

void VulkanRenderCommandList::initialize(RenderDevice* renderDevice)
{
	//
}

void VulkanRenderCommandList::reset(RenderCommandAllocator* allocator)
{
	VulkanRenderCommandAllocator* vkAllocator = static_cast<VulkanRenderCommandAllocator*>(allocator);
	currentCommandBuffer = vkAllocator->getRawCommandBuffer();

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	beginInfo.pInheritanceInfo = nullptr;
	VkResult ret = vkBeginCommandBuffer(currentCommandBuffer, &beginInfo);
	CHECK(ret == VK_SUCCESS);
}

void VulkanRenderCommandList::close()
{
	VkResult ret = vkEndCommandBuffer(currentCommandBuffer);
	CHECK(ret == VK_SUCCESS);
}
