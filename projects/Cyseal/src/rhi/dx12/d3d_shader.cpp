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

static const wchar_t* getD3DShaderModelString(D3D_SHADER_MODEL shaderModel)
{
	switch (shaderModel)
	{
		case D3D_SHADER_MODEL_6_0: return L"6_0";
		case D3D_SHADER_MODEL_6_1: return L"6_1";
		case D3D_SHADER_MODEL_6_2: return L"6_2";
		case D3D_SHADER_MODEL_6_3: return L"6_3";
		case D3D_SHADER_MODEL_6_4: return L"6_4";
		case D3D_SHADER_MODEL_6_5: return L"6_5";
		case D3D_SHADER_MODEL_6_6: return L"6_6";
		case D3D_SHADER_MODEL_6_7: return L"6_7";
		default: CHECK_NO_ENTRY();
	}
	return L"?_?";
}
static const wchar_t* getD3DShaderStagePrefix(EShaderStage stage)
{
	switch (stage)
	{
		case EShaderStage::VERTEX_SHADER:          return L"vs_";
		case EShaderStage::HULL_SHADER:            return L"hs_";
		case EShaderStage::DOMAIN_SHADER:          return L"ds_";
		case EShaderStage::GEOMETRY_SHADER:        return L"gs_";
		case EShaderStage::PIXEL_SHADER:           return L"ps_";
		case EShaderStage::COMPUTE_SHADER:         return L"cs_";
		case EShaderStage::MESH_SHADER:            return L"ms_";
		case EShaderStage::AMPLICATION_SHADER:     return L"as_";
		case EShaderStage::RT_RAYGEN_SHADER:
		case EShaderStage::RT_ANYHIT_SHADER:
		case EShaderStage::RT_CLOSESTHIT_SHADER:
		case EShaderStage::RT_MISS_SHADER:
		case EShaderStage::RT_INTERSECTION_SHADER: return L"lib_";
		default: CHECK_NO_ENTRY();
	}
	return L"??_";
}

static std::wstring getD3DShaderProfile(D3D_SHADER_MODEL shaderModel, EShaderStage type)
{
	std::wstring profile = getD3DShaderStagePrefix(type);
	profile += getD3DShaderModelString(shaderModel);
	return profile;
}

void D3DShaderStage::loadFromFile(const wchar_t* inFilename, const char* inEntryPoint)
{
	IDxcUtils* utils = getD3DDevice()->getDxcUtils();
	IDxcCompiler3* compiler = getD3DDevice()->getDxcCompiler();
	IDxcIncludeHandler* includeHandler = getD3DDevice()->getDxcIncludeHandler();
	D3D_SHADER_MODEL highestSM = getD3DDevice()->getHighestShaderModel();

	std::wstring fullpath = ResourceFinder::get().find(inFilename);
	if (fullpath.size() == 0)
	{
		CYLOG(LogD3DShader, Fatal, L"Failed to find shader: %s", fullpath.c_str());
		CHECK_NO_ENTRY();
	}

	uint32 codePage = CP_UTF8;
	WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
	HRESULT hr = utils->LoadFile(fullpath.c_str(), &codePage, &sourceBlob);
	if (FAILED(hr))
	{
		CYLOG(LogD3DShader, Fatal, L"Failed to create blob from: %s", fullpath.c_str());
		CHECK_NO_ENTRY();
	}

	std::wstring includeDir = getShaderDirectory();
	std::wstring targetProfile = getD3DShaderProfile(highestSM, stageFlag);
	aEntryPoint = inEntryPoint;
	str_to_wstr(inEntryPoint, wEntryPoint);

	std::vector<LPCWSTR> arguments = {
		L"-I", includeDir.c_str(),
		L"-E", wEntryPoint.c_str(),
		L"-T", targetProfile.c_str(),
	};
#if SKIP_SHADER_OPTIMIZATION
	//arguments.push_back(DXC_ARG_DEBUG);
#endif
	
	DxcBuffer sourceBuffer{
		.Ptr      = sourceBlob->GetBufferPointer(),
		.Size     = sourceBlob->GetBufferSize(),
		.Encoding = 0,
	};

	WRL::ComPtr<IDxcResult> compileResult;
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

	readShaderReflection(compileResult.Get());
}

D3D12_SHADER_BYTECODE D3DShaderStage::getBytecode() const
{
	D3D12_SHADER_BYTECODE bc{
		.pShaderBytecode = bytecodeBlob->GetBufferPointer(),
		.BytecodeLength = bytecodeBlob->GetBufferSize(),
	};
	return bc;
}

