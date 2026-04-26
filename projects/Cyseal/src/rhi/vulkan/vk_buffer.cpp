#include "vk_buffer.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "vk_utils.h"
#include "vk_render_command.h"
#include "rhi/buffer.h"
#include "rhi/vertex_buffer_pool.h"
#include "util/string_conversion.h"

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
	VulkanDevice* device,
	VkBuffer defaultBuffer, VkDeviceSize defaultBufferOffset,
	void* srcData, VkDeviceSize dataSizeInBytes)
{
	VkDevice vkDevice = device->getRaw();
	VkPhysicalDevice vkPhysicalDevice = device->getVkPhysicalDevice();
	VkCommandPool vkCommandPool = device->getTempCommandPool();

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

	endSingleTimeCommands(vkDevice, vkCommandPool, device->getVkGraphicsQueue(), vkCommandBuffer);

	vkDestroyBuffer(vkDevice, uploadBuffer, nullptr);
	vkFreeMemory(vkDevice, uploadBufferMemory, nullptr);
}

//////////////////////////////////////////////////////////////////////////
// VulkanBuffer

VulkanBuffer::~VulkanBuffer()
{
	VkDevice vkDevice = device->getRaw();
	vkDestroyBuffer(vkDevice, vkBuffer, nullptr);
	vkDestroyBuffer(vkDevice, vkUploadBuffer, nullptr);
	vkDestroyBuffer(vkDevice, vkReadbackBuffer, nullptr);
	CHECK(readbackHandle.expired());
	vkFreeMemory(vkDevice, vkBufferMemory, nullptr);
	vkFreeMemory(vkDevice, vkUploadMemory, nullptr);
	vkFreeMemory(vkDevice, vkReadbackMemory, nullptr);
}

void VulkanBuffer::onInitialize()
{
	VkDevice vkDevice = device->getRaw();
	VkPhysicalDevice vkPhysicalDevice = device->getVkPhysicalDevice();

	VkBufferUsageFlags usage = 0;
	if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::COPY_SRC)) usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::COPY_DST)) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::VERTEX_BUFFER)) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::INDEX_BUFFER)) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::CBV)) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::SRV)) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; // Both SRV and UAV are SSBO in Vulkan
	if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::UAV)) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	{
		VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		createBufferUtil(
			vkDevice,
			vkPhysicalDevice,
			(VkDeviceSize)createParams.sizeInBytes,
			usage,
			memoryProps,
			vkBuffer, vkBufferMemory);
	}
	// Upload buffer
	if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::CPU_WRITE))
	{
		CHECK(ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::COPY_DST));
		VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		createBufferUtil(
			vkDevice, vkPhysicalDevice, (VkDeviceSize)createParams.sizeInBytes,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			memoryProps,
			vkUploadBuffer, vkUploadMemory);
	}
	// Readback buffer
	if (ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::CPU_READBACK))
	{
		CHECK(ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::COPY_SRC));
		VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		createBufferUtil(
			vkDevice, vkPhysicalDevice, (VkDeviceSize)createParams.sizeInBytes,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			memoryProps,
			vkReadbackBuffer, vkReadbackMemory);
	}
}

