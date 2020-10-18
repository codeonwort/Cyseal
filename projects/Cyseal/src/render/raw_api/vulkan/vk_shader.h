#pragma once

#include "vk_device.h"
#include "core/int_types.h"
#include "render/shader.h"
#include <vector>


class VulkanShader : public Shader
{
public:
	VulkanShader() = default;
	~VulkanShader();

	virtual void loadVertexShader(const TCHAR* filename, const char* entryPoint) override;
	virtual void loadPixelShader(const TCHAR* filename, const char* entryPoint) override;

	virtual ShaderStage* getVertexShader() override;
	virtual ShaderStage* getPixelShader() override;

private:
	void loadFromFile(const TCHAR* filename, std::vector<char>& outCode);
	VkShaderModule createShaderModule(const std::vector<char>& code);

	std::vector<char> vsCode;
	std::vector<char> fsCode;
	VkShaderModule vsModule;
	VkShaderModule fsModule;
};