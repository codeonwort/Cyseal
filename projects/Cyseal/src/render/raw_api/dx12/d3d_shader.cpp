#include "d3d_shader.h"
#include "d3d_util.h"
#include "util/resource_finder.h"
#include "util/logging.h"
#include "core/assertion.h"

#include <d3dcompiler.h>

#define SKIP_SHADER_OPTIMIZATION (_DEBUG)

DEFINE_LOG_CATEGORY_STATIC(LogD3DShader);

static const char* getD3DShaderType(EShaderStage type)
{
	// #todo-wip-dxc: Shader Model 6
	switch (type)
	{
	case EShaderStage::VERTEX_SHADER:
		return "vs_5_1";
		break;
	case EShaderStage::DOMAIN_SHADER:
		return "ds_5_1";
		break;
	case EShaderStage::HULL_SHADER:
		return "hs_5_1";
		break;
	case EShaderStage::GEOMETRY_SHADER:
		return "gs_5_1";
		break;
	case EShaderStage::PIXEL_SHADER:
		return "ps_5_1";
		break;
	case EShaderStage::COMPUTE_SHADER:
		return "cs_5_1";
		break;
	default:
		CHECK_NO_ENTRY();
	}
	return "unknown";
}

void D3DShaderStage::loadFromFile(const wchar_t* inFilename, const char* entryPoint)
{
	UINT compileFlags = 0;
#if SKIP_SHADER_OPTIMIZATION
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	D3D_SHADER_MACRO* defines = nullptr;
	WRL::ComPtr<ID3DBlob> errors;

	std::wstring fullpath = ResourceFinder::get().find(inFilename);

	HRESULT hr = D3DCompileFromFile(
		fullpath.c_str(),
		defines,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entryPoint,
		getD3DShaderType(stageFlag),
		compileFlags,
		0,
		bytecodeBlob.GetAddressOf(),
		&errors);

	if (errors != nullptr)
	{
		const char* errorMessage = (const char*)errors->GetBufferPointer();
		CYLOG(LogD3DShader, Error, L"%S", errorMessage);
	}

	HR(hr);
}
