#include "vk_render_command.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_utils.h"
#include "vk_buffer.h"

namespace into_vk
{
	VkViewport viewport(const Viewport& inViewport)
	{
		VkViewport vkViewport{};
		vkViewport.x = inViewport.topLeftX;
		vkViewport.y = inViewport.topLeftY;
		vkViewport.width = inViewport.width;
		vkViewport.height = inViewport.height;
		vkViewport.minDepth = inViewport.minDepth;
		vkViewport.maxDepth = inViewport.maxDepth;
		return vkViewport;
	}

	VkRect2D scissorRect(const ScissorRect& scissorRect)
	{
		VkRect2D vkScissor{};
		vkScissor.extent.width = scissorRect.right - scissorRect.left;
		vkScissor.extent.height = scissorRect.bottom - scissorRect.top;
		vkScissor.offset.x = scissorRect.left;
		vkScissor.offset.y = scissorRect.top;
		return vkScissor;
	}
}

//////////////////////////////////////////////////////////////////////////
// VulkanRenderCommandQueue

void VulkanRenderCommandQueue::initialize(RenderDevice* renderDevice)
{
	deviceWrapper = static_cast<VulkanDevice*>(renderDevice);
	vkGraphicsQueue = deviceWrapper->getVkGraphicsQueue();
}

void VulkanRenderCommandQueue::executeCommandList(RenderCommandList* commandList)
{
	VulkanRenderCommandList* vkCmdList = static_cast<VulkanRenderCommandList*>(commandList);

	VkSemaphore waitSemaphores[] = { deviceWrapper->getVkImageAvailableSemaphore() };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSemaphore signalSemaphores[] = { deviceWrapper->getVkRenderFinishedSemaphore() };

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	// #todo-vulkan: Semaphore
	// It's possible that current command list is executing some one-time commands,
	// not relevant to swapchain present. So I don't wanna wait for image available sem here...
#if 0
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
#else
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = nullptr;
#endif
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &(vkCmdList->currentCommandBuffer);
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

void VulkanRenderCommandList::iaSetVertexBuffers(
	int32 startSlot,
	uint32 numViews,
	VertexBuffer* const* vertexBuffers)
{
	std::vector<VkBuffer> vkBuffers(numViews, VK_NULL_HANDLE);
	std::vector<VkDeviceSize> vkOffsets(numViews, 0); // #todo-vulkan: Vertex buffer offsets?
	for (uint32 i = 0; i < numViews; ++i)
	{
		vkBuffers[i] = static_cast<VulkanVertexBuffer*>(vertexBuffers[i])->getVkBuffer();
	}
	vkCmdBindVertexBuffers(
		currentCommandBuffer,
		(uint32)startSlot,
		numViews,
		vkBuffers.data(),
		vkOffsets.data());
}

void VulkanRenderCommandList::iaSetIndexBuffer(IndexBuffer* inIndexBuffer)
{
	VulkanIndexBuffer* indexBuffer = static_cast<VulkanIndexBuffer*>(inIndexBuffer);
	VkBuffer vkBuffer = indexBuffer->getVkBuffer();
	VkIndexType vkIndexType = indexBuffer->getIndexType();

	vkCmdBindIndexBuffer(
		currentCommandBuffer,
		vkBuffer,
		0, // #todo-vulkan: Index buffer offset
		vkIndexType);
}

void VulkanRenderCommandList::rsSetViewport(const Viewport& inViewport)
{
	VkViewport vkViewport = into_vk::viewport(inViewport);
	vkCmdSetViewport(currentCommandBuffer, 0, 1, &vkViewport);
}

void VulkanRenderCommandList::rsSetScissorRect(const ScissorRect& scissorRect)
{
	VkRect2D vkScissor = into_vk::scissorRect(scissorRect);
	vkCmdSetScissor(currentCommandBuffer, 0, 1, &vkScissor);
}

void VulkanRenderCommandList::drawIndexedInstanced(
	uint32 indexCountPerInstance,
	uint32 instanceCount,
	uint32 startIndexLocation,
	int32 baseVertexLocation,
	uint32 startInstanceLocation)
{
	vkCmdDrawIndexed(
		currentCommandBuffer,
		indexCountPerInstance,
		instanceCount,
		startIndexLocation,
		baseVertexLocation,
		startInstanceLocation);
}

void VulkanRenderCommandList::drawInstanced(
	uint32 vertexCountPerInstance,
	uint32 instanceCount,
	uint32 startVertexLocation,
	uint32 startInstanceLocation)
{
	vkCmdDraw(
		currentCommandBuffer,
		vertexCountPerInstance,
		instanceCount,
		startVertexLocation,
		startInstanceLocation);
}

void VulkanRenderCommandList::beginEventMarker(const char* eventName)
{
	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	deviceWrapper->beginVkDebugMarker(currentCommandBuffer, eventName);
}

void VulkanRenderCommandList::endEventMarker()
{
	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	deviceWrapper->endVkDebugMarker(currentCommandBuffer);
}

#endif
