#pragma once

#include "core/int_types.h"
#include <string>

enum class EShaderStage : uint8_t
{
	VERTEX_SHADER   = 0,
	DOMAIN_SHADER   = 1,
	HULL_SHADER     = 2,
	GEOMETRY_SHADER = 3,
	PIXEL_SHADER    = 4,
	COMPUTE_SHADER  = 5,
	NUM_TYPES       = 6
};

// #todo-shader: Dummy which could be removed in future.
class ShaderStage
{
};

class Shader
{
public:
	virtual ~Shader() = default;

	virtual void loadVertexShader(const wchar_t* filename, const char* entryPoint) = 0;
	virtual void loadPixelShader(const wchar_t* filename, const char* entryPoint) = 0;

	virtual ShaderStage* getVertexShader() = 0;
	virtual ShaderStage* getPixelShader() = 0;
};
