#pragma once

#include "render/shader.h"
#include "d3d_util.h"
#include <Windows.h>

class D3DShader : public Shader
{
	
public:
	virtual void loadVertexShader(const TCHAR* filename, const char* entryPoint) override;
	virtual void loadPixelShader(const TCHAR* filename, const char* entryPoint) override;

	D3D12_SHADER_BYTECODE getBytecode(EShaderType shaderType);

private:
	void loadFromFile(const TCHAR* filename, const char* entryPoint, EShaderType shaderType);

	WRL::ComPtr<ID3DBlob> byteCodes[static_cast<int>(EShaderType::NUM_TYPES)];

};
