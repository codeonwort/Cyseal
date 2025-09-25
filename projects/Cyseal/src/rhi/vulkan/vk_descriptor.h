#pragma once

#include "vk_device.h"
#include "rhi/gpu_resource_binding.h"
#include "util/string_conversion.h"

class VulkanDescriptorPool : public DescriptorHeap
{
public:
	VulkanDescriptorPool(VulkanDevice* inDevice, const DescriptorHeapDesc& inDesc, VkDescriptorPool inPool)
		: DescriptorHeap(inDesc)
		, device(inDevice)
		, vkPool(inPool)
	{
	}
	~VulkanDescriptorPool()
	{
		vkDestroyDescriptorPool(device->getRaw(), vkPool, nullptr);
	}

	virtual void setDebugName(const wchar_t* debugNameW)
	{
		std::string debugNameA;
		wstr_to_str(debugNameW, debugNameA);

		device->setObjectDebugName(
			VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT,
			(uint64)vkPool,
			debugNameA.c_str());
	}

	inline VkDescriptorPool getVkPool() const { return vkPool; }

private:
	VulkanDevice* device = nullptr;
	VkDescriptorPool vkPool = VK_NULL_HANDLE;
};
