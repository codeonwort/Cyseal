#include "vk_texture.h"

#if COMPILE_BACKEND_VULKAN

#include "vk_device.h"
#include "util/string_conversion.h"

// Convert API-agnostic structs into D3D12 structs
namespace into_vk
{
	inline VkImageType textureDimension(ETextureDimension dimension)
	{
		switch (dimension)
		{
			case ETextureDimension::UNKNOWN:
			{
				CHECK_NO_ENTRY(); // #todo-vulkan: There is no 'typeless' in Vulkan?
				return VK_IMAGE_TYPE_MAX_ENUM;
			}
			case ETextureDimension::TEXTURE1D: return VkImageType::VK_IMAGE_TYPE_1D;
			case ETextureDimension::TEXTURE2D: return VkImageType::VK_IMAGE_TYPE_2D;
			case ETextureDimension::TEXTURE3D: return VkImageType::VK_IMAGE_TYPE_3D;
		}
		CHECK_NO_ENTRY();
		return VkImageType::VK_IMAGE_TYPE_MAX_ENUM;
	}

	inline VkFormat pixelFormat(EPixelFormat inFormat)
	{
		switch (inFormat)
		{
			case EPixelFormat::UNKNOWN:            return VkFormat::VK_FORMAT_UNDEFINED;
			// #todo-vulkan: R32_TYPLESS in Vulkan?
			case EPixelFormat::R32_TYPELESS:       return VkFormat::VK_FORMAT_R32_SFLOAT;
			case EPixelFormat::R8G8B8A8_UNORM:     return VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
			case EPixelFormat::R32G32B32_FLOAT:    return VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
			case EPixelFormat::R32G32B32A32_FLOAT: return VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
			case EPixelFormat::R32_UINT:           return VkFormat::VK_FORMAT_R32_UINT;
			case EPixelFormat::R16_UINT:           return VkFormat::VK_FORMAT_R16_UINT;
			case EPixelFormat::D24_UNORM_S8_UINT:  return VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
			default:
				// #todo: Unknown pixel format
				CHECK_NO_ENTRY();
		}

		return VkFormat::VK_FORMAT_UNDEFINED;
	}

	inline VkSampleCountFlagBits sampleCount(uint32 count)
	{
		switch (count)
		{
			case 1: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
			case 2: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_2_BIT;
			case 4: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_4_BIT;
			case 8: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_8_BIT;
			case 16: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_16_BIT;
			case 32: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_32_BIT;
			case 64: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_64_BIT;
			default: CHECK_NO_ENTRY();
		}
		CHECK_NO_ENTRY();
		return VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
	}

	inline VkImageCreateInfo textureDesc(const TextureCreateParams& params)
	{
		VkImageCreateInfo desc{};
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		desc.imageType = textureDimension(params.dimension);
		desc.extent.width = params.width;
		desc.extent.height = params.height;
		desc.extent.depth = params.depth;
		desc.mipLevels = params.mipLevels;
		desc.arrayLayers = params.numLayers;
		desc.format = pixelFormat(params.format);
		desc.tiling = VK_IMAGE_TILING_OPTIMAL; // #todo-vulkan: Texture tiling param
		// [VUID-VkImageCreateInfo-initialLayout-00993]
		// initialLayout must be VK_IMAGE_LAYOUT_UNDEFINED or VK_IMAGE_LAYOUT_PREINITIALIZED
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		desc.samples = sampleCount(params.sampleCount);
		desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		// #todo-vulkan: VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT for textureCube
		desc.flags = (VkImageCreateFlagBits)0;

		// #todo-vulkan: Other allow flags
		desc.usage = (VkImageUsageFlagBits)0;
		if (0 != (params.accessFlags & ETextureAccessFlags::SRV))
		{
			desc.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::RTV))
		{
			desc.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::UAV))
		{
			desc.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::DSV))
		{
			desc.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		}

		return desc;
	}
}

VulkanTexture::~VulkanTexture()
{
	VulkanDevice* deviceWrapper = static_cast<VulkanDevice*>(gRenderDevice);
	VkDevice vkDevice = deviceWrapper->getRaw();

	srv.reset();
	rtv.reset();
	uav.reset();
	dsv.reset();

	vkDestroyImage(vkDevice, vkImage, nullptr);
	if ((vkSRV != VK_NULL_HANDLE) || (vkRTV != VK_NULL_HANDLE) || (vkUAV != VK_NULL_HANDLE))
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

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(
		vkPhysicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

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
		vkSRV = colorImageView;
		srv = std::make_unique<VulkanShaderResourceView>(this, vkSRV);
	}
	if (0 != (inParams.accessFlags & ETextureAccessFlags::RTV))
	{
		vkRTV = colorImageView;
		rtv = std::make_unique<VulkanRenderTargetView>(vkRTV);
	}
	if (0 != (inParams.accessFlags & ETextureAccessFlags::UAV))
	{
		vkUAV = colorImageView;
		uav = std::make_unique<VulkanUnorderedAccessView>(vkUAV);
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
	uint64 slicePitch)
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

uint32 VulkanTexture::getSRVDescriptorIndex() const
{
	// #todo-vulkan
	return 0;
}

uint32 VulkanTexture::getRTVDescriptorIndex() const
{
	// #todo-vulkan
	return 0;
}

uint32 VulkanTexture::getDSVDescriptorIndex() const
{
	// #todo-vulkan
	return 0;
}

uint32 VulkanTexture::getUAVDescriptorIndex() const
{
	// #todo-vulkan
	return 0;
}

#endif // COMPILE_BACKEND_VULKAN
