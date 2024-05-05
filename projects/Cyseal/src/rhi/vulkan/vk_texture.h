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
	virtual ShaderResourceView* getSRV() const override;
	virtual DepthStencilView* getDSV() const override;

	virtual uint32 getSRVDescriptorIndex() const override { return srvDescriptorIndex; }
	virtual uint32 getRTVDescriptorIndex() const override { return rtvDescriptorIndex; }
	virtual uint32 getDSVDescriptorIndex() const override { return dsvDescriptorIndex; }

	virtual DescriptorHeap* getSourceSRVHeap() const override { return srvHeap; }
	virtual DescriptorHeap* getSourceRTVHeap() const override { return rtvHeap; }
	virtual DescriptorHeap* getSourceDSVHeap() const override { return dsvHeap; }

private:
	VkImage vkImage = VK_NULL_HANDLE;

	// #todo-vulkan: Implement custom large block manager or integrate VMA.
	// Separate VkDeviceMemory per texture for now...
	VkDeviceMemory vkImageMemory = VK_NULL_HANDLE;

	VkImageView vkSRV = VK_NULL_HANDLE;
	VkImageView vkRTV = VK_NULL_HANDLE;
	VkImageView vkDSV = VK_NULL_HANDLE;
	uint32 srvDescriptorIndex = 0xffffffff;
	uint32 rtvDescriptorIndex = 0xffffffff;
	uint32 dsvDescriptorIndex = 0xffffffff;

	std::unique_ptr<VulkanRenderTargetView> rtv;
	std::unique_ptr<ShaderResourceView> srv;
	std::unique_ptr<VulkanDepthStencilView> dsv;

	// Source descriptor heaps from which this texture allocated its descriptors.
	DescriptorHeap* srvHeap = nullptr;
	DescriptorHeap* rtvHeap = nullptr;
	DescriptorHeap* dsvHeap = nullptr;

	TextureCreateParams createParams;
};

#endif // COMPILE_BACKEND_VULKAN
