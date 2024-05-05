#include "vk_texture.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "vk_into.h"
#include "util/string_conversion.h"


VulkanTexture::~VulkanTexture()
{
	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	VkDevice vkDevice = deviceWrapper->getRaw();

	srv.reset();
	rtv.reset();
	dsv.reset();

	vkDestroyImage(vkDevice, vkImage, nullptr);
	if ((vkSRV != VK_NULL_HANDLE) || (vkRTV != VK_NULL_HANDLE))
	{
		// They are all same. Destroy only one.
		vkDestroyImageView(vkDevice, vkSRV, nullptr);
	}
	if (vkDSV != VK_NULL_HANDLE)
	{
		vkDestroyImageView(vkDevice, vkDSV, nullptr);
	}
	vkFreeMemory(vkDevice, vkImageMemory, nullptr);
}

void VulkanTexture::initialize(const TextureCreateParams& inParams)
{
	createParams = inParams;

	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	VkDevice vkDevice = deviceWrapper->getRaw();
	VkPhysicalDevice vkPhysicalDevice = deviceWrapper->getVkPhysicalDevice();

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

	// Create image views
	// #todo-vulkan: Vulkan doesn't have separate SRV/RTV/UAV type,
	//               and 'usage' is already specified in VkImageCreateInfo.
	//               Then only image layout transition matters... as I remember?

	VkImageView colorImageView = VK_NULL_HANDLE;
	if (0 != (inParams.accessFlags & ETextureAccessFlags::SRV)
		|| 0 != (inParams.accessFlags & ETextureAccessFlags::RTV)
		|| 0 != (inParams.accessFlags & ETextureAccessFlags::UAV))
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = vkImage;
		// #todo-vulkan: Non-2D viewType
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = textureDesc.format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		// #todo-vulkan: VkImage may have multiple mips
		viewInfo.subresourceRange.levelCount = 1;
		// #todo-vulkan: For tex 2d array
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkResult ret = vkCreateImageView(vkDevice, &viewInfo, nullptr, &colorImageView);
		CHECK(ret == VK_SUCCESS);
	}

	if (0 != (inParams.accessFlags & ETextureAccessFlags::SRV))
	{
		// #todo-vulkan: SRV ViewDimension
		CHECK(textureDesc.imageType == VkImageType::VK_IMAGE_TYPE_2D);

		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                    = createParams.format;
		srvDesc.viewDimension             = ESRVDimension::Texture2D;
		srvDesc.texture2D.mostDetailedMip = 0;
		srvDesc.texture2D.mipLevels       = textureDesc.mipLevels;
		srvDesc.texture2D.planeSlice      = 0;
		srvDesc.texture2D.minLODClamp     = 0.0f;

		srv = std::unique_ptr<ShaderResourceView>(gRenderDevice->createSRV(this, srvDesc));
		srvHeap = srv->getSourceHeap();
		srvDescriptorIndex = srv->getDescriptorIndexInHeap();

		vkSRV = colorImageView;
	}

	if (0 != (inParams.accessFlags & ETextureAccessFlags::RTV))
	{
		vkRTV = colorImageView;
		rtv = std::make_unique<VulkanRenderTargetView>(vkRTV);
	}

	if (0 != (inParams.accessFlags & ETextureAccessFlags::DSV))
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = vkImage;
		// #todo-vulkan: Non-2D viewType
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = textureDesc.format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		// #todo-vulkan: VkImage may have multiple mips
		viewInfo.subresourceRange.levelCount = 1;
		// #todo-vulkan: For tex 2d array
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkResult ret = vkCreateImageView(vkDevice, &viewInfo, nullptr, &vkDSV);
		CHECK(ret == VK_SUCCESS);

		dsv = std::make_unique<VulkanDepthStencilView>(vkDSV);
	}
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

	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	deviceWrapper->setObjectDebugName(
		VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT,
		(uint64)vkImage,
		debugNameA.c_str());
}

RenderTargetView* VulkanTexture::getRTV() const
{
	return rtv.get();
}

ShaderResourceView* VulkanTexture::getSRV() const
{
	return srv.get();
}

DepthStencilView* VulkanTexture::getDSV() const
{
	return dsv.get();
}

#endif // COMPILE_BACKEND_VULKAN
