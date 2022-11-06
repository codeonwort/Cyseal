#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_resource_view.h"
#include "render/gpu_resource.h"
#include "render/texture.h"

#include <vulkan/vulkan_core.h>

class VulkanTexture : public Texture
{
public:
	~VulkanTexture();

	void initialize(const TextureCreateParams& params);

	virtual void uploadData(
		RenderCommandList& commandList,
		const void* buffer,
		uint64 rowPitch,
		uint64 slicePitch) override;

	virtual RenderTargetView* getRTV() const override { return rtv.get(); }
	virtual ShaderResourceView* getSRV() const override { return srv.get(); }
	virtual DepthStencilView* getDSV() const override { return dsv.get(); }

	virtual void setDebugName(const wchar_t* debugName) override;

	virtual uint32 getSRVDescriptorIndex() const override;
	virtual uint32 getRTVDescriptorIndex() const override;
	virtual uint32 getDSVDescriptorIndex() const override;
	virtual uint32 getUAVDescriptorIndex() const override;

private:
	VkImage vkImage = VK_NULL_HANDLE;

	// #todo-vulkan: Implement custom large block manager or integrate VMA.
	// Separate VkDeviceMemory per texture for now...
	VkDeviceMemory vkImageMemory = VK_NULL_HANDLE;

	std::unique_ptr<VulkanRenderTargetView> rtv;
	std::unique_ptr<VulkanShaderResourceView> srv;
	std::unique_ptr<VulkanDepthStencilView> dsv;
	std::unique_ptr<VulkanUnorderedAccessView> uav;
	VkImageView vkSRV = VK_NULL_HANDLE;
	VkImageView vkRTV = VK_NULL_HANDLE;
	VkImageView vkUAV = VK_NULL_HANDLE;
	VkImageView vkDSV = VK_NULL_HANDLE;

	TextureCreateParams createParams;
};

#endif // COMPILE_BACKEND_VULKAN
