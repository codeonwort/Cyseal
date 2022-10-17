#include "vk_buffer.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "vk_utils.h"

void createBufferUtil(
	VkDevice vkDevice,
	VkPhysicalDevice vkPhysicalDevice,
	VkDeviceSize size,
	VkBufferUsageFlags usage,
	VkMemoryPropertyFlags properties,
	VkBuffer& outBuffer,
	VkDeviceMemory& outBufferMemory)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult ret = vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &outBuffer);
	CHECK(ret == VK_SUCCESS);

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(vkDevice, outBuffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(
		vkPhysicalDevice,
		memRequirements.memoryTypeBits,
		properties);

	ret = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &outBufferMemory);
	CHECK(ret == VK_SUCCESS);

	vkBindBufferMemory(vkDevice, outBuffer, outBufferMemory, 0);
}

//////////////////////////////////////////////////////////////////////////
// VulkanVertexBuffer

void VulkanVertexBuffer::initialize(uint32 sizeInBytes)
{
	vkBufferSize = (VkDeviceSize)sizeInBytes;

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

void VulkanVertexBuffer::initializeWithinPool(
	VertexBufferPool* pool,
	uint64 offsetInPool,
	uint32 sizeInBytes)
{
	//throw std::logic_error("The method or operation is not implemented.");
}

void VulkanVertexBuffer::updateData(
	RenderCommandList* commandList,
	void* data,
	uint32 strideInBytes)
{
	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	VkDevice vkDevice = deviceWrapper->getRaw();
	VkPhysicalDevice vkPhysicalDevice = deviceWrapper->getVkPhysicalDevice();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createBufferUtil(
		vkDevice,
		vkPhysicalDevice,
		vkBufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingBufferMemory);

	void* mapPtr;
	vkMapMemory(vkDevice, stagingBufferMemory, 0, vkBufferSize, 0, &mapPtr);
	memcpy_s(mapPtr, (size_t)vkBufferSize, data, (size_t)vkBufferSize);
	vkUnmapMemory(vkDevice, stagingBufferMemory);

	deviceWrapper->copyVkBuffer(stagingBuffer, vkBuffer, vkBufferSize);

	vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
	vkFreeMemory(vkDevice, stagingBufferMemory, nullptr);
}

//////////////////////////////////////////////////////////////////////////
// VulkanIndexBuffer

void VulkanIndexBuffer::initialize(uint32 sizeInBytes)
{
	vkBufferSize = (VkDeviceSize)sizeInBytes;

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
	//throw std::logic_error("The method or operation is not implemented.");
}

void VulkanIndexBuffer::updateData(
	RenderCommandList* commandList,
	void* data,
	EPixelFormat format)
{
	VkFormat vkFormat = VK_FORMAT_UNDEFINED;
	switch (format)
	{
		case EPixelFormat::R16_UINT:
			vkFormat = VK_FORMAT_R16_UINT;
			indexCount = (uint32)vkBufferSize / 2;
			break;
		case EPixelFormat::R32_UINT:
			vkFormat = VK_FORMAT_R32_UINT;
			indexCount = (uint32)vkBufferSize / 4;
			break;
	}
	CHECK(vkFormat != VK_FORMAT_UNDEFINED);

	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	VkDevice vkDevice = deviceWrapper->getRaw();
	VkPhysicalDevice vkPhysicalDevice = deviceWrapper->getVkPhysicalDevice();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createBufferUtil(
		vkDevice,
		vkPhysicalDevice,
		vkBufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingBufferMemory);

	void* mapPtr;
	vkMapMemory(vkDevice, stagingBufferMemory, 0, vkBufferSize, 0, &mapPtr);
	memcpy_s(mapPtr, (size_t)vkBufferSize, data, (size_t)vkBufferSize);
	vkUnmapMemory(vkDevice, stagingBufferMemory);

	deviceWrapper->copyVkBuffer(stagingBuffer, vkBuffer, vkBufferSize);

	vkDestroyBuffer(vkDevice, stagingBuffer, nullptr);
	vkFreeMemory(vkDevice, stagingBufferMemory, nullptr);
}

#endif // COMPILE_BACKEND_VULKAN

