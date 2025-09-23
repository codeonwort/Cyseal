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

			std::string codegen = ShaderCodegen::get().hlslToSpirv(
				filepath.c_str(), "mainCS",
				EShaderStage::COMPUTE_SHADER, { L"WRITE_PASS" }
			);
			Assert::IsTrue(codegen.size() > 0);

			codegen = ShaderCodegen::get().hlslToSpirv(
				filepath.c_str(), "mainCS",
				EShaderStage::COMPUTE_SHADER, { L"READ_PASS" }
			);
			Assert::IsTrue(codegen.size() > 0);
		}
	};
}
