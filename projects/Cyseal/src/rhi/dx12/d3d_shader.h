#pragma once

#include "rhi/shader.h"
#include "d3d_util.h"
#include <d3dcommon.h>

#include <vector>
#include <string>
#include <map>

class D3DRootSignature;

struct D3DShaderParameter
{
	// Read from shader reflection.
	std::string name;
	//D3D_SHADER_INPUT_TYPE type;
	uint32 registerSlot;
	uint32 registerSpace;

	// Allocated when generating root signature (except for samplers).
	uint32 rootParameterIndex;
};

struct D3DShaderParameterTable
{
	std::vector<D3DShaderParameter> rootConstants;
	std::vector<D3DShaderParameter> constantBuffers;
	std::vector<D3DShaderParameter> rwStructuredBuffers;
	std::vector<D3DShaderParameter> rwBuffers;
	std::vector<D3DShaderParameter> structuredBuffers;
	std::vector<D3DShaderParameter> textures;
	std::vector<D3DShaderParameter> samplers;

	inline size_t totalRootConstants() const
	{
		return rootConstants.size();
	}
	inline size_t totalBuffers() const
	{
		return constantBuffers.size() + rwStructuredBuffers.size() + rwBuffers.size() + structuredBuffers.size();
	}
	inline size_t totalTextures() const
	{
		return textures.size();
	}
};

class D3DShaderStage : public ShaderStage
{
public:
	D3DShaderStage(EShaderStage inStageFlag, const char* inDebugName)
		: ShaderStage(inStageFlag, inDebugName)
	{}

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint) override;

	virtual const wchar_t* getEntryPointW() override { return wEntryPoint.c_str(); }
	virtual const char* getEntryPointA() override { return aEntryPoint.c_str(); }

	D3D12_SHADER_BYTECODE getBytecode() const;
	inline const D3DShaderParameterTable& getParameterTable() const { return parameterTable; }

private:
	void readShaderReflection(IDxcResult* compileResult);

private:
	bool bInitialized = false;
	WRL::ComPtr<IDxcBlob> bytecodeBlob;
	std::wstring wEntryPoint;
	std::string aEntryPoint;

	D3DShaderParameterTable parameterTable; // Filled by shader reflection

	// Compute shader only
	uint32 threadGroupTotalSize = 0;
	uint32 threadGroupSizeX = 0;
	uint32 threadGroupSizeY = 0;
	uint32 threadGroupSizeZ = 0;
};
