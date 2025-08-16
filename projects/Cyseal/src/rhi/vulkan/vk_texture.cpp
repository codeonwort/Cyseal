#include "vk_texture.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "vk_into.h"
#include "util/string_conversion.h"


VulkanTexture::~VulkanTexture()
{
	VkDevice vkDevice = device->getRaw();

	vkDestroyImage(vkDevice, vkImage, nullptr);
	vkFreeMemory(vkDevice, vkImageMemory, nullptr);
}

void VulkanTexture::initialize(const TextureCreateParams& inParams)
{
	createParams = inParams;

	VkDevice vkDevice = device->getRaw();
	VkPhysicalDevice vkPhysicalDevice = device->getVkPhysicalDevice();

	VkImageCreateInfo textureDesc = into_vk::textureDesc(inParams);

	// Create image and image memory

	VkResult ret = vkCreateImage(vkDevice, &textureDesc, nullptr, &vkImage);
	CHECK(ret == VK_SUCCESS);

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(vkDevice, vkImage, &memRequirements);
	uint32 memoryTypeIndex = findMemoryType(vkPhysicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkMemoryAllocateInfo allocInfo{
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext           = nullptr,
		.allocationSize  = memRequirements.size,
		.memoryTypeIndex = memoryTypeIndex,
	};

	ret = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &vkImageMemory);
	CHECK(ret == VK_SUCCESS);

	vkBindImageMemory(vkDevice, vkImage, vkImageMemory, 0);
}

void VulkanTexture::uploadData(
	RenderCommandList& commandList,
	const void* buffer,
	uint64 rowPitch,
	uint64 slicePitch,
	uint32 subresourceIndex)
{
	// #todo-vulkan
}

void VulkanTexture::setDebugName(const wchar_t* debugNameW)
{
	std::string debugNameA;
	wstr_to_str(debugNameW, debugNameA);

	device->setObjectDebugName(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, (uint64)vkImage, debugNameA.c_str());
}

#endif // COMPILE_BACKEND_VULKAN
