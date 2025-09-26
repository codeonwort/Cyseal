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

	/// <summary>
	/// 
	/// </summary>
	/// <param name="bEmitBytecode">Controls what to generate between SPIR-V assembly and bytecode.</param>
	/// <param name="inFilename">Full path to a .hlsl file.</param>
	/// <param name="inEntryPoint">Shader entry point.</param>
	/// <param name="stageFlag">Shader stage flag.</param>
	/// <param name="defines">preprocessor macros.</param>
	/// <returns>SPIR-V assembly or bytecode.</returns>
	std::string hlslToSpirv(
		bool bEmitBytecode,
		const char* inFilename,
		const char* inEntryPoint,
		EShaderStage stageFlag,
		std::initializer_list<std::wstring> defines);

private:
	ShaderCodegen();

	std::string readProcessOutput(const std::string& cmd, bool bEmitBytecode);

private:
	std::string dxcPath;
};
