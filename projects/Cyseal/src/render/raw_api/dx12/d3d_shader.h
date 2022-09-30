#pragma once

#include "render/shader.h"
#include "d3d_util.h"
#include <d3dcommon.h>

class D3DShader : public Shader
{
public:
	virtual void loadVertexShader(const wchar_t* filename, const char* entryPoint) override;
	virtual void loadPixelShader(const wchar_t* filename, const char* entryPoint) override;

	virtual ShaderStage* getVertexShader() override { return vsStage; }
	virtual ShaderStage* getPixelShader() override { return psStage; }

	D3D12_SHADER_BYTECODE getBytecode(EShaderStage shaderType);

private:
	void loadFromFile(const TCHAR* filename, const char* entryPoint, EShaderStage shaderType);

	ShaderStage* vsStage = nullptr;
	ShaderStage* psStage = nullptr;
	ShaderStage* dsStage = nullptr;
	ShaderStage* hsStage = nullptr;
	ShaderStage* gsStage = nullptr;
	WRL::ComPtr<ID3DBlob> byteCodes[static_cast<int>(EShaderStage::NUM_TYPES)];
};

class D3DShaderStage : public ShaderStage
{
public:
	D3DShaderStage(D3DShader* inShader, EShaderStage inType)
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
	EShaderStage type;
};
