#pragma once

#include "rhi/shader.h"
#include "d3d_util.h"

#include <d3dcommon.h>
#include <dxcapi.h>
#include <d3d12shader.h>

#include <vector>
#include <string>
#include <map>

class D3DDevice;

struct D3DShaderParameter
{
	// Read from shader reflection.
	std::string name;
	D3D_SHADER_INPUT_TYPE type;
	uint32 registerSlot;
	uint32 registerSpace;
	uint32 numDescriptors; // Hack: This is num32BitValues for pushConstants, as specified by ShaderStage::pushConstantDecls.

	// Allocated when generating root signature (except for samplers).
	uint32 rootParameterIndex = 0xffffffff;

	inline bool hasSameReflection(const D3DShaderParameter& rhs) const
	{
		return this->name == rhs.name
			&& this->type == rhs.type
			&& this->registerSlot == rhs.registerSlot
			&& this->registerSpace == rhs.registerSpace
			&& this->numDescriptors == rhs.numDescriptors;
	}
};

struct D3DShaderParameterTable
{
	std::vector<D3DShaderParameter> rootConstants;
	std::vector<D3DShaderParameter> constantBuffers;
	std::vector<D3DShaderParameter> rwStructuredBuffers;
	std::vector<D3DShaderParameter> rwBuffers;
	std::vector<D3DShaderParameter> structuredBuffers;
	std::vector<D3DShaderParameter> byteAddressBuffers;
	std::vector<D3DShaderParameter> textures;
	std::vector<D3DShaderParameter> samplers;
	std::vector<D3DShaderParameter> accelerationStructures;

	inline size_t totalRootConstants() const { return rootConstants.size(); }
	inline size_t totalBuffers() const { return constantBuffers.size() + rwStructuredBuffers.size() + rwBuffers.size() + structuredBuffers.size() + byteAddressBuffers.size(); }
	inline size_t totalTextures() const { return textures.size(); }
	inline size_t totalAccelerationStructures() const { return accelerationStructures.size(); }
};

class D3DShaderStage : public ShaderStage
{
public:
	D3DShaderStage(D3DDevice* inDevice, EShaderStage inStageFlag, const char* inDebugName)
		: ShaderStage(inStageFlag, inDebugName)
		, device(inDevice)
	{}

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint, std::initializer_list<std::wstring> defines) override;

	virtual const wchar_t* getEntryPointW() override { return wEntryPoint.c_str(); }
	virtual const char* getEntryPointA() override { return aEntryPoint.c_str(); }

	D3D12_SHADER_BYTECODE getBytecode() const;
	inline const D3DShaderParameterTable& getParameterTable() const { return parameterTable; }

private:
	void readShaderReflection(IDxcResult* compileResult);
	void addToShaderParameterTable(const D3D12_SHADER_INPUT_BIND_DESC& inputBindDesc);

private:
	D3DDevice* device = nullptr;

	bool bInitialized = false;
	WRL::ComPtr<IDxcBlob> bytecodeBlob;
	std::wstring wEntryPoint;
	std::string aEntryPoint;

	D3DShaderParameterTable parameterTable; // Filled by shader reflection

	// e.g., cs_6_6
	// https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/ns-d3d12shader-d3d12_shader_desc
	D3D12_SHADER_VERSION_TYPE programType = D3D12_SHVER_RESERVED0;
	uint32 programMajorVersion = 0;
	uint32 programMinorVersion = 0;

	// Compute shader only
	uint32 threadGroupTotalSize = 0;
	uint32 threadGroupSizeX = 0;
	uint32 threadGroupSizeY = 0;
	uint32 threadGroupSizeZ = 0;
};
