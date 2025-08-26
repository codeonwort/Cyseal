#pragma once

#include "vk_device.h"

#if COMPILE_BACKEND_VULKAN

#include "core/int_types.h"
#include "rhi/shader.h"
#include <vector>

class VulkanShaderStage : public ShaderStage
{
public:
	VulkanShaderStage(EShaderStage inStageFlag, const char* inDebugName);
	~VulkanShaderStage();

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint, std::initializer_list<std::wstring> defines) override;

	virtual const wchar_t* getEntryPointW() override { return wEntryPoint.c_str(); }
	virtual const char* getEntryPointA() override { return aEntryPoint.c_str(); }

	VkShaderModule getVkShaderModule() const { return vkModule; }
	VkShaderStageFlagBits getVkShaderStage() const { return vkShaderStage; }

private:
	std::vector<char> sourceCode;
	std::string aEntryPoint;
	std::wstring wEntryPoint;

	VkShaderModule vkModule = VK_NULL_HANDLE;
	VkShaderStageFlagBits vkShaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
};

#endif // COMPILE_BACKEND_VULKAN
