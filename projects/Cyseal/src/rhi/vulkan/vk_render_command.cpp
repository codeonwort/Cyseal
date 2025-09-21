#include "vk_render_command.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_utils.h"
#include "vk_buffer.h"
#include "vk_into.h"

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

	// #todo-vulkan-critical: waitSemaphore in executeCommandList()
	// - It's possible that current command list is executing some one-time commands,
	//   not relevant to swapchain present. So I don't wanna wait for imageAvailable sem here...
	// - Why should I wait for swapchain image here at first? If I do offscreen rendering
	//   then am I ok to defer wait sem until the time to blit offscreen render target to backbuffer?
#if 1
	uint32 waitSemaphoreCount = 1;
	VkSemaphore waitSemaphores[] = { deviceWrapper->getVkSwapchainImageAvailableSemaphore() };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
#else
	uint32 waitSemaphoreCount = 0;
	VkSemaphore* waitSemaphores = nullptr;
	VkPipelineStageFlags* waitStages = nullptr;
#endif

	VkSemaphore signalSemaphores[] = { deviceWrapper->getVkRenderFinishedSemaphore() };

	VkSubmitInfo submitInfo{
		.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext                = nullptr,
		.waitSemaphoreCount   = waitSemaphoreCount,
		.pWaitSemaphores      = waitSemaphores,
		.pWaitDstStageMask    = waitStages,
		.commandBufferCount   = 1,
		.pCommandBuffers      = &(vkCmdList->currentCommandBuffer),
		.signalSemaphoreCount = 1,
		.pSignalSemaphores    = signalSemaphores,
	};

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
		VkCommandPoolCreateInfo poolInfo{
			.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext            = nullptr,
			.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = (uint32)queueFamilyIndices.graphicsFamily,
		};

		VkResult ret = vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &vkCommandPool);
		CHECK(ret == VK_SUCCESS);
	}
	{
		VkCommandBufferAllocateInfo allocInfo{
			.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.pNext              = nullptr,
			.commandPool        = vkCommandPool,
			.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		VkResult ret = vkAllocateCommandBuffers(vkDevice, &allocInfo, &vkCommandBuffer);
		CHECK(ret == VK_SUCCESS);
	}
}

void VulkanRenderCommandAllocator::onReset()
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

	VkCommandBufferBeginInfo beginInfo{
		.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext            = nullptr,
		.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
		.pInheritanceInfo = nullptr,
	};
	VkResult ret = vkBeginCommandBuffer(currentCommandBuffer, &beginInfo);
	CHECK(ret == VK_SUCCESS);
}

void VulkanRenderCommandList::close()
{
	VkResult ret = vkEndCommandBuffer(currentCommandBuffer);
	CHECK(ret == VK_SUCCESS);
}

void VulkanRenderCommandList::resourceBarriers(
	uint32 numBufferMemoryBarriers, const BufferMemoryBarrier* bufferMemoryBarriers,
	uint32 numTextureMemoryBarriers, const TextureMemoryBarrier* textureMemoryBarriers,
	uint32 numUAVBarriers, GPUResource* const* uavBarrierResources)
{
	// [Vulkanised 2021 - Ensure Correct Vulkan Synchronization by Using Synchornization Validation]
	// Barrier types
	// - A memory barrier synchronizes all memory accessible by the GPU.
	// - A buffer barrier synchronizes memory access to a buffer.
	// - A image barrier synchronizes memory access to an image and allow Image Layout Transitions.
	// Image Layout Transitions
	// - Rearrange memory for efficient use by different pipeline stages.
	// - Happens between the first and second execution scopes of the barrier.
	// - Each subresource of an image can be transitioned independently.

	// #todo-barrier: Use proper VkPipelineStageFlags
	// https://gpuopen.com/learn/vulkan-barriers-explained/
	// https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html
	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

	std::vector<VkBufferMemoryBarrier> vkBufferMemoryBarriers(numBufferMemoryBarriers);
	std::vector<VkImageMemoryBarrier> vkImageMemoryBarriers(numTextureMemoryBarriers);
	for (uint32 i = 0; i < numBufferMemoryBarriers; ++i)
	{
		vkBufferMemoryBarriers[i] = into_vk::bufferMemoryBarrier(bufferMemoryBarriers[i]);
	}
	for (uint32 i = 0; i < numTextureMemoryBarriers; ++i)
	{
		vkImageMemoryBarriers[i] = into_vk::imageMemoryBarrier(textureMemoryBarriers[i]);
	}
	// #todo-barrier: UAV barriers

	vkCmdPipelineBarrier(
		currentCommandBuffer, srcStageMask, dstStageMask,
		(VkDependencyFlagBits)0,
		0, nullptr,// #todo-barrier: Vulkan global memory barrier
		(uint32)vkBufferMemoryBarriers.size(), vkBufferMemoryBarriers.data(),
		(uint32)vkImageMemoryBarriers.size(), vkImageMemoryBarriers.data());
}

