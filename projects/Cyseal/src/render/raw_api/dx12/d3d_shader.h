#pragma once

#include "render/shader.h"
#include "d3d_util.h"
#include <d3dcommon.h>

class D3DShaderStage : public ShaderStage
{
public:
	D3DShaderStage(EShaderStage inStageFlag, const char* inDebugName)
		: ShaderStage(inStageFlag, inDebugName)
	{}

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint) override;

	D3D12_SHADER_BYTECODE getBytecode() const
	{
		D3D12_SHADER_BYTECODE bc;
		bc.pShaderBytecode = bytecodeBlob->GetBufferPointer();
		bc.BytecodeLength = bytecodeBlob->GetBufferSize();
		return bc;
	}

private:
	WRL::ComPtr<ID3DBlob> bytecodeBlob;
};
