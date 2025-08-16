#include "vk_shader.h"

// #todo-vulkan: Runtime shader recompilation, maybe using this?
// https://github.com/KhronosGroup/SPIRV-Tools

#if COMPILE_BACKEND_VULKAN

#include "core/platform.h"
#include "core/assertion.h"
#include "util/resource_finder.h"
#include "util/string_conversion.h"
#include "vk_into.h"
#include <fstream>

const char* shaderTypeStrings[] = {
	"vert",
	"tese",
	"tesc",
	"geom",
	"frag",
	"comp",
	nullptr, // mesh
	nullptr, // amp
	nullptr, // intersection
	nullptr, // anyhit
	nullptr, // closesthit
	nullptr, // miss
};

VulkanShaderStage::VulkanShaderStage(EShaderStage inStageFlag, const char* inDebugName)
	: ShaderStage(inStageFlag, inDebugName)
{
	vkShaderStage = into_vk::shaderStage(inStageFlag);
}

VulkanShaderStage::~VulkanShaderStage()
{
	CHECK(vkModule != VK_NULL_HANDLE);

	VulkanDevice* device = static_cast<VulkanDevice*>(gRenderDevice);
	vkDestroyShaderModule(device->getRaw(), vkModule, nullptr);
}

void VulkanShaderStage::loadFromFile(const wchar_t* inFilename, const char* inEntryPoint, std::initializer_list<std::wstring> defines)
{
	aEntryPoint = inEntryPoint;
	str_to_wstr(inEntryPoint, wEntryPoint);

	std::wstring hlslPathW = ResourceFinder::get().find(inFilename);
	CHECK(hlslPathW.size() > 0);
	std::string hlslPath;
	wstr_to_str(hlslPathW, hlslPath);

#if PLATFORM_WINDOWS
	// Hmm... glslangValidator also works for HLSL?
	// https://github.com/KhronosGroup/glslang/wiki/HLSL-FAQ
	
	const char* glslang = "%VULKAN_SDK%\\Bin\\glslangValidator.exe";
	const char* shaderTypeStr = shaderTypeStrings[(int)stageFlag];
	std::string spirvPath = hlslPath.substr(0, hlslPath.find(".hlsl")) + ".spv";
	char cmd[512];
	sprintf_s(cmd, "%s -S %s -e %s -o %s -V -D %s",
		glslang, shaderTypeStr, inEntryPoint, spirvPath.c_str(), hlslPath.c_str());
	std::system(cmd);
#else
	#error Not implemented yet
#endif

	std::ifstream file(spirvPath, std::ios::ate | std::ios::binary);
	CHECK(file.is_open());

	size_t fileSize = static_cast<size_t>(file.tellg());
	sourceCode.resize(fileSize);

	file.seekg(0);
	file.read(sourceCode.data(), fileSize);
	file.close();

	//////////////////////////////////////////////////////////////////////////

	VulkanDevice* device = static_cast<VulkanDevice*>(gRenderDevice);

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = sourceCode.size();
	createInfo.pCode = reinterpret_cast<const uint32*>(sourceCode.data());

	VkResult ret = vkCreateShaderModule(
		device->getRaw(),
		&createInfo,
		nullptr,
		&vkModule);
	CHECK(ret == VK_SUCCESS);
}

#endif // COMPILE_BACKEND_VULKAN
