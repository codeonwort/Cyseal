#include "vk_render_command.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_utils.h"
#include "vk_buffer.h"
#include "vk_pipeline_state.h"
#include "vk_descriptor.h"
#include "vk_into.h"

DEFINE_LOG_CATEGORY_STATIC(LogVulkanCommandList);

static void reportUndeclaredShaderParameter(const char* name)
{
	// #todo-log: How to stop error spam? Track same errors and report only once?
	//CYLOG(LogVulkanCommandList, Error, L"Undeclared parameter: %S", name);
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

	// #todo-vulkan-critical: waitSemaphore in executeCommandList()
	// - It's possible that current command list is executing some one-time commands,
	//   not relevant to swapchain present. So I don't wanna wait for imageAvailable sem here...
	// - Why should I wait for swapchain image here at first? If I do offscreen rendering
	//   then am I ok to defer wait sem until the time to blit offscreen render target to backbuffer?
	// - [2025-11-12] OK this code is ancient old... now I clearly see it that
	//   even if a swapchain-available semaphore is required, it should never be in a command queue.
	//   Let's just disable it for the sake of barrier unit tests.
	//   Revisit when doing actual present using Vulkan backend.
#if 0
	uint32 waitSemaphoreCount = 1;
	VkSemaphore waitSemaphores[] = { deviceWrapper->getVkSwapchainImageAvailableSemaphore() };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
#else
	uint32 waitSemaphoreCount = 0;
	VkSemaphore* waitSemaphores = nullptr;
	VkPipelineStageFlags* waitStages = nullptr;
#endif

	VkSemaphore signalSemaphores[] = { deviceWrapper->getVkRenderFinishedSemaphore() };
	VkCommandBuffer vkCommandBuffer = vkCmdList->internal_getVkCommandBuffer();

	VkSubmitInfo submitInfo{
		.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext                = nullptr,
		.waitSemaphoreCount   = waitSemaphoreCount,
		.pWaitSemaphores      = waitSemaphores,
		.pWaitDstStageMask    = waitStages,
		.commandBufferCount   = 1,
		.pCommandBuffers      = &vkCommandBuffer,
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
	device = static_cast<VulkanDevice*>(renderDevice);
	barrierTracker.initialize(this);
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

	barrierTracker.resetAll();
}

void VulkanRenderCommandList::close()
{
	VkResult ret = vkEndCommandBuffer(currentCommandBuffer);
	CHECK(ret == VK_SUCCESS);

	barrierTracker.flushFinalStates();
}

void VulkanRenderCommandList::barrier(
	uint32 numBufferBarriers, const BufferBarrier* bufferBarriers,
	uint32 numTextureBarriers, const TextureBarrier* textureBarriers,
	uint32 numGlobalBarriers, const GlobalBarrier* globalBarriers)
{
	std::vector<VkBufferMemoryBarrier2> vkBufferBarriers(numBufferBarriers);
	std::vector<VkImageMemoryBarrier2> vkImageBarriers(numTextureBarriers);
	std::vector<VkMemoryBarrier2> vkGlobalBarriers(numGlobalBarriers);
	for (uint32 i = 0; i < numBufferBarriers; ++i)
	{
		vkBufferBarriers[i] = into_vk::bufferMemoryBarrier(bufferBarriers[i]);
	}
	for (uint32 i = 0; i < numTextureBarriers; ++i)
	{
		vkImageBarriers[i] = into_vk::imageMemoryBarrier(textureBarriers[i]);
	}
	for (uint32 i = 0; i < numGlobalBarriers; ++i)
	{
		vkGlobalBarriers[i] = into_vk::globalMemoryBarrier(globalBarriers[i]);
	}

	VkDependencyInfo dep{
		.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.pNext                    = nullptr,
		.dependencyFlags          = (VkDependencyFlags)0,
		.memoryBarrierCount       = numGlobalBarriers,
		.pMemoryBarriers          = vkGlobalBarriers.data(),
		.bufferMemoryBarrierCount = numBufferBarriers,
		.pBufferMemoryBarriers    = vkBufferBarriers.data(),
		.imageMemoryBarrierCount  = numTextureBarriers,
		.pImageMemoryBarriers     = vkImageBarriers.data(),
	};
	vkCmdPipelineBarrier2(currentCommandBuffer, &dep);

	// Update tracker state.
	for (size_t i = 0; i < numBufferBarriers; ++i)
	{
		barrierTracker.applyBufferBarrier(bufferBarriers[i]);
	}
	for (size_t i = 0; i < numTextureBarriers; ++i)
	{
		barrierTracker.applyTextureBarrier(textureBarriers[i]);
	}
}

void VulkanRenderCommandList::barrierAuto(
	uint32 numBufferBarriers, const BufferBarrierAuto* bufferBarriers,
	uint32 numTextureBarriers, const TextureBarrierAuto* textureBarriers,
	uint32 numGlobalBarriers, const GlobalBarrier* globalBarriers)
{
	std::vector<BufferBarrier> fullBufferBarriers;
	std::vector<TextureBarrier> fullTextureBarriers;
	fullBufferBarriers.reserve(numBufferBarriers);
	fullTextureBarriers.reserve(numTextureBarriers);
	for (uint32 i = 0; i < numBufferBarriers; ++i)
	{
		BufferBarrier fullBarrier = barrierTracker.toBufferBarrier(bufferBarriers[i]);
		fullBufferBarriers.emplace_back(fullBarrier);
	}
	for (uint32 i = 0; i < numTextureBarriers; ++i)
	{
		TextureBarrier fullBarrier = barrierTracker.toTextureBarrier(textureBarriers[i]);
		fullTextureBarriers.emplace_back(fullBarrier);
	}

	this->barrier(
		numBufferBarriers, fullBufferBarriers.data(),
		numTextureBarriers, fullTextureBarriers.data(),
		numGlobalBarriers, globalBarriers);
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
	auto vulkanPipeline = static_cast<VulkanComputePipelineState*>(state);
	VkPipeline vkPipeline = vulkanPipeline->getVkPipeline();
	vkCmdBindPipeline(currentCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline);
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
	const ShaderParameterTable* inParameters,
	DescriptorHeap* inDescriptorHeap,
	DescriptorIndexTracker* tracker)
{
	VulkanComputePipelineState* computePSO = static_cast<VulkanComputePipelineState*>(pipelineState);
	VulkanDescriptorPool* pool = static_cast<VulkanDescriptorPool*>(inDescriptorHeap);
	CHECK(pool->getCreateParams().purpose == EDescriptorHeapPurpose::Volatile);

	VkDevice vkDevice = device->getRaw();
	VkPipelineLayout vkPipelineLayout = computePSO->getVkPipelineLayout();
	VkDescriptorPool vkDescPool = pool->getVkPool();
	const std::vector<VkDescriptorSetLayout>& vkDescriptorSetLayouts = computePSO->getVkDescriptorSetLayouts();

	// #wip: When to vkResetDescriptorPool()?
#if 0
	if (tracker == nullptr || tracker->lastIndex == 0)
	{
		vkResetDescriptorPool(vkDevice, vkDescPool, (VkDescriptorPoolResetFlags)0);
	}
#endif
	uint32 setGeneration = (tracker == nullptr) ? 0 : tracker->lastIndex;

	const uint32_t descSetCount = (uint32_t)vkDescriptorSetLayouts.size();
	const uint32_t firstSet = 0; // Assume firstSet=0 and consecutive set indices.
	std::vector<uint32_t> dynamicOffsets = {}; // Needed?

	const std::vector<VkDescriptorSet>* vkDescriptorSets = pool->findCachedDescriptorSets(computePSO, setGeneration);
	if (vkDescriptorSets == nullptr)
	{
		vkDescriptorSets = pool->createDescriptorSets(computePSO, setGeneration, vkDescriptorSetLayouts);
	}

	auto bindParameterClass = [computePSO, vkDescriptorSets]
		<typename T>(std::vector<VkCopyDescriptorSet>& outCopies, const std::vector<T>& parameters, VkDescriptorType vkDescriptorType)
	{
		for (const auto& inParam : parameters)
		{
			auto srcPool = static_cast<VulkanDescriptorPool*>(inParam.sourceHeap);
			CHECK(srcPool->getCreateParams().purpose == EDescriptorHeapPurpose::Persistent);

			const VulkanShaderParameter* param = computePSO->findShaderParameter(inParam.name);
			if (param == nullptr)
			{
				reportUndeclaredShaderParameter(inParam.name.c_str());
				continue;
			}

			VkCopyDescriptorSet copyDesc{
				.sType           = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
				.pNext           = nullptr,
				.srcSet          = srcPool->getVkDescriptorSetGlobal(),
				.srcBinding      = srcPool->getDescriptorBindingIndex(vkDescriptorType),
				.srcArrayElement = inParam.startIndex,
				.dstSet          = (*vkDescriptorSets)[param->set],
				.dstBinding      = param->binding,
				.dstArrayElement = 0,
				.descriptorCount = inParam.count,
			};
			outCopies.emplace_back(copyDesc);
		}
	};

	for (const auto& inParam : inParameters->_pushConstants)
	{
		const VulkanPushConstantParameter* param = computePSO->findPushConstantParameter(inParam.name);
		if (param == nullptr)
		{
			reportUndeclaredShaderParameter(inParam.name.c_str());
			continue;
		}

		CHECK(inParam.destOffsetIn32BitValues == param->range.offset); // #wip: What to use?

		vkCmdPushConstants(
			currentCommandBuffer,
			vkPipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT,
			param->range.offset,
			param->range.size,
			inParam.values.data());
	}

	std::vector<VkCopyDescriptorSet> copies;
	bindParameterClass(copies, inParameters->rwStructuredBuffers, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	bindParameterClass(copies, inParameters->rwBuffers, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	bindParameterClass(copies, inParameters->structuredBuffers, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	// #wip: sampled image or combined image sampler?
	bindParameterClass(copies, inParameters->textures, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	bindParameterClass(copies, inParameters->rwTextures, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

	vkUpdateDescriptorSets(vkDevice, 0, nullptr, static_cast<uint32_t>(copies.size()), copies.data());

	vkCmdBindDescriptorSets(
		currentCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		vkPipelineLayout, firstSet,
		(uint32_t)vkDescriptorSets->size(), vkDescriptorSets->data(),
		(uint32_t)dynamicOffsets.size(), dynamicOffsets.data());

	if (tracker != nullptr)
	{
		tracker->lastIndex += 1;
	}
}

void VulkanRenderCommandList::dispatchCompute(uint32 threadGroupX, uint32 threadGroupY, uint32 threadGroupZ)
{
	vkCmdDispatch(currentCommandBuffer, threadGroupX, threadGroupY, threadGroupZ);
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
	device->beginVkDebugMarker(currentCommandBuffer, eventName);
}

void VulkanRenderCommandList::endEventMarker()
{
	device->endVkDebugMarker(currentCommandBuffer);
}

#endif