void VulkanRenderCommandList::barrier(
	uint32 numBufferBarriers, const BufferBarrier* bufferBarriers,
	uint32 numTextureBarriers, const TextureBarrier* textureBarriers,
	uint32 numGlobalBarriers, const GlobalBarrier* globalBarriers)
{
	// #wip
}

void VulkanRenderCommandList::clearRenderTargetView(RenderTargetView* RTV, const float* rgba)
{
	// #todo-vulkan
	//throw std::logic_error("The method or operation is not implemented.");
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::clearDepthStencilView(DepthStencilView* DSV, EDepthClearFlags clearFlags, float depth, uint8_t stencil)
{
	// #todo-vulkan
	//throw std::logic_error("The method or operation is not implemented.");
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::copyTexture2D(Texture* src, Texture* dst)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::setGraphicsPipelineState(GraphicsPipelineState* state)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::setComputePipelineState(ComputePipelineState* state)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::setRaytracingPipelineState(RaytracingPipelineStateObject* rtpso)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::setDescriptorHeaps(uint32 count, DescriptorHeap* const* heaps)
{
	// #todo-vulkan: What to do here?
	// Vulkan binds descriptor sets, not descriptor pools.
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::iaSetPrimitiveTopology(EPrimitiveTopology inTopology)
{
	// The PSO should be created with VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY
	// set in VkPipelineDynamicStateCreateInfo::pDynamicStates.
	VkPrimitiveTopology vkTopology = into_vk::primitiveTopology(inTopology);
	vkCmdSetPrimitiveTopology(currentCommandBuffer, vkTopology);
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
	VkIndexType vkIndexType = indexBuffer->getVkIndexType();

	vkCmdBindIndexBuffer(
		currentCommandBuffer,
		vkBuffer,
		indexBuffer->getBufferOffsetInBytes(),
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

void VulkanRenderCommandList::omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV)
{
	// #todo-vulkan
	//throw std::logic_error("The method or operation is not implemented.");
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::omSetRenderTargets(uint32 numRTVs, RenderTargetView* const* RTVs, DepthStencilView* DSV)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::bindGraphicsShaderParameters(PipelineState* pipelineState, const ShaderParameterTable* parameters, DescriptorHeap* descriptorHeap)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::updateGraphicsRootConstants(PipelineState* pipelineState, const ShaderParameterTable* parameters)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
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

void VulkanRenderCommandList::executeIndirect(
	CommandSignature* commandSignature,
	uint32 maxCommandCount,
	Buffer* argumentBuffer,
	uint64 argumentBufferOffset,
	Buffer* countBuffer /*= nullptr*/,
	uint64 countBufferOffset /*= 0*/)
{
	// #todo-vulkan
}

void VulkanRenderCommandList::bindComputeShaderParameters(
	PipelineState* pipelineState,
	const ShaderParameterTable* parameters,
	DescriptorHeap* descriptorHeap,
	DescriptorIndexTracker* tracker)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::dispatchCompute(uint32 threadGroupX, uint32 threadGroupY, uint32 threadGroupZ)
{
	// #todo-vulkan
	//vkCmdDispatch(currentCommandBuffer, threadGroupX, threadGroupY, threadGroupZ);
	CHECK_NO_ENTRY();
}

AccelerationStructure* VulkanRenderCommandList::buildRaytracingAccelerationStructure(uint32 numBLASDesc, BLASInstanceInitDesc* blasDescArray)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
	return nullptr;
}

void VulkanRenderCommandList::bindRaytracingShaderParameters(RaytracingPipelineStateObject* pipelineState, const ShaderParameterTable* parameters, DescriptorHeap* descriptorHeap, DescriptorHeap* samplerHeap)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
}

void VulkanRenderCommandList::dispatchRays(const DispatchRaysDesc& dispatchDesc)
{
	// #todo-vulkan
	CHECK_NO_ENTRY();
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
