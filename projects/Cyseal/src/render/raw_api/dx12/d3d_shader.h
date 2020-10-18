#pragma once

#include "render/shader.h"
#include "d3d_util.h"
#include <Windows.h>

class D3DShader : public Shader
{
public:
	virtual void loadVertexShader(const TCHAR* filename, const char* entryPoint) override;
	virtual void loadPixelShader(const TCHAR* filename, const char* entryPoint) override;

	virtual ShaderStage* getVertexShader() override { return vsStage; }
	virtual ShaderStage* getPixelShader() override { return psStage; }

	D3D12_SHADER_BYTECODE getBytecode(EShaderType shaderType);

private:
	void loadFromFile(const TCHAR* filename, const char* entryPoint, EShaderType shaderType);

	ShaderStage* vsStage = nullptr;
	ShaderStage* psStage = nullptr;
	ShaderStage* dsStage = nullptr;
	ShaderStage* hsStage = nullptr;
	ShaderStage* gsStage = nullptr;
	WRL::ComPtr<ID3DBlob> byteCodes[static_cast<int>(EShaderType::NUM_TYPES)];
};

class D3DShaderStage : public ShaderStage
{
public:
	D3DShaderStage(D3DShader* inShader, EShaderType inType)
		: shader(inShader)
		, type(inType)
	{
	}

	D3D12_SHADER_BYTECODE getBytecode() const
	{
		return shader->getBytecode(type);
	}

private:
	D3DShader* shader;
	EShaderType type;
};