void D3DShaderStage::readShaderReflection(IDxcResult* compileResult)
{
	IDxcUtils* const utils = getD3DDevice()->getDxcUtils();

	if (!isRaytracingShader(stageFlag))
	{
		// https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nn-d3d12shader-id3d12shaderreflection

		WRL::ComPtr<IDxcBlob> reflectionBlob;
		HR( compileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(reflectionBlob.GetAddressOf()), NULL) );

		DxcBuffer reflectionBuffer{
			.Ptr = reflectionBlob->GetBufferPointer(),
			.Size = reflectionBlob->GetBufferSize(),
			.Encoding = 0,
		};
		WRL::ComPtr<ID3D12ShaderReflection> shaderReflection;
		HR( utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(shaderReflection.GetAddressOf())) );

		D3D12_SHADER_DESC shaderDesc{};
		shaderReflection->GetDesc(&shaderDesc);

		// Shader version, e.g., cs_6_6
		// https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/ns-d3d12shader-d3d12_shader_desc
		programType = static_cast<D3D12_SHADER_VERSION_TYPE>((shaderDesc.Version & 0xFFFF0000) >> 16);
		programMajorVersion = (shaderDesc.Version & 0x000000F0) >> 4;
		programMinorVersion = (shaderDesc.Version & 0x0000000F);

		// BoundResources = shader parameters
		for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
		{
			D3D12_SHADER_INPUT_BIND_DESC inputBindDesc{};
			shaderReflection->GetResourceBindingDesc(i, &inputBindDesc);
			addToShaderParameterTable(inputBindDesc);
		}

		if (stageFlag == EShaderStage::COMPUTE_SHADER)
		{
			threadGroupTotalSize = shaderReflection->GetThreadGroupSize(&threadGroupSizeX, &threadGroupSizeY, &threadGroupSizeZ);
		}
	}
	else
	{
		// https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nn-d3d12shader-id3d12libraryreflection

		WRL::ComPtr<IDxcBlob> reflectionBlob;
		HR( compileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(reflectionBlob.GetAddressOf()), NULL) );

		DxcBuffer reflectionBuffer{
			.Ptr = reflectionBlob->GetBufferPointer(),
			.Size = reflectionBlob->GetBufferSize(),
			.Encoding = 0,
		};
		WRL::ComPtr<ID3D12LibraryReflection> libraryReflection;
		HR( utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(libraryReflection.GetAddressOf())) );

		D3D12_LIBRARY_DESC libraryDesc{};
		libraryReflection->GetDesc(&libraryDesc);

		// Loop through functions and process only matching one.
		for (UINT functionIx = 0; functionIx < libraryDesc.FunctionCount; ++functionIx)
		{
			ID3D12FunctionReflection* functionReflection = libraryReflection->GetFunctionByIndex(functionIx);

			D3D12_FUNCTION_DESC functionDesc{};
			functionReflection->GetDesc(&functionDesc);

			std::string weirdFunctionName = functionDesc.Name;
			if (weirdFunctionName.find(aEntryPoint) == std::string::npos)
			{
				continue;
			}

			// Shader version, e.g., cs_6_6
			// https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/ns-d3d12shader-d3d12_function_desc
			programType = static_cast<D3D12_SHADER_VERSION_TYPE>((functionDesc.Version & 0xFFFF0000) >> 16);
			programMajorVersion = (functionDesc.Version & 0x000000F0) >> 4;
			programMinorVersion = (functionDesc.Version & 0x0000000F);

			for (UINT resourceIx = 0; resourceIx < functionDesc.BoundResources; ++resourceIx)
			{
				D3D12_SHADER_INPUT_BIND_DESC inputBindDesc{};
				functionReflection->GetResourceBindingDesc(resourceIx, &inputBindDesc);
				addToShaderParameterTable(inputBindDesc);
			}
		}
	}
}

void D3DShaderStage::addToShaderParameterTable(const D3D12_SHADER_INPUT_BIND_DESC& inputBindDesc)
{
	D3DShaderParameter parameter{
		.name               = inputBindDesc.Name,
		.type               = inputBindDesc.Type, // D3D_SIT_CBUFFER = ConstantBuffer, D3D_SIT_UAV_RWTYPED = RWBuffer, D3D_SIT_STRUCTURED = StructuredBuffer, ...
		.registerSlot       = inputBindDesc.BindPoint,
		.registerSpace      = inputBindDesc.Space,
		.numDescriptors     = inputBindDesc.BindCount,
		.rootParameterIndex = 0xffffffff, // Allocated in createRoogSignature()
	};
	
	// #todo-dx12: Handle missing D3D_SHADER_INPUT_TYPE cases
	switch (inputBindDesc.Type)
	{
		case D3D_SIT_CBUFFER: // ConstantBuffer
			if (shouldBePushConstants(inputBindDesc.Name))
			{
				parameterTable.rootConstants.emplace_back(parameter);
			}
			else
			{
				parameterTable.constantBuffers.emplace_back(parameter);
			}
			break;
		case D3D_SIT_TBUFFER:
			CHECK_NO_ENTRY();
			break;
		case D3D_SIT_TEXTURE: // Texture2D, Texture3D, TextureCube, ...
			parameterTable.textures.emplace_back(parameter);
			break;
		case D3D_SIT_SAMPLER: // SamplerState
			parameterTable.samplers.emplace_back(parameter);
			break;
		case D3D_SIT_UAV_RWTYPED: // RWBuffer
			parameterTable.rwBuffers.emplace_back(parameter);
			break;
		case D3D_SIT_STRUCTURED: // StructuredBuffer
			parameterTable.structuredBuffers.emplace_back(parameter);
			break;
		case D3D_SIT_UAV_RWSTRUCTURED: // RWStructuredBuffer
			parameterTable.rwStructuredBuffers.emplace_back(parameter);
			break;
		case D3D_SIT_BYTEADDRESS: // ByteAddressBuffer
			parameterTable.byteAddressBuffers.emplace_back(parameter);
			break;
		case D3D_SIT_UAV_RWBYTEADDRESS: // RWByteAddressBuffer
			CHECK_NO_ENTRY();
			break;
		case D3D_SIT_UAV_APPEND_STRUCTURED: // AppendStructuredBuffer
			CHECK_NO_ENTRY();
			break;
		case D3D_SIT_UAV_CONSUME_STRUCTURED:
			CHECK_NO_ENTRY();
			break;
		case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
			CHECK_NO_ENTRY();
			break;
		case D3D_SIT_RTACCELERATIONSTRUCTURE:
			parameterTable.accelerationStructures.emplace_back(parameter);
			break;
		case D3D_SIT_UAV_FEEDBACKTEXTURE:
			CHECK_NO_ENTRY();
			break;
		default:
			CHECK_NO_ENTRY();
			break;
	}
}
