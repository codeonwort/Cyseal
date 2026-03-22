#include "vk_texture.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "vk_render_command.h"
#include "vk_into.h"
#include "util/string_conversion.h"


VulkanTexture::VulkanTexture(VulkanDevice* inDevice)
	: device(inDevice)
{
	// TextureKind sets EBarrierLayout::Common as default value,
	// but initialLayout for VkImageCreateInfo is VK_IMAGE_LAYOUT_UNDEFINED.
	auto state = internal_getLastBarrierState();
	state.globalState.layoutBefore = EBarrierLayout::Undefined;
	internal_setLastBarrierState(state);
}

VulkanTexture::~VulkanTexture()
{
	VkDevice vkDevice = device->getRaw();

	vkDestroyImage(vkDevice, vkImage, nullptr);
	vkDestroyBuffer(vkDevice, vkUploadBuffer, nullptr);
	vkDestroyBuffer(vkDevice, vkReadbackBuffer, nullptr);
	vkFreeMemory(vkDevice, vkImageMemory, nullptr);
	vkFreeMemory(vkDevice, vkUploadMemory, nullptr);
	vkFreeMemory(vkDevice, vkReadbackMemory, nullptr);
}

void VulkanTexture::initialize(const TextureCreateParams& inParams)
{
	createParams = inParams;

	VkDevice vkDevice = device->getRaw();
	VkPhysicalDevice vkPhysicalDevice = device->getVkPhysicalDevice();

	rowPitchAlignment = device->getVkPhysicalDeviceLimits().optimalBufferCopyRowPitchAlignment;

	// Create image.
	{
		constexpr bool bSkipReadbackFlag = false;
		VkImageCreateInfo textureDesc = into_vk::textureDesc(inParams, bSkipReadbackFlag);

		VkResult vkRet = vkCreateImage(vkDevice, &textureDesc, nullptr, &vkImage);
		CHECK(vkRet == VK_SUCCESS);

		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(vkDevice, vkImage, &req);
		VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		uint32 memoryTypeIndex = findMemoryType(vkPhysicalDevice, req.memoryTypeBits, memProps);

		VkMemoryAllocateInfo allocInfo{
			.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext           = nullptr,
			.allocationSize  = req.size,
			.memoryTypeIndex = memoryTypeIndex,
		};
		allocSize = req.size;
		rowPitch = req.alignment;

		vkRet = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &vkImageMemory);
		CHECK(vkRet == VK_SUCCESS);

		vkBindImageMemory(vkDevice, vkImage, vkImageMemory, 0);
	};

	// Create upload buffer, if needed.
	if (ENUM_HAS_FLAG(createParams.accessFlags, ETextureAccessFlags::CPU_WRITE))
	{
		VkBufferCreateInfo createInfo{
			.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.pNext                 = nullptr,
			.flags                 = (VkBufferCreateFlagBits)0,
			.size                  = allocSize,
			.usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 0,
			.pQueueFamilyIndices   = nullptr,
		};

		VkResult vkRet = vkCreateBuffer(vkDevice, &createInfo, nullptr, &vkUploadBuffer);
		CHECK(vkRet == VK_SUCCESS);

		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(vkDevice, vkUploadBuffer, &req);
		CHECK(req.size >= allocSize);

		VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		uint32_t memoryTypeIndex = findMemoryType(vkPhysicalDevice, req.memoryTypeBits, memProps);

		VkMemoryAllocateInfo allocInfo{
			.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext           = nullptr,
			.allocationSize  = req.size,
			.memoryTypeIndex = memoryTypeIndex,
		};

		vkRet = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &vkUploadMemory);
		CHECK(vkRet == VK_SUCCESS);

		vkBindBufferMemory(vkDevice, vkUploadBuffer, vkUploadMemory, 0);
	}

	// Create readback buffer, if needed.
	if (ENUM_HAS_FLAG(createParams.accessFlags, ETextureAccessFlags::CPU_READBACK))
	{
		VkBufferCreateInfo createInfo{
			.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.pNext                 = nullptr,
			.flags                 = (VkBufferCreateFlagBits)0,
			.size                  = allocSize,
			.usage                 = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 0,
			.pQueueFamilyIndices   = nullptr,
		};

		VkResult vkRet = vkCreateBuffer(vkDevice, &createInfo, nullptr, &vkReadbackBuffer);
		CHECK(vkRet == VK_SUCCESS);

		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(vkDevice, vkReadbackBuffer, &req);
		CHECK(req.size >= allocSize);

		VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		uint32_t memoryTypeIndex = findMemoryType(vkPhysicalDevice, req.memoryTypeBits, memProps);

		VkMemoryAllocateInfo allocInfo{
			.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext           = nullptr,
			.allocationSize  = req.size,
			.memoryTypeIndex = memoryTypeIndex,
		};

		vkRet = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &vkReadbackMemory);
		CHECK(vkRet == VK_SUCCESS);

		vkBindBufferMemory(vkDevice, vkReadbackBuffer, vkReadbackMemory, 0);
	}
}

