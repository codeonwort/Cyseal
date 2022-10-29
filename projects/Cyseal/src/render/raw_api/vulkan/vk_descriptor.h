#pragma once

#include "vk_device.h"
#include "render/resource_binding.h"
#include "util/string_conversion.h"

class VulkanDescriptorPool : public DescriptorHeap
{
public:
	VulkanDescriptorPool(const DescriptorHeapDesc& inDesc, VkDescriptorPool inPool)
		: DescriptorHeap(inDesc)
		, vkPool(inPool)
	{
	}
	~VulkanDescriptorPool()
	{
		VkDevice vkDevice = static_cast<VulkanDevice*>(gRenderDevice)->getRaw();
		vkDestroyDescriptorPool(vkDevice, vkPool, nullptr);
	}

	virtual void setDebugName(const wchar_t* debugNameW)
	{
		std::string debugNameA;
		wstr_to_str(debugNameW, debugNameA);

		static_cast<VulkanDevice*>(gRenderDevice)->setObjectDebugName(
			VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT,
			(uint64)vkPool,
			debugNameA.c_str());
	}

private:
	VkDescriptorPool vkPool;
};
