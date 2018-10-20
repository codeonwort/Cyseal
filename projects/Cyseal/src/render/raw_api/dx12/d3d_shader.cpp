#include "d3d_shader.h"
#include "d3d_util.h"
#include "util/resource_finder.h"
#include "core/assertion.h"
#include <d3dcompiler.h>

static const char* getD3DShaderType(EShaderType type)
{
	switch (type)
	{
	case EShaderType::VERTEX_SHADER:
		return "vs_5_0";
		break;
	case EShaderType::DOMAIN_SHADER:
		return "ds_5_0";
		break;
	case EShaderType::HULL_SHADER:
		return "hs_5_0";
		break;
	case EShaderType::GEOMETRY_SHADER:
		return "gs_5_0";
		break;
	case EShaderType::PIXEL_SHADER:
		return "ps_5_0";
		break;
	case EShaderType::COMPUTE_SHADER:
		return "cs_5_0";
		break;
	default:
		CHECK_NO_ENTRY();
	}
	return "unknown";
}

void D3DShader::loadVertexShader(const TCHAR* filename, const char* entryPoint)
{
	loadFromFile(filename, entryPoint, EShaderType::VERTEX_SHADER);
}

void D3DShader::loadPixelShader(const TCHAR* filename, const char* entryPoint)
{
	loadFromFile(filename, entryPoint, EShaderType::PIXEL_SHADER);
}

D3D12_SHADER_BYTECODE D3DShader::getBytecode(EShaderType shaderType)
{
	D3D12_SHADER_BYTECODE bc;
	bc.pShaderBytecode = byteCodes[(int)shaderType]->GetBufferPointer();
	bc.BytecodeLength = byteCodes[(int)shaderType]->GetBufferSize();
	return bc;
}

void D3DShader::loadFromFile(const TCHAR* filename, const char* entryPoint, EShaderType shaderType)
{
	UINT compileFlags = 0;
#if (DEBUG || _DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	D3D_SHADER_MACRO* defines = nullptr;
	WRL::ComPtr<ID3DBlob> errors;

	auto file = ResourceFinder::get().find(filename);

	int byteCodeIx = static_cast<int>(shaderType);

	HRESULT hr = D3DCompileFromFile(
		file.c_str(),
		defines,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entryPoint,
		getD3DShaderType(shaderType),
		compileFlags,
		0,
		byteCodes[byteCodeIx].GetAddressOf(),
		&errors);

	if (errors != nullptr)
	{
		// #todo: log module
		OutputDebugStringA((char*)errors->GetBufferPointer());
	}

	HR(hr);
}
