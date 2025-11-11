#pragma once

#include "vk_device.h"
#include "rhi/gpu_resource_binding.h"
#include "util/string_conversion.h"

#include <map>

class PipelineState;

class VulkanDescriptorPool : public DescriptorHeap
{
public:
	VulkanDescriptorPool(const DescriptorHeapDesc& inDesc);
	~VulkanDescriptorPool();

	virtual void setDebugName(const wchar_t* debugNameW);

	void initialize(VulkanDevice* inDevice);

	inline VkDescriptorPool getVkPool() const { return vkPool; }
	uint32 getDescriptorBindingIndex(VkDescriptorType descriptorType) const;

// Persistent pool only.
public:
	inline VkDescriptorSet getVkDescriptorSetGlobal() const { return vkDescriptorSetGlobal; }

// Volatile pool only.
public:
	// Returns nullptr if not found for the given pipeline.
	const std::vector<VkDescriptorSet>* findCachedDescriptorSets(PipelineState* pipeline) const;
	const std::vector<VkDescriptorSet>* createDescriptorSets(PipelineState* pipeline, const std::vector<VkDescriptorSetLayout>& layouts);

private:
	VulkanDevice* device = nullptr;
	VkDescriptorPool vkPool = VK_NULL_HANDLE;

	std::vector<VkDescriptorPoolSize> poolSizes; // element index = binding index in shader

// Persistent pool only.
private:
	VkDescriptorSetLayout vkDescriptorSetLayoutGlobal = VK_NULL_HANDLE;
	VkDescriptorSet vkDescriptorSetGlobal = VK_NULL_HANDLE;

// Volatile pool only.
private:
	std::map<PipelineState*, std::vector<VkDescriptorSet>> volDescriptorSetCache;
};
