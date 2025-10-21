#pragma once

#include "shader_common.h"
#include "core/int_types.h"
#include "core/assertion.h"
#include <string>
#include <vector>

class ShaderStage
{
	// { { "name_0", num32BitValues_0 }, { "name_1", num32BitValues_1 }, ... }
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
			auto num32BitValues = decl.second;
			if (num32BitValues <= 0)
			{
				CHECK_NO_ENTRY();
			}
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
