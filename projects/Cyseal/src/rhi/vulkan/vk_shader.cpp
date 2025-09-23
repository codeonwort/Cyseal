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
#include <sstream>

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

VulkanShaderStage::VulkanShaderStage(VulkanDevice* inDevice, EShaderStage inStageFlag, const char* inDebugName)
	: ShaderStage(inStageFlag, inDebugName)
	, device(inDevice)
{
	vkShaderStage = into_vk::shaderStage(inStageFlag);
}

VulkanShaderStage::~VulkanShaderStage()
{
	CHECK(vkModule != VK_NULL_HANDLE);

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

	std::stringstream ss;
	ss << glslang;
	ss << " -S " << shaderTypeStr;
	ss << " -e " << inEntryPoint;
	// #wip: Maybe hold as in-memory string?
	// This is gonna be a problem if I compile multiple shaders from a single source file.
	ss << " -o " << spirvPath.c_str();
	for (const std::wstring& defW : defines)
	{
		std::string def;
		wstr_to_str(defW, def);
		ss << " -D" << def;
	}
	ss << " -V -D " << hlslPath.c_str(); // -V: create SPIR-V binary, -D: input is HLSL.
	
	std::string cmd = ss.str();
	std::system(cmd.c_str());
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
