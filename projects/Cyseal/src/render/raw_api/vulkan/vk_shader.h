#pragma once

#include "vk_device.h"

#if COMPILE_BACKEND_VULKAN

#include "core/int_types.h"
#include "render/shader.h"
#include <vector>

class VulkanShaderStage : public ShaderStage
{
public:
	VulkanShaderStage(EShaderStage inStageFlag, const char* inDebugName);
	~VulkanShaderStage();

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint) override;

	VkShaderModule getVkShaderModule() const { return vkModule; }
	VkShaderStageFlagBits getVkShaderStage() const { return vkShaderStage; }
	const char* getEntryPoint() const { return entryPointName.c_str(); }

private:
	std::vector<char> sourceCode;
	std::string entryPointName;
	VkShaderModule vkModule = VK_NULL_HANDLE;
	VkShaderStageFlagBits vkShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
};

#endif // COMPILE_BACKEND_VULKAN
