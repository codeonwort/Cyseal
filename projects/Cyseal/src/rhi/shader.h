#pragma once

#include "core/int_types.h"
#include "core/assertion.h"
#include <string>
#include <vector>

class RenderDevice;

// VkShaderStageFlagBits
enum class EShaderStage : uint8
{
	// Rasterization pipeline
	VERTEX_SHADER          = 0,
	HULL_SHADER            = 1, // TCS(Tessellation Control Shader)
	DOMAIN_SHADER          = 2, // TES(Tessellation Evaluation Shader)
	GEOMETRY_SHADER        = 3,
	PIXEL_SHADER           = 4,

	// Compute pipeline
	COMPUTE_SHADER         = 5,

	// Mesh Shader pipeline
	MESH_SHADER            = 6,
	AMPLICATION_SHADER     = 7,

	// Raytracing pipeline
	RT_RAYGEN_SHADER       = 8,
	RT_ANYHIT_SHADER       = 9,
	RT_CLOSESTHIT_SHADER   = 10,
	RT_MISS_SHADER         = 11,
	RT_INTERSECTION_SHADER = 12,

	NUM_TYPES              = 13
};

inline bool isRaytracingShader(EShaderStage shaderStage)
{
	switch (shaderStage)
	{
	case EShaderStage::VERTEX_SHADER:
	case EShaderStage::HULL_SHADER:
	case EShaderStage::DOMAIN_SHADER:
	case EShaderStage::GEOMETRY_SHADER:
	case EShaderStage::PIXEL_SHADER:
	case EShaderStage::COMPUTE_SHADER:
	case EShaderStage::MESH_SHADER:
	case EShaderStage::AMPLICATION_SHADER:
		return false;
	case EShaderStage::RT_RAYGEN_SHADER:
	case EShaderStage::RT_ANYHIT_SHADER:
	case EShaderStage::RT_CLOSESTHIT_SHADER:
	case EShaderStage::RT_MISS_SHADER:
	case EShaderStage::RT_INTERSECTION_SHADER:
		return true;
	default:
		CHECK_NO_ENTRY();
	}
	return false;
}

class ShaderStage
{
public:
	ShaderStage(EShaderStage inStageFlag, const char* inDebugName)
		: stageFlag(inStageFlag)
		, debugName(inDebugName)
	{}
	virtual ~ShaderStage() = default;

	// Invoke before loadFromFile().
	// Need to pre-determine before shader compilation as shader reflection can't discriminate between root constants and CBVs.
	inline void declarePushConstants(const std::vector<std::string>& inPushConstantNames)
	{
		CHECK(!bPushConstantsDeclared);
		pushConstantNames = inPushConstantNames;
		bPushConstantsDeclared = true;
	}
	inline void declarePushConstants()
	{
		CHECK(!bPushConstantsDeclared);
		pushConstantNames.clear();
		bPushConstantsDeclared = true;
	}

	inline bool isPushConstantsDeclared() const { return bPushConstantsDeclared; }

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint) = 0;

	virtual const wchar_t* getEntryPointW() = 0;
	virtual const char* getEntryPointA() = 0;

protected:
	inline bool shouldBePushConstants(const std::string& name) { return std::find(pushConstantNames.begin(), pushConstantNames.end(), name) != pushConstantNames.end(); }

protected:
	EShaderStage stageFlag;
	std::string debugName;

	std::vector<std::string> pushConstantNames;
	bool bPushConstantsDeclared = false;
};
