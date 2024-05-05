#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_resource_view.h"
#include "rhi/gpu_resource.h"

#include <vulkan/vulkan_core.h>
#include <memory>

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

	virtual RenderTargetView* getRTV() const override;

	virtual uint32 getRTVDescriptorIndex() const override { return rtvDescriptorIndex; }

	virtual DescriptorHeap* getSourceRTVHeap() const override { return rtvHeap; }

private:
	VkImage vkImage = VK_NULL_HANDLE;

	// #todo-vulkan: Implement custom large block manager or integrate VMA.
	// Separate VkDeviceMemory per texture for now...
	VkDeviceMemory vkImageMemory = VK_NULL_HANDLE;

	VkImageView vkRTV = VK_NULL_HANDLE;
	uint32 rtvDescriptorIndex = 0xffffffff;

	std::unique_ptr<VulkanRenderTargetView> rtv;

	// Source descriptor heaps from which this texture allocated its descriptors.
	DescriptorHeap* rtvHeap = nullptr;

	TextureCreateParams createParams;
};

#endif // COMPILE_BACKEND_VULKAN
