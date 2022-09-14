#include "vk_shader.h"

#if COMPILE_BACKEND_VULKAN

#include "core/assertion.h"
#include "util/resource_finder.h"
#include <fstream>

VulkanShader::~VulkanShader()
{
	VulkanDevice* device = static_cast<VulkanDevice*>(gRenderDevice);
	vkDestroyShaderModule(device->getRaw(), vsModule, nullptr);
	vkDestroyShaderModule(device->getRaw(), fsModule, nullptr);
}

void VulkanShader::loadVertexShader(const TCHAR* filename, const char* entryPoint)
{
	loadFromFile(filename, vsCode);
	vsModule = createShaderModule(vsCode);
}

void VulkanShader::loadPixelShader(const TCHAR* filename, const char* entryPoint)
{
	loadFromFile(filename, fsCode);
	fsModule = createShaderModule(fsCode);
}

ShaderStage* VulkanShader::getVertexShader()
{
	// #todo-vulkan: shader stage
	return nullptr;
}

ShaderStage* VulkanShader::getPixelShader()
{
	// #todo-vulkan: shader stage
	return nullptr;
}

void VulkanShader::loadFromFile(const TCHAR* filename, std::vector<char>& outCode)
{
	auto filenameEx = ResourceFinder::get().find(filename);

	std::ifstream file(filenameEx, std::ios::ate | std::ios::binary);
	CHECK(file.is_open());

	size_t fileSize = static_cast<size_t>(file.tellg());
	outCode.resize(fileSize);

	file.seekg(0);
	file.read(outCode.data(), fileSize);
	file.close();
}

VkShaderModule VulkanShader::createShaderModule(const std::vector<char>& code)
{
	VulkanDevice* device = static_cast<VulkanDevice*>(gRenderDevice);

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32*>(code.data());

	VkShaderModule shaderModule;
	VkResult ret = vkCreateShaderModule(device->getRaw(), &createInfo, nullptr, &shaderModule);
	CHECK(ret == VK_SUCCESS);

	return shaderModule;
}

#endif // COMPILE_BACKEND_VULKAN
