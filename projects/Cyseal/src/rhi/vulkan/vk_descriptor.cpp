#include "vk_descriptor.h"
#include "vk_into.h"

VulkanDescriptorPool::VulkanDescriptorPool(const DescriptorHeapDesc& desc)
	: DescriptorHeap(desc)
{
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
	CHECK(device != nullptr && vkPool != nullptr);
	vkDestroyDescriptorPool(device->getRaw(), vkPool, nullptr);
}

void VulkanDescriptorPool::setDebugName(const wchar_t* debugNameW)
{
	std::string debugNameA;
	wstr_to_str(debugNameW, debugNameA);

	device->setObjectDebugName(
		VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT,
		(uint64)vkPool,
		debugNameA.c_str());
}

void VulkanDescriptorPool::initialize(VulkanDevice* inDevice)
{
	CHECK(device == nullptr && vkPool == nullptr);
	device = inDevice;

	VkDevice vkDevice = device->getRaw();
	const DescriptorHeapDesc& desc = getCreateParams();

	std::vector<VkDescriptorPoolSize> poolSizes;
	if (desc.type == EDescriptorHeapType::CBV_SRV_UAV)
	{
		// #todo-vulkan: For now, allocate 3x times than requested...
		poolSizes.emplace_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, desc.numDescriptors }); // CBV
		poolSizes.emplace_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, desc.numDescriptors });  // SRV
		poolSizes.emplace_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, desc.numDescriptors });  // UAV
	}
	else
	{
		// #todo-vulkan: Watch out for VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK
		// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorPoolSize.html
		poolSizes.emplace_back(VkDescriptorPoolSize{ into_vk::descriptorPoolType(desc.type), desc.numDescriptors });
	}

	VkDescriptorPoolCreateInfo createInfo{
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext         = nullptr,
		.flags         = (VkDescriptorPoolCreateFlagBits)0,
		.maxSets       = 1, // #wip: maxSets of VkDescriptorPoolCreateInfo
		.poolSizeCount = (uint32_t)poolSizes.size(),
		.pPoolSizes    = poolSizes.data(),
	};

	VkResult vkRet = vkCreateDescriptorPool(vkDevice, &createInfo, nullptr, &vkPool);
	CHECK(vkRet == VK_SUCCESS);
}
