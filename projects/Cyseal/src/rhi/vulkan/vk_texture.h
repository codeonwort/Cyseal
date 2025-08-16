#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_resource_view.h"
#include "rhi/texture.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

class VulkanTexture : public Texture
{
public:
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

private:
	VkImage vkImage = VK_NULL_HANDLE;

	// #todo-vulkan: Implement custom large block manager or integrate VMA.
	// Separate VkDeviceMemory per texture for now...
	VkDeviceMemory vkImageMemory = VK_NULL_HANDLE;

	TextureCreateParams createParams;
};

#endif // COMPILE_BACKEND_VULKAN
