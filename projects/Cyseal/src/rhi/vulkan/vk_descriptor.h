#pragma once

#include "vk_device.h"
#include "rhi/gpu_resource_binding.h"
#include "util/string_conversion.h"

class VulkanDescriptorPool : public DescriptorHeap
{
public:
	VulkanDescriptorPool(const DescriptorHeapDesc& inDesc);
	~VulkanDescriptorPool();

	virtual void setDebugName(const wchar_t* debugNameW);

	void initialize(VulkanDevice* inDevice);

	inline VkDescriptorPool getVkPool() const { return vkPool; }

private:
	VulkanDevice* device = nullptr;
	VkDescriptorPool vkPool = VK_NULL_HANDLE;
};
