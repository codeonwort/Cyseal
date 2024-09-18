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
	// #todo-dxc: hlsl::Exception? Anyway the application runs fine.
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
	createRootSignature();
}

D3D12_SHADER_BYTECODE D3DShaderStage::getBytecode() const
{
	D3D12_SHADER_BYTECODE bc{
		.pShaderBytecode = bytecodeBlob->GetBufferPointer(),
		.BytecodeLength = bytecodeBlob->GetBufferSize(),
	};
	return bc;
}

const D3DShaderParameter* D3DShaderStage::findShaderParameter(const std::string& name) const
{
	auto it = parameterHashMap.find(name);
	return (it == parameterHashMap.end()) ? nullptr : it->second;
}

void D3DShaderStage::readShaderReflection(IDxcResult* compileResult)
{
	IDxcUtils* const utils = getD3DDevice()->getDxcUtils();

	// https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nn-d3d12shader-id3d12shaderreflection
	if (!isRaytracingShader(stageFlag))
	{
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

		// BoundResources = shader parameters
		for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
		{
			D3D12_SHADER_INPUT_BIND_DESC inputBindDesc{};
			shaderReflection->GetResourceBindingDesc(i, &inputBindDesc);

			D3DShaderParameter parameter{
				.name               = inputBindDesc.Name,
				//.type               = inputBindDesc.Type, // D3D_SIT_CBUFFER = ConstantBuffer, D3D_SIT_UAV_RWTYPED = RWBuffer, D3D_SIT_STRUCTURED = StructuredBuffer, ...
				.registerSlot       = inputBindDesc.BindPoint,
				.registerSpace      = inputBindDesc.Space,
				.rootParameterIndex = 0xffffffff, // Allocated in createRoogSignature()
			};
			
			// #wip-dxc-reflection: Handle missing D3D_SHADER_INPUT_TYPE cases
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
					// #wip-dxc-reflection: How to represent bindless?
					// ex) base_pass.hlsl has bindless texture.
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
					CHECK_NO_ENTRY();
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
					CHECK_NO_ENTRY();
					break;
				case D3D_SIT_UAV_FEEDBACKTEXTURE:
					CHECK_NO_ENTRY();
					break;
				default:
					CHECK_NO_ENTRY();
					break;
			}
		}

		if (stageFlag == EShaderStage::COMPUTE_SHADER)
		{
			threadGroupTotalSize = shaderReflection->GetThreadGroupSize(&threadGroupSizeX, &threadGroupSizeY, &threadGroupSizeZ);
		}
	}
	// #wip-dxc-reflection: ID3D12LibraryReflection for raytracing shaders
	// https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nn-d3d12shader-id3d12libraryreflection
	else
	{
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

		for (UINT functionIx = 0; functionIx < libraryDesc.FunctionCount; ++functionIx)
		{
			ID3D12FunctionReflection* functionReflection = libraryReflection->GetFunctionByIndex(functionIx);

			D3D12_FUNCTION_DESC functionDesc{};
			functionReflection->GetDesc(&functionDesc);

			const char* functionName = functionDesc.Name;

			for (UINT resourceIx = 0; resourceIx < functionDesc.BoundResources; ++resourceIx)
			{
				D3D12_SHADER_INPUT_BIND_DESC inputBindDesc{};
				functionReflection->GetResourceBindingDesc(resourceIx, &inputBindDesc);

				const char* resourceName = inputBindDesc.Name;

				// ...
			}
		}
	}

	// Build hash map for fast query.
	{
		auto build = [hashmap = &(this->parameterHashMap)](const std::vector<D3DShaderParameter>& params) {
			for (auto i = 0; i < params.size(); ++i)
			{
				hashmap->insert(std::make_pair(params[i].name, &(params[i])));
			}
		};
		build(parameterTable.rootConstants);
		build(parameterTable.constantBuffers);
		build(parameterTable.rwStructuredBuffers);
		build(parameterTable.rwBuffers);
		build(parameterTable.structuredBuffers);
		build(parameterTable.textures);
		build(parameterTable.samplers);
	}
}

