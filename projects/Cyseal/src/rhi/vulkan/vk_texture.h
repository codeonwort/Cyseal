#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_resource_view.h"
#include "rhi/texture.h"
#include "core/smart_pointer.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

class VulkanDevice;

class VulkanTexture : public Texture
{
public:
	VulkanTexture(VulkanDevice* inDevice);
	~VulkanTexture();

	void initialize(const TextureCreateParams& params);

	//~ BEGIN GPUResource
	virtual void setDebugName(const wchar_t* debugName) override;

	virtual void* getRawResource() const override { return vkImage; }
	//~ END GPUResource

	//~ BEGIN Texture
	virtual const TextureCreateParams& getCreateParams() const override { return createParams; }

	virtual void uploadData(
		RenderCommandList* commandList,
		const void* buffer,
		uint64 rowPitch,
		uint64 slicePitch,
		uint32 subresourceIndex = 0) override;

	virtual uint64 getRowPitch() const override { return rowPitch; }

	virtual SharedPtr<ReadbackHandle> requestReadback(RenderCommandList* commandList, const ReadbackRegion& region) override;

	void internal_finalizeReadbackBuffer();
	//~ END Texture

private:
	VulkanDevice* device = nullptr;
	VkImage vkImage = VK_NULL_HANDLE;
	VkBuffer vkUploadBuffer = VK_NULL_HANDLE;
	VkBuffer vkReadbackBuffer = VK_NULL_HANDLE;

	// #todo-vulkan: Implement custom large block manager or integrate VMA.
	// Separate VkDeviceMemory per texture for now...
	VkDeviceMemory vkImageMemory = VK_NULL_HANDLE;
	VkDeviceMemory vkUploadMemory = VK_NULL_HANDLE;
	VkDeviceMemory vkReadbackMemory = VK_NULL_HANDLE;

	TextureCreateParams createParams;
	VkDeviceSize allocSize = 0;
	uint64 rowPitch = 0;
	VkDeviceSize rowPitchAlignment = 1;

	WeakPtr<Texture::ReadbackHandle> readbackHandle;
};

#endif // COMPILE_BACKEND_VULKAN
