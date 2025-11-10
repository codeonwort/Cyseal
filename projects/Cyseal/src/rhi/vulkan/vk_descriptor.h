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
	uint32 getDescriptorBindingIndex(VkDescriptorType descriptorType) const;

	inline VkDescriptorSet getVkDescriptorSetGlobal() const { return vkDescriptorSetGlobal; }

private:
	VulkanDevice* device = nullptr;
	VkDescriptorPool vkPool = VK_NULL_HANDLE;

	std::vector<VkDescriptorPoolSize> poolSizes; // element index = binding index in shader

	// Allocated only if this pool is persistent.
	VkDescriptorSetLayout vkDescriptorSetLayoutGlobal = VK_NULL_HANDLE;
	VkDescriptorSet vkDescriptorSetGlobal = VK_NULL_HANDLE;
};
