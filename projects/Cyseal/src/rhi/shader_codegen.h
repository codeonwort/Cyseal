#pragma once

#include "shader_dxc_common.h"

#include <string>
#include <vector>

// Temp util with absurd design because I'm lazy and tired.
class ShaderCodegen
{
public:
	static ShaderCodegen& get()
	{
		static ShaderCodegen inst;
		return inst;
	}

	std::string hlslToSpirv(
		const char* inFilename,
		const char* inEntryPoint,
		EShaderStage stageFlag,
		std::initializer_list<std::wstring> defines);

private:
	ShaderCodegen();

	std::string readProcessOutput(const std::string& cmd);

private:
	std::string dxcPath;
};
