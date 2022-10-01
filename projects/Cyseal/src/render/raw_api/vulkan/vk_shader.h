#pragma once

#include "vk_device.h"

#if COMPILE_BACKEND_VULKAN

#include "core/int_types.h"
#include "render/shader.h"
#include <vector>


class VulkanShader : public Shader
{
public:
	VulkanShader() = default;
	~VulkanShader();

	virtual void loadVertexShader(const wchar_t* filename, const char* entryPoint) override;
	virtual void loadPixelShader(const wchar_t* filename, const char* entryPoint) override;

	virtual ShaderStage* getVertexShader() override;
	virtual ShaderStage* getPixelShader() override;

private:
	void loadFromFile(const wchar_t* filename, std::vector<char>& outCode);
	VkShaderModule createShaderModule(const std::vector<char>& code);

	std::vector<char> vsCode;
	std::vector<char> fsCode;
	VkShaderModule vsModule;
	VkShaderModule fsModule;
};

#endif // COMPILE_BACKEND_VULKAN
