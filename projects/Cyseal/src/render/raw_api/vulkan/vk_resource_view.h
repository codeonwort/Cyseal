#pragma once

#if COMPILE_BACKEND_VULKAN

#include "render/resource_view.h"
#include <vulkan/vulkan_core.h>

class VulkanRenderTargetView : public RenderTargetView
{
public:
	VulkanRenderTargetView(VkImageView inHandle) : handle(inHandle) {}
	VkImageView getRaw() const { return handle; }
private:
	VkImageView handle = VK_NULL_HANDLE;
};

class VulkanDepthStencilView : public DepthStencilView
{
public:
	VulkanDepthStencilView(VkImageView inHandle) : handle(inHandle) {}
	VkImageView getRaw() const { return handle; }
private:
	VkImageView handle = VK_NULL_HANDLE;
};

class VulkanShaderResourceView : public ShaderResourceView
{
public:
	VulkanShaderResourceView(Texture* inOwner, VkImageView inHandle)
		: ShaderResourceView(inOwner)
		, handle(inHandle)
	{}
	VkImageView getRaw() const { return handle; }
private:
	VkImageView handle = VK_NULL_HANDLE;
};

class VulkanUnorderedAccessView : public UnorderedAccessView
{
public:
	VulkanUnorderedAccessView(VkImageView inHandle) : handle(inHandle) {}
	VkImageView getRaw() const { return handle; }
private:
	VkImageView handle = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
