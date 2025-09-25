#pragma once

#include "vk_device.h"

#if COMPILE_BACKEND_VULKAN

#include "core/int_types.h"
#include "rhi/shader.h"
#include <vector>

struct VulkanShaderReflection
{
	// #wip-vk
};

class VulkanShaderStage : public ShaderStage
{
public:
	VulkanShaderStage(VulkanDevice* inDevice, EShaderStage inStageFlag, const char* inDebugName);
	~VulkanShaderStage();

	virtual void loadFromFile(const wchar_t* inFilename, const char* inEntryPoint, std::initializer_list<std::wstring> defines) override;

	virtual const wchar_t* getEntryPointW() override { return wEntryPoint.c_str(); }
	virtual const char* getEntryPointA() override { return aEntryPoint.c_str(); }

	const VulkanShaderReflection& getShaderReflection() { return shaderReflection; }

	VkShaderModule getVkShaderModule() const { return vkModule; }
	VkShaderStageFlagBits getVkShaderStage() const { return vkShaderStage; }
	const std::vector<VkDescriptorSetLayout>& getVkDescriptorSetLayouts() const { return vkDescriptorSetLayouts; }
	const std::vector<VkPushConstantRange>& getVkPushConstantRanges() const { return vkPushConstantRanges; }

private:
	void loadFromFileByGlslangValidator(const wchar_t* inFilename, const char* inEntryPoint, std::initializer_list<std::wstring> defines);
	void loadFromFileByDxc(const wchar_t* inFilename, const char* inEntryPoint, std::initializer_list<std::wstring> defines);

	void readShaderReflection(const void* spirv_code, size_t spirv_nbytes);

private:
	VulkanDevice* device = nullptr;

	std::vector<char> sourceCode;
	std::string aEntryPoint;
	std::wstring wEntryPoint;

	VulkanShaderReflection shaderReflection;

	// Native resources
	VkShaderModule vkModule = VK_NULL_HANDLE;
	VkShaderStageFlagBits vkShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	std::vector<VkDescriptorSetLayout> vkDescriptorSetLayouts; // #wip-vk
	std::vector<VkPushConstantRange> vkPushConstantRanges; // #wip-vk
};

#endif // COMPILE_BACKEND_VULKAN
