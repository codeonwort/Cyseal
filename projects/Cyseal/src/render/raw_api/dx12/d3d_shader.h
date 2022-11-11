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

	D3D12_SHADER_BYTECODE getBytecode() const;

	const wchar_t* getEntryPoint() const { return wEntryPoint.c_str(); }

private:
	bool bInitialized = false;
	WRL::ComPtr<IDxcBlob> bytecodeBlob;
	std::wstring wEntryPoint;
};
