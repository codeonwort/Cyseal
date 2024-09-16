#pragma once

#include "rhi/shader.h"
#include "d3d_util.h"
#include <d3dcommon.h>

#include <vector>
#include <string>

struct D3DShaderParameter
{
	std::string name;
	//D3D_SHADER_INPUT_TYPE type;
	uint32 registerSlot;
	uint32 registerSpace;
};

struct D3DShaderParameterTable
{
	std::vector<D3DShaderParameter> constantBuffers;
	std::vector<D3DShaderParameter> rwStructuredBuffers;
	std::vector<D3DShaderParameter> rwBuffers;
	std::vector<D3DShaderParameter> structuredBuffers;
	std::vector<D3DShaderParameter> textures;
	std::vector<D3DShaderParameter> samplers;
};

class D3DShaderStage : public ShaderStage
{
public:
	D3DShaderStage(EShaderStage inStageFlag, const char* inDebugName)
		: ShaderStage(inStageFlag, inDebugName)
	{}

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint) override;

	D3D12_SHADER_BYTECODE getBytecode() const;

	virtual const wchar_t* getEntryPointW() override { return wEntryPoint.c_str(); }
	virtual const char* getEntryPointA() override { return aEntryPoint.c_str(); }

private:
	void readShaderReflection(IDxcResult* compileResult);

private:
	bool bInitialized = false;
	WRL::ComPtr<IDxcBlob> bytecodeBlob;
	std::wstring wEntryPoint;
	std::string aEntryPoint;

	D3DShaderParameterTable parameterTable;

	// Compute shader only
	uint32 threadGroupTotalSize = 0;
	uint32 threadGroupSizeX = 0;
	uint32 threadGroupSizeY = 0;
	uint32 threadGroupSizeZ = 0;
};