void VulkanTexture::uploadData(
	RenderCommandList* commandList,
	const void* buffer,
	uint64 rowPitch,
	uint64 slicePitch,
	uint32 subresourceIndex)
{
	const uint64 uploadSize = slicePitch * createParams.depth;
	CHECK(uploadSize <= allocSize);

	VkDevice vkDevice = device->getRaw();
	VkCommandBuffer cmd = static_cast<VulkanRenderCommandList*>(commandList)->internal_getVkCommandBuffer();

	void* pData = nullptr;
	vkMapMemory(vkDevice, vkUploadMemory, 0, uploadSize, (VkMemoryMapFlags)0, &pData);
	std::memcpy(pData, buffer, uploadSize);
	vkUnmapMemory(vkDevice, vkUploadMemory);

	VkImageAspectFlags aspectMask = isDepthStencilFormat(createParams.format)
		? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
		: VK_IMAGE_ASPECT_COLOR_BIT;
	
	// https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html
	VkBufferImageCopy region{
		.bufferOffset       = 0,
		// Hmm I don't understand the doc :( let's just make it use imageExtent.
		.bufferRowLength    = 0,//(uint32)rowPitch,
		.bufferImageHeight  = 0,//(uint32)(slicePitch / rowPitch),
		.imageSubresource   = VkImageSubresourceLayers{
			.aspectMask     = aspectMask,
			.mipLevel       = subresourceIndex,
			.baseArrayLayer = 0,
			.layerCount     = 1,
		},
		.imageOffset        = VkOffset3D { 0, 0, 0 },
		.imageExtent        = VkExtent3D { createParams.width, createParams.height, createParams.depth },
	};

	TextureBarrierAuto texBarrier = TextureBarrierAuto::toCopyDest(this);
	commandList->barrierAuto(0, nullptr, 1, &texBarrier, 0, nullptr);

	vkCmdCopyBufferToImage(cmd, vkUploadBuffer, vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

SharedPtr<Texture::ReadbackHandle> VulkanTexture::requestReadback(
	RenderCommandList* commandList, const ReadbackRegion& region)
{
	CHECK(ENUM_HAS_FLAG(createParams.accessFlags, ETextureAccessFlags::CPU_READBACK));

	// If previous readback request is alive, then reject the request.
	// Multiple readbacks are possible in theory, but let's keep it simple for now.
	if (readbackHandle.expired() == false)
	{
		return nullptr;
	}

	auto vulkanCmdList = static_cast<VulkanRenderCommandList*>(commandList);
	auto vkCmdBuffer = vulkanCmdList->internal_getVkCommandBuffer();

	// #wip: Figure out proper values
	BarrierSubresourceRange subresourceRange{
		.indexOrFirstMipLevel = region.mipLevel,
		.numMipLevels         = 1,
		.firstArraySlice      = region.baseArrayLayer,
		.numArraySlices       = region.layerCount,
		.firstPlane           = 0,
		.numPlanes            = 1,
	};
	TextureBarrierAuto texBarrier{
		EBarrierSync::COPY, EBarrierAccess::COPY_SOURCE, EBarrierLayout::CopySource,
		this, subresourceRange, ETextureBarrierFlags::None
	};
	commandList->barrierAuto(0, nullptr, 1, &texBarrier, 0, nullptr);

	VkImageAspectFlags aspectMask = isDepthStencilFormat(createParams.format)
		? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
		: VK_IMAGE_ASPECT_COLOR_BIT;
	
	// https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html
	VkBufferImageCopy copyRegion{
		.bufferOffset       = 0,
		// Hmm I don't understand the doc :( let's just make it use imageExtent.
		.bufferRowLength    = 0,//(uint32)rowPitch,
		.bufferImageHeight  = 0,//(uint32)(slicePitch / rowPitch),
		.imageSubresource   = VkImageSubresourceLayers{
			.aspectMask     = aspectMask,
			.mipLevel       = region.mipLevel,
			.baseArrayLayer = region.baseArrayLayer,
			.layerCount     = region.layerCount,
		},
		.imageOffset        = VkOffset3D { (int32)region.offsetX, (int32)region.offsetY, (int32)region.offsetZ },
		.imageExtent        = VkExtent3D { region.sizeX, region.sizeY, region.sizeZ },
	};
	vkCmdCopyImageToBuffer(vkCmdBuffer, vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkReadbackBuffer, 1, &copyRegion);

	auto newHandle = makeShared<Texture::ReadbackHandle>();
	newHandle->owner = this;
	newHandle->rowPitch = Cymath::alignBytes64(region.sizeX * getPixelFormatBytes(createParams.format), rowPitchAlignment);
	newHandle->slicePitch = newHandle->rowPitch * region.sizeY;
	newHandle->totalBytes = newHandle->slicePitch * region.sizeZ;

	vulkanCmdList->addReadbackHandle(newHandle);

	readbackHandle = newHandle;
	return newHandle;
}

void VulkanTexture::internal_finalizeReadbackBuffer()
{
	CHECK(ENUM_HAS_FLAG(createParams.accessFlags, ETextureAccessFlags::CPU_READBACK));
	CHECK(readbackHandle.expired() == false);

	VkDevice vkDevice = device->getRaw();
	auto req = readbackHandle.lock();

	const size_t readbackBufferSize = req->totalBytes;
	req->readbackData = new uint8[readbackBufferSize];

	void* pData = nullptr;
	vkMapMemory(vkDevice, vkReadbackMemory, 0, readbackBufferSize, 0, &pData);
	std::memcpy(req->readbackData, pData, readbackBufferSize);
	vkUnmapMemory(vkDevice, vkReadbackMemory);

	req->bAvailable = true;
}

void VulkanTexture::setDebugName(const wchar_t* debugNameW)
{
	std::string debugNameA;
	wstr_to_str(debugNameW, debugNameA);

	device->setObjectDebugName(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, (uint64)vkImage, debugNameA.c_str());
}

#endif // COMPILE_BACKEND_VULKAN
