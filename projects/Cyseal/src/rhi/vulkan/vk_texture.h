#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_resource_view.h"
#include "rhi/texture.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

class VulkanDevice;

class VulkanTexture : public Texture
{
public:
	VulkanTexture(VulkanDevice* inDevice) : device(inDevice) {}
	~VulkanTexture();

	void initialize(const TextureCreateParams& params);

	virtual void* getRawResource() const override { return vkImage; }

	virtual const TextureCreateParams& getCreateParams() const override { return createParams; }

	virtual void uploadData(
		RenderCommandList& commandList,
		const void* buffer,
		uint64 rowPitch,
		uint64 slicePitch,
		uint32 subresourceIndex = 0) override;

	virtual void setDebugName(const wchar_t* debugName) override;

	// #wip: Support vulkan texture readback.
#if 0
	virtual uint64 getRowPitch() const override { return rowPitch; }

	virtual uint64 getReadbackBufferSize() const override { return readbackBufferSize; }

	virtual bool prepareReadback(RenderCommandList* commandList) override;

	virtual bool readbackData(void* dst) override;
#endif

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
};

#endif // COMPILE_BACKEND_VULKAN
