#include "vk_buffer.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "vk_utils.h"
#include "vk_render_command.h"
#include "rhi/vertex_buffer_pool.h"

static void createBufferUtil(
	VkDevice vkDevice,
	VkPhysicalDevice vkPhysicalDevice,
	VkDeviceSize size,
	VkBufferUsageFlags bufferUsageFlags,
	VkMemoryPropertyFlags memoryProperties,
	VkBuffer& outBuffer,
	VkDeviceMemory& outBufferMemory)
{
	VkBufferCreateInfo createInfo{
		.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext                 = nullptr,
		.flags                 = (VkBufferCreateFlagBits)0,
		.size                  = size,
		.usage                 = bufferUsageFlags,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices   = nullptr,
	};

	VkResult vkRet = vkCreateBuffer(vkDevice, &createInfo, nullptr, &outBuffer);
	CHECK(vkRet == VK_SUCCESS);

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(vkDevice, outBuffer, &memRequirements);
	
	uint32_t memoryTypeIndex = findMemoryType(
		vkPhysicalDevice, memRequirements.memoryTypeBits, memoryProperties);

	VkMemoryAllocateInfo allocInfo{
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext           = nullptr,
		.allocationSize  = memRequirements.size,
		.memoryTypeIndex = memoryTypeIndex,
	};

	vkRet = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &outBufferMemory);
	CHECK(vkRet == VK_SUCCESS);

	vkBindBufferMemory(vkDevice, outBuffer, outBufferMemory, 0);
}

static void updateDefaultBuffer(
	VkDevice vkDevice, VkPhysicalDevice vkPhysicalDevice,
	VkBuffer defaultBuffer, VkDeviceSize defaultBufferOffset,
	void* srcData, VkDeviceSize dataSizeInBytes)
{
	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	VkCommandPool vkCommandPool = deviceWrapper->getTempCommandPool();

	VkBuffer uploadBuffer = VK_NULL_HANDLE;
	VkDeviceMemory uploadBufferMemory = VK_NULL_HANDLE;
	createBufferUtil(
		vkDevice,
		vkPhysicalDevice,
		dataSizeInBytes,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		uploadBuffer, uploadBufferMemory);

	void* uploadMapPtr = nullptr;
	vkMapMemory(vkDevice, uploadBufferMemory, 0, dataSizeInBytes, 0, &uploadMapPtr);
	::memcpy_s(uploadMapPtr, (size_t)dataSizeInBytes, srcData, (size_t)dataSizeInBytes);
	vkUnmapMemory(vkDevice, uploadBufferMemory);

	VkCommandBuffer vkCommandBuffer = beginSingleTimeCommands(vkDevice, vkCommandPool);

	VkBufferCopy region{
		.srcOffset = 0,
		.dstOffset = defaultBufferOffset,
		.size      = dataSizeInBytes,
	};
	vkCmdCopyBuffer(vkCommandBuffer, uploadBuffer, defaultBuffer, 1, &region);

	endSingleTimeCommands(vkDevice, vkCommandPool, deviceWrapper->getVkGraphicsQueue(), vkCommandBuffer);

	vkDestroyBuffer(vkDevice, uploadBuffer, nullptr);
	vkFreeMemory(vkDevice, uploadBufferMemory, nullptr);
}

//////////////////////////////////////////////////////////////////////////
// VulkanVertexBuffer

VulkanVertexBuffer::~VulkanVertexBuffer()
{
	VkDevice vkDevice = getVkDevice();
	vkDestroyBuffer(vkDevice, vkBuffer, nullptr);
	vkFreeMemory(vkDevice, vkBufferMemory, nullptr);
}

void VulkanVertexBuffer::initialize(uint32 sizeInBytes)
{
	vkBufferSize = (VkDeviceSize)sizeInBytes;

	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	VkDevice vkDevice = deviceWrapper->getRaw();
	VkPhysicalDevice vkPhysicalDevice = deviceWrapper->getVkPhysicalDevice();

	createBufferUtil(
		vkDevice,
		vkPhysicalDevice,
		vkBufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		vkBuffer, vkBufferMemory);
}

void VulkanVertexBuffer::initializeWithinPool(
	VertexBufferPool* pool,
	uint64 offsetInPool,
	uint32 sizeInBytes)
{
	parentPool = pool;
	offsetInDefaultBuffer = offsetInPool;
	vkBufferSize = (VkDeviceSize)sizeInBytes;

	VulkanVertexBuffer* poolBuffer = static_cast<VulkanVertexBuffer*>(pool->internal_getPoolBuffer());
	vkBuffer = poolBuffer->vkBuffer;
}

void VulkanVertexBuffer::updateData(
	RenderCommandList* commandList,
	void* data,
	uint32 strideInBytes)
{
	vertexCount = (uint32)(vkBufferSize / strideInBytes);

	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	updateDefaultBuffer(
		deviceWrapper->getRaw(),
		deviceWrapper->getVkPhysicalDevice(),
		vkBuffer, offsetInDefaultBuffer,
		data, vkBufferSize);
}

//////////////////////////////////////////////////////////////////////////
// VulkanIndexBuffer

void VulkanIndexBuffer::initialize(uint32 sizeInBytes, EPixelFormat format)
{
	vkBufferSize = (VkDeviceSize)sizeInBytes;
	indexFormat = format;
	CHECK(vkBufferSize > 0);

	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	VkDevice vkDevice = deviceWrapper->getRaw();
	VkPhysicalDevice vkPhysicalDevice = deviceWrapper->getVkPhysicalDevice();

	createBufferUtil(
		vkDevice,
		vkPhysicalDevice,
		sizeInBytes,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		vkBuffer, vkBufferMemory);
}

void VulkanIndexBuffer::initializeWithinPool(
	IndexBufferPool* pool,
	uint64 offsetInPool,
	uint32 sizeInBytes)
{
	parentPool = pool;
	offsetInDefaultBuffer = offsetInPool;
	vkBufferSize = (VkDeviceSize)sizeInBytes;
	CHECK(vkBufferSize > 0);

	VulkanIndexBuffer* poolBuffer = static_cast<VulkanIndexBuffer*>(pool->internal_getPoolBuffer());
	vkBuffer = poolBuffer->vkBuffer;
}

void VulkanIndexBuffer::updateData(
	RenderCommandList* commandList,
	void* data,
	EPixelFormat format)
{
	vkIndexType = VK_INDEX_TYPE_MAX_ENUM;
	switch (format)
	{
		case EPixelFormat::R16_UINT:
			vkIndexType = VK_INDEX_TYPE_UINT16;
			indexCount = (uint32)vkBufferSize / 2;
			break;
		case EPixelFormat::R32_UINT:
			vkIndexType = VK_INDEX_TYPE_UINT32;
			indexCount = (uint32)vkBufferSize / 4;
			break;
	}
	CHECK(vkIndexType != VK_INDEX_TYPE_MAX_ENUM);

	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	updateDefaultBuffer(
		deviceWrapper->getRaw(),
		deviceWrapper->getVkPhysicalDevice(),
		vkBuffer, offsetInDefaultBuffer,
		data, vkBufferSize);
}

#endif // COMPILE_BACKEND_VULKAN
