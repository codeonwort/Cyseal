#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "rhi/shader_codegen.h"

#define TEST_SHADERS_DIR "../../projects/UnitTest/src/shader/"

namespace UnitTest
{
	TEST_CLASS(TestShaderCodegen)
	{
	public:
		TEST_METHOD(HlslToSpirv)
		{
			std::string filepath = TEST_SHADERS_DIR;
			filepath += "codegen_test.hlsl";

			for (int32 i = 0; i < 2; ++i)
			{
				bool bEmitBytecode = (i == 0);

				std::string codegen = ShaderCodegen::get().hlslToSpirv(
					bEmitBytecode,
					filepath.c_str(), "mainCS",
					EShaderStage::COMPUTE_SHADER, { L"WRITE_PASS" }
				);
				Assert::IsTrue(codegen.size() > 0);

				codegen = ShaderCodegen::get().hlslToSpirv(
					bEmitBytecode,
					filepath.c_str(), "mainCS",
					EShaderStage::COMPUTE_SHADER, { L"READ_PASS" }
				);
				Assert::IsTrue(codegen.size() > 0);
			}
		}
	};
}