void D3DShaderStage::createRootSignature()
{
	// #wip-dxc-reflection: Impose after converting all render passes.
	//CHECK(bPushConstantsDeclared);

	const size_t totalParameters = parameterTable.totalRootConstants() + parameterTable.totalBuffers() + parameterTable.totalTextures();
	const size_t totalSamplers = parameterTable.samplers.size();
	std::vector<D3D12_ROOT_PARAMETER> rootParameters(totalParameters);
	std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers(totalSamplers);

	// Temp storage for parameters.
	std::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges;
	descriptorRanges.reserve(totalParameters);
	auto lastDescriptorPtr = [](const decltype(descriptorRanges)& ranges) { return &ranges[ranges.size() - 1]; };

	// Construct root parameters.
	// #wip-dxc-reflection: How to determine shader visibility?
	{
		uint32 p = 0; // root parameter index
		for (auto& param : parameterTable.rootConstants)
		{
			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			rootParameters[p].Constants.ShaderRegister = param.registerSlot;
			rootParameters[p].Constants.RegisterSpace = param.registerSpace;
			rootParameters[p].Constants.Num32BitValues = 1; // #wip-dxc-reflection: Num32BitValues
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			param.rootParameterIndex = p;
			++p;
		}
		for (auto& param : parameterTable.constantBuffers)
		{
			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			rootParameters[p].Descriptor.ShaderRegister = param.registerSlot;
			rootParameters[p].Descriptor.RegisterSpace = param.registerSpace;
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			param.rootParameterIndex = p;
			++p;
		}
		for (auto& param : parameterTable.rwStructuredBuffers)
		{
			D3D12_DESCRIPTOR_RANGE descriptor{
				.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
				.NumDescriptors     = 1,
				.BaseShaderRegister = param.registerSlot,
				.RegisterSpace      = param.registerSpace,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
			};
			descriptorRanges.emplace_back(descriptor);

			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[p].DescriptorTable.NumDescriptorRanges = 1;
			rootParameters[p].DescriptorTable.pDescriptorRanges = lastDescriptorPtr(descriptorRanges);
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			param.rootParameterIndex = p;
			++p;
		}
		for (auto& param : parameterTable.rwBuffers)
		{
			D3D12_DESCRIPTOR_RANGE descriptor{
				.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
				.NumDescriptors     = 1,
				.BaseShaderRegister = param.registerSlot,
				.RegisterSpace      = param.registerSpace,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
			};
			descriptorRanges.emplace_back(descriptor);

			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[p].DescriptorTable.NumDescriptorRanges = 1;
			rootParameters[p].DescriptorTable.pDescriptorRanges = lastDescriptorPtr(descriptorRanges);
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			param.rootParameterIndex = p;
			++p;
		}
		for (auto& param : parameterTable.structuredBuffers)
		{
			D3D12_DESCRIPTOR_RANGE descriptor{
				.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				.NumDescriptors     = 1,
				.BaseShaderRegister = param.registerSlot,
				.RegisterSpace      = param.registerSpace,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
			};
			descriptorRanges.emplace_back(descriptor);

			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[p].DescriptorTable.NumDescriptorRanges = 1;
			rootParameters[p].DescriptorTable.pDescriptorRanges = lastDescriptorPtr(descriptorRanges);
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			param.rootParameterIndex = p;
			++p;
		}
		for (auto& param : parameterTable.textures)
		{
			D3D12_DESCRIPTOR_RANGE descriptor{
				.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				.NumDescriptors     = 1,
				.BaseShaderRegister = param.registerSlot,
				.RegisterSpace      = param.registerSpace,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
			};
			descriptorRanges.emplace_back(descriptor);

			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[p].DescriptorTable.NumDescriptorRanges = 1;
			rootParameters[p].DescriptorTable.pDescriptorRanges = lastDescriptorPtr(descriptorRanges);
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			param.rootParameterIndex = p;
			++p;
		}
		CHECK(p == totalParameters);

		p = 0;
		for (const auto& samp : parameterTable.samplers)
		{
			staticSamplers[p] = D3D12_STATIC_SAMPLER_DESC{
				.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
				.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				.MipLODBias       = 0.0f,
				.MaxAnisotropy    = 0,
				.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS,
				.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
				.MinLOD           = 0.0f,
				.MaxLOD           = 0.0f,
				.ShaderRegister   = samp.registerSlot,
				.RegisterSpace    = samp.registerSpace,
				.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
			};
			++p;
		}
		CHECK(p == totalSamplers);
	}

	// Create root signature.
	{
		D3D12_ROOT_SIGNATURE_DESC rootSigDesc{
			.NumParameters     = (UINT)rootParameters.size(),
			.pParameters       = rootParameters.data(),
			.NumStaticSamplers = (UINT)staticSamplers.size(),
			.pStaticSamplers   = staticSamplers.data(),
		};
		WRL::ComPtr<ID3DBlob> serializedRootSig, errorBlob;
		HRESULT hresult = D3D12SerializeRootSignature(
			&rootSigDesc,
			D3D_ROOT_SIGNATURE_VERSION_1, // #todo-dx12: Root signature version
			serializedRootSig.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			const char* message = reinterpret_cast<char*>(errorBlob->GetBufferPointer());
			::OutputDebugStringA(message);
		}
		HR(hresult);

		hresult = getD3DDevice()->getRawDevice()->CreateRootSignature(
			0, /*nodeMask*/
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(&rootSignature));
		HR(hresult);
	}
}