void VulkanBuffer::writeToGPU(RenderCommandList* commandList, uint32 numUploads, Buffer::UploadDesc* uploadDescs)
{
	CHECK(ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::CPU_WRITE));
	for (uint32 i = 0; i < numUploads; ++i)
	{
		CHECK((createParams.alignment == 0) || (uploadDescs[i].destOffsetInBytes % createParams.alignment == 0));
		CHECK(uploadDescs[i].destOffsetInBytes + uploadDescs[i].sizeInBytes <= createParams.sizeInBytes);
	}

	VkDevice vkDevice = device->getRaw();
	VulkanRenderCommandList* vkCmdList = static_cast<VulkanRenderCommandList*>(commandList);
	VkCommandBuffer rawCmd = vkCmdList->internal_getVkCommandBuffer();

	BufferBarrierAuto barrierBefore{ EBarrierSync::COPY, EBarrierAccess::COPY_DEST, this };
	commandList->barrierAuto(1, &barrierBefore, 0, nullptr, 0, nullptr);

	void* pData = nullptr;
	vkMapMemory(vkDevice, vkUploadMemory, 0, VK_WHOLE_SIZE, (VkMemoryMapFlags)0, &pData);
	uint8* uploadMapPtr = reinterpret_cast<uint8*>(pData);

	std::vector<VkBufferCopy> regions(numUploads);
	for (uint32 i = 0; i < numUploads; ++i)
	{
		const UploadDesc& desc = uploadDescs[i];
		std::memcpy(uploadMapPtr + desc.destOffsetInBytes, desc.srcData, desc.sizeInBytes);

		regions[i] = VkBufferCopy{
			.srcOffset = desc.destOffsetInBytes,
			.dstOffset = desc.destOffsetInBytes,
			.size      = desc.sizeInBytes,
		};
	}
	vkCmdCopyBuffer(rawCmd, vkUploadBuffer, vkBuffer, numUploads, regions.data());

	vkUnmapMemory(vkDevice, vkUploadMemory);
}

SharedPtr<Buffer::ReadbackHandle> VulkanBuffer::requestReadback(
	RenderCommandList* commandList, uint64 offset, uint64 size)
{
	CHECK(ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::CPU_READBACK));

	// If previous readback request is alive, then reject the request.
	// Multiple readbacks are possible in theory, but let's keep it simple for now.
	if (readbackHandle.expired() == false)
	{
		return nullptr;
	}

	auto vulkanCmdList = static_cast<VulkanRenderCommandList*>(commandList);
	auto vkCmdBuffer = vulkanCmdList->internal_getVkCommandBuffer();

	BufferBarrierAuto barrier{ EBarrierSync::COPY, EBarrierAccess::COPY_DEST, this };
	vulkanCmdList->barrierAuto(1, &barrier, 0, nullptr, 0, nullptr);

	uint64 bytesToRead = (size == Buffer::READBACK_SIZE_ALL) ? (createParams.sizeInBytes - offset) : size;
	VkBufferCopy region{
		.srcOffset = offset,
		.dstOffset = 0,
		.size      = bytesToRead,
	};
	vkCmdCopyBuffer(vkCmdBuffer, vkBuffer, vkReadbackBuffer, 1, &region);

	auto newHandle = makeShared<Buffer::ReadbackHandle>();
	newHandle->owner = this;
	newHandle->readbackSize = bytesToRead;

	vulkanCmdList->addReadbackHandle(newHandle);

	readbackHandle = newHandle;
	return newHandle;
}

void VulkanBuffer::setDebugName(const wchar_t* inDebugNameW)
{
	std::string debugNameA;
	wstr_to_str(inDebugNameW, debugNameA);
	device->setObjectDebugName(
		VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT,
		(uint64)vkBuffer,
		debugNameA.c_str());
}

void VulkanBuffer::internal_finalizeReadbackBuffer()
{
	CHECK(ENUM_HAS_FLAG(createParams.accessFlags, EBufferAccessFlags::CPU_READBACK));
	CHECK(readbackHandle.expired() == false);

	auto req = readbackHandle.lock();

	req->readbackData = new uint8[req->readbackSize];

	VkDevice vkDevice = device->getRaw();
	void* pData = nullptr;
	vkMapMemory(vkDevice, vkReadbackMemory, 0, VK_WHOLE_SIZE, 0, &pData);
	std::memcpy(req->readbackData, pData, req->readbackSize);
	vkUnmapMemory(vkDevice, vkReadbackMemory);

	req->bAvailable = true;
}

//////////////////////////////////////////////////////////////////////////
// VulkanVertexBuffer

