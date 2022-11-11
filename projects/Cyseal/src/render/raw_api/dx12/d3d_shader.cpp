#include "d3d_shader.h"
#include "d3d_util.h"
#include "d3d_device.h"
#include "util/resource_finder.h"
#include "util/logging.h"
#include "util/string_conversion.h"
#include "core/assertion.h"
#include <filesystem>

// References
// https://github.com/microsoft/DirectXShaderCompiler/wiki/Using-dxc.exe-and-dxcompiler.dll
// https://asawicki.info/news_1719_two_shader_compilers_of_direct3d_12
// https://simoncoenen.com/blog/programming/graphics/DxcCompiling

// #todo-dx12: DXC options
#define SKIP_SHADER_OPTIMIZATION (_DEBUG)

DEFINE_LOG_CATEGORY_STATIC(LogD3DShader);

static std::wstring getSolutionDirectory()
{
	static std::wstring solutionDir;
	if (solutionDir.size() == 0)
	{
		std::filesystem::path currentDir = std::filesystem::current_path();
		int count = 64;
		while (count --> 0)
		{
			auto sln = currentDir;
			sln.append("CysealSolution.sln");
			if (std::filesystem::exists(sln))
			{
				solutionDir = currentDir.wstring() + L"/";
				break;
			}
			currentDir = currentDir.parent_path();
		}
		CHECK(count >= 0); // Couldn't find shader directory
	}
	return solutionDir;
}
static std::wstring getShaderDirectory()
{
	static std::wstring shaderDir;
	if (shaderDir.size() == 0)
	{
		shaderDir = getSolutionDirectory() + L"shaders/";
		shaderDir = std::filesystem::path(shaderDir).make_preferred().wstring();
	}
	return shaderDir;
}

// #todo-wip-dxc: Should return something that matches with D3DDevice::highestShaderModel
static const wchar_t* getD3DShaderType(EShaderStage type)
{
	switch (type)
	{
		case EShaderStage::VERTEX_SHADER: return L"vs_6_6";
		case EShaderStage::DOMAIN_SHADER: return L"ds_6_6";
		case EShaderStage::HULL_SHADER: return L"hs_6_6";
		case EShaderStage::GEOMETRY_SHADER: return L"gs_6_6";
		case EShaderStage::PIXEL_SHADER: return L"ps_6_6";
		case EShaderStage::COMPUTE_SHADER: return L"cs_6_6";
		default: CHECK_NO_ENTRY();
	}
	return L"unknown";
}

void D3DShaderStage::loadFromFile(const wchar_t* inFilename, const char* entryPoint)
{
	IDxcLibrary* library = getD3DDevice()->getDxcLibrary();
	IDxcCompiler3* compiler = getD3DDevice()->getDxcCompiler();
	IDxcIncludeHandler* includeHandler = getD3DDevice()->getDxcIncludeHandler();

	std::wstring fullpath = ResourceFinder::get().find(inFilename);
	if (fullpath.size() == 0)
	{
		CYLOG(LogD3DShader, Fatal, L"Failed to find shader: %s", fullpath.c_str());
		CHECK_NO_ENTRY();
	}

	uint32 codePage = CP_UTF8;
	WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
	HRESULT hr = library->CreateBlobFromFile(fullpath.c_str(), &codePage, &sourceBlob);
	if (FAILED(hr))
	{
		CYLOG(LogD3DShader, Fatal, L"Failed to create blob from: %s", fullpath.c_str());
		CHECK_NO_ENTRY();
	}

	std::wstring shaderDir = getShaderDirectory();
	std::wstring wEntryPoint;
	str_to_wstr(entryPoint, wEntryPoint);

	std::vector<LPCWSTR> arguments;
#if SKIP_SHADER_OPTIMIZATION
	//arguments.push_back(DXC_ARG_DEBUG);
#endif
	// Include directory
	arguments.push_back(L"-I");
	arguments.push_back(shaderDir.c_str());
	// Entry point
	arguments.push_back(L"-E");
	arguments.push_back(wEntryPoint.c_str());
	// Target profile
	arguments.push_back(L"-T");
	arguments.push_back(getD3DShaderType(stageFlag));
	
	DxcBuffer sourceBuffer;
	sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
	sourceBuffer.Size = sourceBlob->GetBufferSize();
	sourceBuffer.Encoding = 0;

	WRL::ComPtr<IDxcResult> compileResult;
	// #todo-wip-dxc: Huh? hlsl::Exception? Anyway the application runs fine.
	hr = compiler->Compile(
		&sourceBuffer,
		arguments.data(), (uint32)arguments.size(),
		includeHandler,
		IID_PPV_ARGS(&compileResult));

	if (SUCCEEDED(hr))
	{
		HR(compileResult->GetStatus(&hr));
	}

	if (FAILED(hr) && compileResult)
	{
		WRL::ComPtr<IDxcBlobEncoding> errorBlob;
		hr = compileResult->GetErrorBuffer(&errorBlob);
		if (SUCCEEDED(hr) && errorBlob)
		{
			const char* msg = (const char*)errorBlob->GetBufferPointer();
			CYLOG(LogD3DShader, Error, L"Compilation failed: %S", msg);
		}
	}

	HR(compileResult->GetResult(&bytecodeBlob));
}

D3D12_SHADER_BYTECODE D3DShaderStage::getBytecode() const
{
	D3D12_SHADER_BYTECODE bc;
	bc.BytecodeLength = bytecodeBlob->GetBufferSize();
	bc.pShaderBytecode = bytecodeBlob->GetBufferPointer();
	return bc;
}
