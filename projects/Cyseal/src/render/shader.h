#pragma once

#include "core/int_types.h"
#include <string>

class RenderDevice;

enum class EShaderStage : uint8
{
	VERTEX_SHADER          = 0, // Traditional rasterization stages
	DOMAIN_SHADER          = 1,
	HULL_SHADER            = 2,
	GEOMETRY_SHADER        = 3,
	PIXEL_SHADER           = 4,
	COMPUTE_SHADER         = 5, // Compute stage
	MESH_SHADER            = 6, // #todo-shader: Modern shader stages
	AMPLICATION_SHADER     = 7,
	RT_INTERSECTION_SHADER = 8,
	RT_ANYHIT_SHADER       = 9,
	RT_CLOSESTHIT_SHADER   = 10,
	RT_MISS_SHADER         = 11,
	NUM_TYPES              = 12
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

protected:
	EShaderStage stageFlag;
	std::string debugName;
};