VulkanVertexBuffer::~VulkanVertexBuffer()
{
	if (parentPool == nullptr && internalBuffer != nullptr)
	{
		delete internalBuffer;
	}
}

void VulkanVertexBuffer::initialize(uint32 sizeInBytes, EBufferAccessFlags usageFlags)
{
	bufferSize = sizeInBytes;
	BufferCreateParams createParams{
		.sizeInBytes = sizeInBytes,
		.alignment   = 0,
		.accessFlags = EBufferAccessFlags::VERTEX_BUFFER | usageFlags,
	};
	internalBuffer = new VulkanBuffer(device);
	internalBuffer->initialize(createParams);
}

void VulkanVertexBuffer::initializeWithinPool(
	VertexBufferPool* pool,
	uint64 offsetInPool,
	uint32 sizeInBytes)
{
	parentPool = pool;
	offsetInParentBuffer = offsetInPool;
	bufferSize = sizeInBytes;
}

void VulkanVertexBuffer::updateData(
	RenderCommandList* commandList,
	void* data,
	uint32 strideInBytes)
{
	CHECK(!bRemovedFromPool);
	vertexCount = (uint32)(bufferSize / strideInBytes);

	VulkanVertexBuffer* bufferOwner = (parentPool == nullptr) ? this : static_cast<VulkanVertexBuffer*>(parentPool->internal_getPoolBuffer());
	VkBuffer vkBuffer = (VkBuffer)bufferOwner->internalBuffer->getRawResource();

	updateDefaultBuffer(device, vkBuffer, offsetInParentBuffer, data, bufferSize);
}

VkBuffer VulkanVertexBuffer::getVkBuffer() const
{
	const VulkanVertexBuffer* bufferOwner = (parentPool == nullptr) ? this : static_cast<VulkanVertexBuffer*>(parentPool->internal_getPoolBuffer());
	return (VkBuffer)bufferOwner->internalBuffer->getRawResource();
}

//////////////////////////////////////////////////////////////////////////
// VulkanIndexBuffer

VulkanIndexBuffer::~VulkanIndexBuffer()
{
	if (parentPool == nullptr && internalBuffer != nullptr)
	{
		delete internalBuffer;
	}
}

void VulkanIndexBuffer::initialize(uint32 sizeInBytes, EPixelFormat format, EBufferAccessFlags usageFlags)
{
	vkBufferSize = (VkDeviceSize)sizeInBytes;
	indexFormat = format;
	CHECK(vkBufferSize > 0);
	BufferCreateParams createParams{
		.sizeInBytes = sizeInBytes,
		.alignment   = 0,
		.accessFlags = EBufferAccessFlags::INDEX_BUFFER | usageFlags,
	};
	internalBuffer = new VulkanBuffer(device);
	internalBuffer->initialize(createParams);
}

void VulkanIndexBuffer::initializeWithinPool(
	IndexBufferPool* pool,
	uint64 offsetInPool,
	uint32 sizeInBytes)
{
	parentPool = pool;
	offsetInParentBuffer = offsetInPool;
	vkBufferSize = (VkDeviceSize)sizeInBytes;
	CHECK(vkBufferSize > 0);
}

void VulkanIndexBuffer::updateData(
	RenderCommandList* commandList,
	void* data,
	EPixelFormat format)
{
	CHECK(!bRemovedFromPool);
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

	updateDefaultBuffer(device, getVkBuffer(), offsetInParentBuffer, data, vkBufferSize);
}

VkBuffer VulkanIndexBuffer::getVkBuffer() const
{
	CHECK(!bRemovedFromPool);
	const VulkanIndexBuffer* bufferOwner = (parentPool == nullptr) ? this : static_cast<VulkanIndexBuffer*>(parentPool->internal_getPoolBuffer());
	return (VkBuffer)bufferOwner->internalBuffer->getRawResource();
}

#endif // COMPILE_BACKEND_VULKAN
