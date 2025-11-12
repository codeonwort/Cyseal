#include "vk_shader.h"
#include "spirv_reflect.h"

// #todo-vulkan: Runtime shader recompilation, maybe using this?
// https://github.com/KhronosGroup/SPIRV-Tools

// 1 = Use dxc to convert HLSL to SPIR-V. 0 = Use glslangValidator.
#define USE_DXC 1

#if COMPILE_BACKEND_VULKAN

#if USE_DXC
	#include "rhi/shader_codegen.h"
#endif
#include "core/platform.h"
#include "core/assertion.h"
#include "util/resource_finder.h"
#include "util/string_conversion.h"
#include "vk_into.h"
#include <fstream>
#include <sstream>
#include <vector>

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

static VkDescriptorType descriptorTypeSpvToVk(SpvReflectDescriptorType inType)
{
	return (VkDescriptorType)inType; // Enum values are same; just cast it
}

VulkanShaderStage::VulkanShaderStage(VulkanDevice* inDevice, EShaderStage inStageFlag, const char* inDebugName)
	: ShaderStage(inStageFlag, inDebugName)
	, device(inDevice)
{
	vkShaderStage = into_vk::shaderStage(inStageFlag);
}

VulkanShaderStage::~VulkanShaderStage()
{
	CHECK(vkModule != VK_NULL_HANDLE);

	VkDevice vkDevice = device->getRaw();
	vkDestroyShaderModule(vkDevice, vkModule, nullptr);

	// Arrays might be already empty by moveVkDescriptorSetLayouts().
	for (VkDescriptorSetLayout layout : vkDescriptorSetLayouts)
	{
		vkDestroyDescriptorSetLayout(vkDevice, layout, nullptr);
	}
}

void VulkanShaderStage::loadFromFile(const wchar_t* inFilename, const char* inEntryPoint, std::initializer_list<std::wstring> defines)
{
#if USE_DXC
	loadFromFileByDxc(inFilename, inEntryPoint, defines);
#else
	loadFromFileByGlslangValidator(inFilename, inEntryPoint, defines);
#endif

	readShaderReflection(sourceCode.data(), sourceCode.size());
}

void VulkanShaderStage::moveVkDescriptorSetLayouts(std::vector<VkDescriptorSetLayout>& target)
{
	target = vkDescriptorSetLayouts;
	vkDescriptorSetLayouts.clear();
}

void VulkanShaderStage::loadFromFileByGlslangValidator(const wchar_t* inFilename, const char* inEntryPoint, std::initializer_list<std::wstring> defines)
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
	// #todo-barrier-vk: Maybe hold as in-memory string?
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

void VulkanShaderStage::loadFromFileByDxc(const wchar_t* inFilename, const char* inEntryPoint, std::initializer_list<std::wstring> defines)
{
	aEntryPoint = inEntryPoint;
	str_to_wstr(inEntryPoint, wEntryPoint);

	std::wstring hlslPathW = ResourceFinder::get().find(inFilename);
	CHECK(hlslPathW.size() > 0);
	std::string hlslPath;
	wstr_to_str(hlslPathW, hlslPath);

	std::string codegen = ShaderCodegen::get().hlslToSpirv(true, hlslPath.c_str(), inEntryPoint, stageFlag, defines);
	CHECK(codegen.size() > 0);
	sourceCode.assign(codegen.begin(), codegen.end());

	VkShaderModuleCreateInfo createInfo{
		.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext    = nullptr,
		.flags    = (VkShaderModuleCreateFlags)0,
		.codeSize = sourceCode.size(),
		.pCode    = reinterpret_cast<const uint32*>(sourceCode.data()),
	};

	VkResult ret = vkCreateShaderModule(device->getRaw(), &createInfo, nullptr, &vkModule);
	CHECK(ret == VK_SUCCESS);
}

void VulkanShaderStage::readShaderReflection(const void* spirv_code, size_t spirv_nbytes)
{
	SpvReflectShaderModule module;
	SpvReflectResult result = spvReflectCreateShaderModule(spirv_nbytes, spirv_code, &module);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	// #todo-vulkan-reflection: Process inputVar and outputVar if needed.
#if 0
	// Input variables
	{
		uint32 varCount = 0;
		result = spvReflectEnumerateInputVariables(&module, &varCount, NULL);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);
		std::vector<SpvReflectInterfaceVariable*> inputVars(varCount, nullptr);
		result = spvReflectEnumerateInputVariables(&module, &varCount, inputVars.data());
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		for (uint32 i = 0; i < varCount; ++i)
		{
			SpvReflectInterfaceVariable* inputVar = inputVars[i];
			// ...
		}
	}
	// Output variables
	{
		uint32 varCount = 0;
		result = spvReflectEnumerateOutputVariables(&module, &varCount, NULL);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);
		std::vector<SpvReflectInterfaceVariable*> outputVars(varCount, nullptr);
		result = spvReflectEnumerateOutputVariables(&module, &varCount, outputVars.data());
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		for (uint32 i = 0; i < varCount; ++i)
		{
			SpvReflectInterfaceVariable* outputVar = outputVars[i];
			// ...
		}
	}
