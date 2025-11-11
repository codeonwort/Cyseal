#include "vk_descriptor.h"
#include "vk_into.h"

VulkanDescriptorPool::VulkanDescriptorPool(const DescriptorHeapDesc& desc)
	: DescriptorHeap(desc)
{
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
	CHECK(device != nullptr && vkPool != nullptr);

	// There's no API to destroy a VkDescriptorSet; we just destroy its owner pool.
	if (getCreateParams().purpose == EDescriptorHeapPurpose::Persistent)
	{
		vkDestroyDescriptorSetLayout(device->getRaw(), vkDescriptorSetLayoutGlobal, nullptr);
		vkDescriptorSetGlobal = VK_NULL_HANDLE;
	}
	else if (getCreateParams().purpose == EDescriptorHeapPurpose::Volatile)
	{
		volDescriptorSetCache.clear();
	}

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

	// #todo-vulkan: For now, allocate as many as 'numDescriptors' for each type...
	// so there are more allocations than what happens in d3d descriptor heap.
	// #todo-vulkan: Watch out for VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorPoolSize.html
	switch (desc.type)
	{
	case EDescriptorHeapType::CBV:
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, desc.numDescriptors });
		break;
	case EDescriptorHeapType::SRV:
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, desc.numDescriptors });
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, desc.numDescriptors });
		break;
	case EDescriptorHeapType::UAV:
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, desc.numDescriptors });
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, desc.numDescriptors });
		break;
	case EDescriptorHeapType::CBV_SRV_UAV:
		// #todo-vulkan: D3D12 backend needs this type as it can only bind two heaps. (CbvSrvUav heap + Sampler heap)
		// But there is no such type in VkDescriptorType.
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, desc.numDescriptors });
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, desc.numDescriptors });
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, desc.numDescriptors });
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, desc.numDescriptors });
		break;
	case EDescriptorHeapType::SAMPLER:
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, desc.numDescriptors });
		break;
	case EDescriptorHeapType::RTV:
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, desc.numDescriptors });
		break;
	case EDescriptorHeapType::DSV:
		poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, desc.numDescriptors });
		break;
	case EDescriptorHeapType::NUM_TYPES:
		CHECK_NO_ENTRY();
		break;
	default:
		CHECK_NO_ENTRY();
		break;
	}

	VkDescriptorPoolCreateInfo createInfo{
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext         = nullptr,
		.flags         = (VkDescriptorPoolCreateFlagBits)0,
		.maxSets       = 32, // #wip-set: maxSets? usually swapchain count is enough but suballocated cbuffers might need more...
		.poolSizeCount = (uint32_t)poolSizes.size(),
		.pPoolSizes    = poolSizes.data(),
	};

	VkResult vkRet = vkCreateDescriptorPool(vkDevice, &createInfo, nullptr, &vkPool);
	CHECK(vkRet == VK_SUCCESS);

	if (desc.purpose == EDescriptorHeapPurpose::Persistent)
	{
		std::vector<VkDescriptorSetLayoutBinding> vkBindings(poolSizes.size());
		for (size_t i = 0; i < vkBindings.size(); ++i)
		{
			vkBindings[i] = VkDescriptorSetLayoutBinding{
				.binding            = (uint32)i, // #wip-pool
				.descriptorType     = poolSizes[i].type,
				.descriptorCount    = poolSizes[i].descriptorCount,
				.stageFlags         = VK_SHADER_STAGE_ALL, // #wip-pool
				.pImmutableSamplers = nullptr, // #wip-pool
			};
		}
		VkDescriptorSetLayoutCreateInfo layoutInfo{
			.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext        = nullptr,
			.flags        = (VkDescriptorSetLayoutCreateFlagBits)0,
			.bindingCount = (uint32)vkBindings.size(),
			.pBindings    = vkBindings.data(),
		};
		VkResult vkRet = vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr, &vkDescriptorSetLayoutGlobal);
		CHECK(vkRet == VK_SUCCESS);

		VkDescriptorSetAllocateInfo setInfo{
			.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext              = nullptr,
			.descriptorPool     = vkPool,
			.descriptorSetCount = 1,
			.pSetLayouts        = &vkDescriptorSetLayoutGlobal,
		};
		vkRet = vkAllocateDescriptorSets(vkDevice, &setInfo, &vkDescriptorSetGlobal);
		CHECK(vkRet == VK_SUCCESS);
	}
}

uint32 VulkanDescriptorPool::getDescriptorBindingIndex(VkDescriptorType descriptorType) const
{
	for (size_t i = 0; i < poolSizes.size(); ++i)
	{
		if (poolSizes[i].type == descriptorType)
		{
			return (uint32)i;
		}
	}
	CHECK_NO_ENTRY();
	return 0xffffffff;
}

const std::vector<VkDescriptorSet>* VulkanDescriptorPool::findCachedDescriptorSets(PipelineState* pipeline, uint32 generation) const
{
	CHECK(getCreateParams().purpose == EDescriptorHeapPurpose::Volatile);

	auto it = volDescriptorSetCache.find(pipeline);
	if (it == volDescriptorSetCache.end())
	{
		return nullptr;
	}
	else if (it->second.generations.size() <= generation)
	{
		return nullptr;
	}
	return &(it->second.generations[generation]);
}

const std::vector<VkDescriptorSet>* VulkanDescriptorPool::createDescriptorSets(PipelineState* pipeline, uint32 generation, const std::vector<VkDescriptorSetLayout>& layouts)
{
	CHECK(getCreateParams().purpose == EDescriptorHeapPurpose::Volatile);

	// #wip-param: 'layouts' can be acquired from 'pipeline'...
	// But currently only VulkanComputePipelineState provides such public method.

	VkDevice vkDevice = device->getRaw();

	VkDescriptorSetAllocateInfo allocInfo{
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext              = nullptr,
		.descriptorPool     = vkPool,
		.descriptorSetCount = (uint32)layouts.size(),
		.pSetLayouts        = layouts.data(),
	};

	std::vector<VkDescriptorSet> sets(layouts.size(), VK_NULL_HANDLE);
	VkResult vkRet = vkAllocateDescriptorSets(vkDevice, &allocInfo, sets.data());
	CHECK(vkRet == VK_SUCCESS);

	const std::vector<VkDescriptorSet>* pSets = nullptr;

	auto it = volDescriptorSetCache.find(pipeline);
	if (it == volDescriptorSetCache.end())
	{
		DescriptorSetGeneration gen;
		gen.generations.emplace_back(sets);
		auto itit = volDescriptorSetCache.insert({ pipeline, std::move(gen) });
		CHECK(itit.second == true);

		pSets = &(itit.first->second.generations[generation]);
	}
	else
	{
		auto& generations = it->second.generations;
		CHECK(generations.size() == generation);

		generations.emplace_back(std::move(sets));
		pSets = &(generations[generations.size() - 1]);
	}

	return pSets;
}
