#pragma once

#include "core/int_types.h"
#include "core/assertion.h"
#include <string>
#include <vector>

class RenderDevice;

// VkShaderStageFlagBits
enum class EShaderStage : uint8
{
	// Rasterization pipeline
	VERTEX_SHADER          = 0,
	HULL_SHADER            = 1, // TCS(Tessellation Control Shader)
	DOMAIN_SHADER          = 2, // TES(Tessellation Evaluation Shader)
	GEOMETRY_SHADER        = 3,
	PIXEL_SHADER           = 4,

	// Compute pipeline
	COMPUTE_SHADER         = 5,

	// Mesh Shader pipeline
	MESH_SHADER            = 6,
	AMPLICATION_SHADER     = 7,

	// Raytracing pipeline
	RT_RAYGEN_SHADER       = 8,
	RT_ANYHIT_SHADER       = 9,
	RT_CLOSESTHIT_SHADER   = 10,
	RT_MISS_SHADER         = 11,
	RT_INTERSECTION_SHADER = 12,

	NUM_TYPES              = 13
};

inline bool isRaytracingShader(EShaderStage shaderStage)
{
	switch (shaderStage)
	{
	case EShaderStage::VERTEX_SHADER:
	case EShaderStage::HULL_SHADER:
	case EShaderStage::DOMAIN_SHADER:
	case EShaderStage::GEOMETRY_SHADER:
	case EShaderStage::PIXEL_SHADER:
	case EShaderStage::COMPUTE_SHADER:
	case EShaderStage::MESH_SHADER:
	case EShaderStage::AMPLICATION_SHADER:
		return false;
	case EShaderStage::RT_RAYGEN_SHADER:
	case EShaderStage::RT_ANYHIT_SHADER:
	case EShaderStage::RT_CLOSESTHIT_SHADER:
	case EShaderStage::RT_MISS_SHADER:
	case EShaderStage::RT_INTERSECTION_SHADER:
		return true;
	default:
		CHECK_NO_ENTRY();
	}
	return false;
}

class ShaderStage
{
	using PushConstantDecls = std::vector<std::pair<std::string, int32>>;
public:
	ShaderStage(EShaderStage inStageFlag, const char* inDebugName)
		: stageFlag(inStageFlag)
		, debugName(inDebugName)
	{}
	virtual ~ShaderStage() = default;

	// Invoke before loadFromFile().
	// Need to pre-determine before shader compilation as shader reflection can't discriminate between root constants and CBVs.
	// @param inPushConstantDecls { { "name_0", num32BitValues_0 }, { "name_1", num32BitValues_1 }, ... }
	inline void declarePushConstants(const PushConstantDecls& inPushConstantDecls)
	{
		CHECK(!bPushConstantsDeclared);
		pushConstantDecls = inPushConstantDecls;
		bPushConstantsDeclared = true;

		for (const auto& decl : inPushConstantDecls)
		{
			CHECK(decl.second > 0);
		}
	}
	// Use this when this shader has no push constants.
	inline void declarePushConstants()
	{
		CHECK(!bPushConstantsDeclared);
		pushConstantDecls.clear();
		bPushConstantsDeclared = true;
	}

	inline bool isPushConstantsDeclared() const { return bPushConstantsDeclared; }

	virtual void loadFromFile(const wchar_t* inFilename, const char* entryPoint, std::initializer_list<std::wstring> defines = {}) = 0;

	virtual const wchar_t* getEntryPointW() = 0;
	virtual const char* getEntryPointA() = 0;

protected:
	inline bool shouldBePushConstants(const std::string& name, int32* num32BitValues)
	{
		for (const auto& decl : pushConstantDecls)
		{
			if (decl.first == name)
			{
				*num32BitValues = decl.second;
				return true;
			}
		}
		*num32BitValues = -1;
		return false;
	}

protected:
	EShaderStage stageFlag;
	std::string debugName;

	PushConstantDecls pushConstantDecls;
	bool bPushConstantsDeclared = false;
};
