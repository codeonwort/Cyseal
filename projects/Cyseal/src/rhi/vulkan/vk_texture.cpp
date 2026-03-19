#include "vk_texture.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "vk_into.h"
#include "util/string_conversion.h"


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

	VkDeviceSize allocSize = 0;

	// Create image.
	{
		constexpr bool bSkipReadbackFlag = true;
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
	RenderCommandList& commandList,
	const void* buffer,
	uint64 rowPitch,
	uint64 slicePitch,
	uint32 subresourceIndex)
{
	// #wip: VulkanTexture::uploadData
	CHECK_NO_ENTRY();
}

void VulkanTexture::setDebugName(const wchar_t* debugNameW)
{
	std::string debugNameA;
	wstr_to_str(debugNameW, debugNameA);

	device->setObjectDebugName(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, (uint64)vkImage, debugNameA.c_str());
}

#endif // COMPILE_BACKEND_VULKAN
