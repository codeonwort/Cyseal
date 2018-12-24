#pragma once

#include <stdint.h>
#include <Windows.h>

enum class EShaderType : uint8_t
{
	VERTEX_SHADER   = 0,
	DOMAIN_SHADER   = 1,
	HULL_SHADER     = 2,
	GEOMETRY_SHADER = 3,
	PIXEL_SHADER    = 4,
	COMPUTE_SHADER  = 5,
	NUM_TYPES       = 6
};

class Shader
{
	
public:
	virtual void loadVertexShader(const TCHAR* filename, const char* entryPoint) = 0;
	virtual void loadPixelShader(const TCHAR* filename, const char* entryPoint) = 0;

};
