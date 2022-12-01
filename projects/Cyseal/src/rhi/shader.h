#pragma once

#include "core/int_types.h"
#include <string>

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

class ShaderStage
{
public:
	ShaderStage(EShaderStage inStageFlag, const char* inDebugName)
		: stageFlag(inStageFlag)
		, debugName(inDebugName)
	{}
	virtual ~ShaderStage() = default;

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint) = 0;

	virtual const wchar_t* getEntryPointW() = 0;
	virtual const char* getEntryPointA() = 0;

protected:
	EShaderStage stageFlag;
	std::string debugName;
};
