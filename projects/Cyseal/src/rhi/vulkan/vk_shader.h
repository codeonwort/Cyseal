#pragma once

#include "vk_device.h"

#if COMPILE_BACKEND_VULKAN

#include "core/int_types.h"
#include "rhi/shader.h"
#include <vector>

// #wip: VulkanShaderReflection
struct VulkanShaderReflection
{
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

	void moveVkDescriptorSetLayouts(std::vector<VkDescriptorSetLayout>& target);
	void moveVkPushConstantRanges(std::vector<VkPushConstantRange>& target);

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

	// Native resources, but ownership might be lost and get emptied.
	std::vector<VkDescriptorSetLayout> vkDescriptorSetLayouts;
	std::vector<VkPushConstantRange> vkPushConstantRanges;
};

#endif // COMPILE_BACKEND_VULKAN
