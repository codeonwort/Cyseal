#pragma once

#include "vk_device.h"

#if COMPILE_BACKEND_VULKAN

#include "core/int_types.h"
#include "render/shader.h"
#include <vector>

class VulkanShaderStage : public ShaderStage
{
public:
	VulkanShaderStage(EShaderStage inStageFlag, const char* inDebugName)
		: ShaderStage(inStageFlag, inDebugName)
	{}
	~VulkanShaderStage();

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint) override;

private:
	std::vector<char> sourceCode;
	VkShaderModule vkModule = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