#endif

	// Push constants
	{
		uint32 pushConstCount = 0;
		result = spvReflectEnumeratePushConstantBlocks(&module, &pushConstCount, NULL);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);
		std::vector<SpvReflectBlockVariable*> spvPushConstants(pushConstCount, nullptr);
		result = spvReflectEnumeratePushConstantBlocks(&module, &pushConstCount, spvPushConstants.data());
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		for (uint32 i = 0; i < pushConstCount; ++i)
		{
			SpvReflectBlockVariable* spvPushConst = spvPushConstants[i];

			VkPushConstantRange range{
				.stageFlags = static_cast<VkShaderStageFlags>(vkShaderStage),
				.offset     = spvPushConst->offset,
				.size       = spvPushConst->size,
			};

			VulkanPushConstantParameter param{
				.name  = spvPushConst->name,
				.range = std::move(range),
			};
			parameterTable.pushConstants.emplace_back(param);
		}
	}
	// Descriptor bindings
	{
		uint32 count = 0;
		result = spvReflectEnumerateDescriptorBindings(&module, &count, NULL);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);
		std::vector<SpvReflectDescriptorBinding*> bindings(count, nullptr);
		result = spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		for (uint32 i = 0; i < count; ++i)
		{
			SpvReflectDescriptorBinding* binding = bindings[i];

			VulkanShaderParameter param{
				.name             = binding->name,
				.vkDescriptorType = descriptorTypeSpvToVk(binding->descriptor_type),
				.set              = binding->set,
				.binding          = binding->binding,
				.numDescriptors   = binding->count,
			};
			addToShaderParameterTable(param);
		}
	}
	// Descriptor sets (only create set layouts here; actual sets are allocated later)
	{
		uint32 setCount = 0;
		result = spvReflectEnumerateDescriptorSets(&module, &setCount, NULL);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);
		std::vector<SpvReflectDescriptorSet*> sets(setCount, nullptr);
		result = spvReflectEnumerateDescriptorSets(&module, &setCount, sets.data());
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		std::vector<uint32> setIndicesValidation(setCount, 0xffffffff);

		for (uint32 i = 0; i < setCount; ++i)
		{
			SpvReflectDescriptorSet* spvSet = sets[i];
			setIndicesValidation[i] = spvSet->set;
			
			std::vector<VkDescriptorSetLayoutBinding> vkBindings(spvSet->binding_count);
			for (uint32 j = 0; j < spvSet->binding_count; ++j)
			{
				SpvReflectDescriptorBinding* spvBinding = spvSet->bindings[j];
				vkBindings[j] = VkDescriptorSetLayoutBinding{
					.binding            = spvBinding->binding,
					.descriptorType     = static_cast<VkDescriptorType>(spvBinding->descriptor_type),
					.descriptorCount    = spvBinding->count,
					.stageFlags         = static_cast<VkShaderStageFlags>(vkShaderStage),
					.pImmutableSamplers = nullptr, // #wip: pImmutableSamplers
				};
			}

			VkDescriptorSetLayoutCreateInfo createInfo{
				.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.pNext        = nullptr,
				.flags        = (VkDescriptorSetLayoutCreateFlagBits)0,
				.bindingCount = (uint32)vkBindings.size(),
				.pBindings    = vkBindings.data(),
			};
			VkDescriptorSetLayout vkSetLayout = VK_NULL_HANDLE;

			VkResult vkRet = vkCreateDescriptorSetLayout(
				device->getRaw(), &createInfo, nullptr, &vkSetLayout);
			CHECK(vkRet == VK_SUCCESS);

			vkDescriptorSetLayouts.push_back(vkSetLayout);
		}

		for (uint32 i = 0; i < setCount; ++i)
		{
			// Other logics assume firstSet=0 and consecutive set indices...
			CHECK(setIndicesValidation[i] == i);
		}
	}

	// Destroy the reflection data when no longer required.
	spvReflectDestroyShaderModule(&module);
}

void VulkanShaderStage::addToShaderParameterTable(const VulkanShaderParameter& inParam)
{
	// #todo-shader-reflection: Fill the table for new cases. Also need to modify:
	// 1. The definition of VulkanShaderParameterTable struct.
	// 2. createShaderParameterHashMap() in vk_pipeline_state.cpp.
	switch (inParam.vkDescriptorType)
	{
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			parameterTable.sampledImages.emplace_back(inParam);
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			parameterTable.storageImages.emplace_back(inParam);
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			parameterTable.storageBuffers.emplace_back(inParam);
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_TENSOR_ARM:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
			CHECK_NO_ENTRY();
			break;
		case VK_DESCRIPTOR_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_NV:
			CHECK_NO_ENTRY();
			break;
		default:
			CHECK_NO_ENTRY();
			break;
	}
}

#endif // COMPILE_BACKEND_VULKAN
