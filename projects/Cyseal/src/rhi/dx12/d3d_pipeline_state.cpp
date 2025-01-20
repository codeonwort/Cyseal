#include "d3d_pipeline_state.h"
#include "d3d_buffer.h"
#include "d3d_into.h"
#include "rhi/render_command.h"
#include "rhi/buffer.h"
#include "util/logging.h"

#include <algorithm>

// --------------------------------------------------------
// common

DEFINE_LOG_CATEGORY_STATIC(LogD3DPipelineState);

enum class ESpecialParameterSetPolicy
{
	DontCare,      // Ignore the set
	AcceptOnlySet, // Accept parameters only in the set
	DiscardSet,    // Discard parameters in the set
};

static void buildShaderParameterTable(
	D3DShaderParameterTable& dstTable,
	const std::vector<ShaderStage*>& shaderStages,
	const std::vector<std::string>& specialParameterSet,
	ESpecialParameterSetPolicy policy)
{
	struct InvalidParamInfo
	{
		D3DShaderStage* shader;
		D3DShaderParameter validParam;
		D3DShaderParameter invalidParam;
	};
	std::vector<InvalidParamInfo> invalidParamInfo;
	std::map<std::string, D3DShaderParameter> tempCache;
	auto appendParameters = [&invalidParamInfo, &tempCache, &specialParameterSet, &policy](std::vector<D3DShaderParameter>& dst, const std::vector<D3DShaderParameter>& src, D3DShaderStage* srcShader) {
		for (const D3DShaderParameter& param : src)
		{
			auto it = tempCache.find(param.name);
			if (it == tempCache.end())
			{
				bool addToTable = true;
				if (policy != ESpecialParameterSetPolicy::DontCare)
				{
					bool isSpecial = std::find(specialParameterSet.begin(), specialParameterSet.end(), param.name) != specialParameterSet.end();
					addToTable = (isSpecial && policy == ESpecialParameterSetPolicy::AcceptOnlySet) || (!isSpecial && policy == ESpecialParameterSetPolicy::DiscardSet);
				}
				if (addToTable)
				{
					tempCache.insert(std::make_pair(param.name, param));
					dst.push_back(param);
				}
			}
			else if (it->second.hasSameReflection(param) == false)
			{
				invalidParamInfo.push_back({ srcShader, it->second, param });
			}
		}
	};

	for (ShaderStage* shaderStage : shaderStages)
	{
		D3DShaderStage* d3dShader = static_cast<D3DShaderStage*>(shaderStage);
		if (d3dShader == nullptr) continue;
		
		const D3DShaderParameterTable& srcTable = d3dShader->getParameterTable();
		appendParameters(dstTable.rootConstants,          srcTable.rootConstants,          d3dShader);
		appendParameters(dstTable.constantBuffers,        srcTable.constantBuffers,        d3dShader);
		appendParameters(dstTable.rwStructuredBuffers,    srcTable.rwStructuredBuffers,    d3dShader);
		appendParameters(dstTable.rwBuffers,              srcTable.rwBuffers,              d3dShader);
		appendParameters(dstTable.structuredBuffers,      srcTable.structuredBuffers,      d3dShader);
		appendParameters(dstTable.byteAddressBuffers,     srcTable.byteAddressBuffers,     d3dShader);
		appendParameters(dstTable.textures,               srcTable.textures,               d3dShader);
		appendParameters(dstTable.samplers,               srcTable.samplers,               d3dShader);
		appendParameters(dstTable.accelerationStructures, srcTable.accelerationStructures, d3dShader);
	}

	if (invalidParamInfo.size() > 0)
	{
		for (const InvalidParamInfo& info : invalidParamInfo)
		{
			CYLOG(LogD3DPipelineState, Error,
				L"Shader %s: Parameter %S is already defined by { type=(D3D_SHADER_INPUT_TYPE)%u, register=%u, space=%u } but you're trying to define it again by { type = (D3D_SHADER_INPUT_TYPE)%u, register=%u, space=%u }.",
				info.shader->getEntryPointW(), info.validParam.name.c_str(),
				(uint32)info.validParam.type, info.validParam.registerSlot, info.validParam.registerSpace,
				(uint32)info.invalidParam.type, info.invalidParam.registerSlot, info.invalidParam.registerSpace);
		}
		CHECK_NO_ENTRY();
	}
}

// Modifies outRootSignature and parameterTable.
static void createRootSignatureFromParameterTable(
	WRL::ComPtr<ID3D12RootSignature>& outRootSignature,
	ID3D12Device* device,
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags,
	D3DShaderParameterTable& inoutParameterTable,
	const std::vector<StaticSamplerDesc>& inStaticSamplers = {})
{
	D3DShaderParameterTable& parameterTable = inoutParameterTable;

	const size_t totalParameters = parameterTable.totalRootConstants() + parameterTable.totalBuffers() + parameterTable.totalTextures() + parameterTable.totalAccelerationStructures();
	const size_t totalSamplers = parameterTable.samplers.size();
	std::vector<D3D12_ROOT_PARAMETER> rootParameters(totalParameters);
	std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers(totalSamplers);

	// Temp storage for parameters.
	std::vector<D3D12_DESCRIPTOR_RANGE> descriptorRanges;
	descriptorRanges.reserve(totalParameters);
	auto lastDescriptorPtr = [](const decltype(descriptorRanges)& ranges) { return &ranges[ranges.size() - 1]; };

	// #todo-dx12: D3D12_SHADER_VISIBILITY
	// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_shader_visibility
	// Let's just use D3D12_SHADER_VISIBILITY_ALL for now.

	// Construct root parameters.
	{
		uint32 p = 0; // root parameter index
		for (auto& param : parameterTable.rootConstants)
		{
			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			rootParameters[p].Constants.ShaderRegister = param.registerSlot;
			rootParameters[p].Constants.RegisterSpace = param.registerSpace;
			rootParameters[p].Constants.Num32BitValues = 1; // #todo-dx12: Num32BitValues
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			param.rootParameterIndex = p;
			++p;
		}
		for (auto& param : parameterTable.constantBuffers)
		{
#if 0
			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			rootParameters[p].Descriptor.ShaderRegister = param.registerSlot;
			rootParameters[p].Descriptor.RegisterSpace = param.registerSpace;
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
#else
			D3D12_DESCRIPTOR_RANGE descriptor{
				.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
				.NumDescriptors     = param.numDescriptors,
				.BaseShaderRegister = param.registerSlot,
				.RegisterSpace      = param.registerSpace,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
			};
			descriptorRanges.emplace_back(descriptor);

			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[p].DescriptorTable.NumDescriptorRanges = 1;
			rootParameters[p].DescriptorTable.pDescriptorRanges = lastDescriptorPtr(descriptorRanges);
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
#endif
			param.rootParameterIndex = p;
			++p;
		}
		for (auto& param : parameterTable.rwStructuredBuffers)
		{
			D3D12_DESCRIPTOR_RANGE descriptor{
				.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
				.NumDescriptors     = param.numDescriptors,
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
				.NumDescriptors     = param.numDescriptors,
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
				.NumDescriptors     = param.numDescriptors,
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
		for (auto& param : parameterTable.byteAddressBuffers)
		{
			D3D12_DESCRIPTOR_RANGE descriptor{
				.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				.NumDescriptors     = param.numDescriptors,
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
				.NumDescriptors     = param.numDescriptors,
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
		for (auto& param : parameterTable.accelerationStructures)
		{
			// #todo-dxr: SRV in D3DAccelerationStructure does not have source heap
#if 0
			D3D12_DESCRIPTOR_RANGE descriptor{
				.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				.NumDescriptors     = param.numDescriptors,
				.BaseShaderRegister = param.registerSlot,
				.RegisterSpace      = param.registerSpace,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
			};
			descriptorRanges.emplace_back(descriptor);

			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[p].DescriptorTable.NumDescriptorRanges = 1;
			rootParameters[p].DescriptorTable.pDescriptorRanges = lastDescriptorPtr(descriptorRanges);
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
#else
			D3D12_DESCRIPTOR_RANGE descriptor{
				.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				.NumDescriptors     = param.numDescriptors,
				.BaseShaderRegister = param.registerSlot,
				.RegisterSpace      = param.registerSpace,
				.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
			};
			descriptorRanges.emplace_back(descriptor);

			rootParameters[p].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			rootParameters[p].Descriptor.ShaderRegister = param.registerSlot;
			rootParameters[p].Descriptor.RegisterSpace = param.registerSpace;
			rootParameters[p].DescriptorTable.pDescriptorRanges = lastDescriptorPtr(descriptorRanges);
			rootParameters[p].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
#endif

			param.rootParameterIndex = p;
			++p;
		}
		CHECK(p == totalParameters);

		p = 0;
		for (const D3DShaderParameter& samplerReflection : parameterTable.samplers)
		{
			size_t sampDescIx;
			for (sampDescIx = 0; sampDescIx < inStaticSamplers.size(); ++sampDescIx)
			{
				if (inStaticSamplers[sampDescIx].name == samplerReflection.name)
				{
					break;
				}
			}
			if (sampDescIx == inStaticSamplers.size())
			{
				CYLOG(LogD3DPipelineState, Error, L"Sampler desc for %S : register(s%d, space%d) was not provided. A default desc will be used.",
					samplerReflection.name.c_str(), samplerReflection.registerSlot, samplerReflection.registerSpace);

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
					.ShaderRegister   = samplerReflection.registerSlot,
					.RegisterSpace    = samplerReflection.registerSpace,
					.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
				};
			}
			else
			{
				into_d3d::staticSamplerDesc(inStaticSamplers[sampDescIx], samplerReflection.registerSlot, samplerReflection.registerSpace, staticSamplers[p]);
			}
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
			.Flags             = rootSignatureFlags,
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

		hresult = device->CreateRootSignature(
			0, // #todo-dx12: nodeMask in CreateRootSignature()
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(&outRootSignature));
		HR(hresult);
	}
}

static void createShaderParameterHashMap(std::map<std::string, const D3DShaderParameter*>& outParameterHashMap, const D3DShaderParameterTable& parameterTable)
{
	auto build = [&outParameterHashMap](const std::vector<D3DShaderParameter>& params) {
		for (auto i = 0; i < params.size(); ++i)
		{
			outParameterHashMap.insert(std::make_pair(params[i].name, &(params[i])));
		}
	};
	build(parameterTable.rootConstants);
	build(parameterTable.constantBuffers);
	build(parameterTable.rwStructuredBuffers);
	build(parameterTable.rwBuffers);
	build(parameterTable.structuredBuffers);
	build(parameterTable.byteAddressBuffers);
	build(parameterTable.textures);
	build(parameterTable.accelerationStructures);
	build(parameterTable.samplers);
}

// --------------------------------------------------------
// D3DGraphicsPipelineState

void D3DGraphicsPipelineState::initialize(ID3D12Device* device, const GraphicsPipelineDesc& inDesc)
{
	createRootSignature(device, inDesc);

	into_d3d::TempAlloc tempAlloc;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC d3d_desc;
	into_d3d::graphicsPipelineDesc(inDesc, d3d_desc, tempAlloc);
	d3d_desc.pRootSignature = rootSignature.Get();

	HR( device->CreateGraphicsPipelineState(&d3d_desc, IID_PPV_ARGS(&pipelineState)) );
}

const D3DShaderParameter* D3DGraphicsPipelineState::findShaderParameter(const std::string& name) const
{
	auto it = parameterHashMap.find(name);
	return (it == parameterHashMap.end()) ? nullptr : it->second;
}

void D3DGraphicsPipelineState::createRootSignature(ID3D12Device* device, const GraphicsPipelineDesc& inDesc)
{
	D3DShaderStage* vs = static_cast<D3DShaderStage*>(inDesc.vs);
	D3DShaderStage* ps = static_cast<D3DShaderStage*>(inDesc.ps);
	D3DShaderStage* ds = static_cast<D3DShaderStage*>(inDesc.ds);
	D3DShaderStage* hs = static_cast<D3DShaderStage*>(inDesc.hs);
	D3DShaderStage* gs = static_cast<D3DShaderStage*>(inDesc.gs);
	CHECK(vs == nullptr || vs->isPushConstantsDeclared());
	CHECK(ps == nullptr || ps->isPushConstantsDeclared());
	CHECK(ds == nullptr || ds->isPushConstantsDeclared());
	CHECK(hs == nullptr || hs->isPushConstantsDeclared());
	CHECK(gs == nullptr || gs->isPushConstantsDeclared());

	auto flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	buildShaderParameterTable(parameterTable, { vs, ps, ds, hs, gs }, {}, ESpecialParameterSetPolicy::DontCare);
	createRootSignatureFromParameterTable(rootSignature, device, flags, parameterTable, inDesc.staticSamplers);
	createShaderParameterHashMap(parameterHashMap, parameterTable);
}

// --------------------------------------------------------
// D3DComputePipelineState

void D3DComputePipelineState::initialize(ID3D12Device* device, const ComputePipelineDesc& inDesc)
{
	CHECK(inDesc.cs != nullptr);
	D3DShaderStage* shaderStage = static_cast<D3DShaderStage*>(inDesc.cs);

	createRootSignature(device, shaderStage, inDesc.staticSamplers);

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{
		.pRootSignature = rootSignature.Get(),
		.CS             = shaderStage->getBytecode(),
		.NodeMask       = (UINT)inDesc.nodeMask,
		// #todo-dx12: Compute shader - CachedPSO, Flags
		.CachedPSO      = D3D12_CACHED_PIPELINE_STATE{
			.pCachedBlob           = NULL,
			.CachedBlobSizeInBytes = 0,
		},
		.Flags          = D3D12_PIPELINE_STATE_FLAG_NONE,
	};

	HR( device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipelineState)) );
}

const D3DShaderParameter* D3DComputePipelineState::findShaderParameter(const std::string& name) const
{
	auto it = parameterHashMap.find(name);
	return (it == parameterHashMap.end()) ? nullptr : it->second;
}

void D3DComputePipelineState::createRootSignature(ID3D12Device* device, D3DShaderStage* computeShader, const std::vector<StaticSamplerDesc>& staticSamplers)
{
	CHECK(computeShader->isPushConstantsDeclared());

	auto flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	parameterTable = computeShader->getParameterTable(); // There is only one shader, just do deep copy rather than calling buildShaderParameterTable().
	createRootSignatureFromParameterTable(rootSignature, device, flags, parameterTable, staticSamplers);
	createShaderParameterHashMap(parameterHashMap, parameterTable);
}

// --------------------------------------------------------
// D3DRaytracingPipelineStateObject

void D3DRaytracingPipelineStateObject::initialize(ID3D12Device5* device, const D3D12_STATE_OBJECT_DESC& desc)
{
	HR(device->CreateStateObject(&desc, IID_PPV_ARGS(&rawRTPSO)));
	HR(rawRTPSO.As(&rawProperties));
}

void D3DRaytracingPipelineStateObject::initialize(ID3D12Device5* device, const RaytracingPipelineStateObjectDesc& inDesc)
{
	createRootSignatures(device, inDesc);

	D3DShaderStage* raygenShader = static_cast<D3DShaderStage*>(inDesc.raygenShader);
	D3DShaderStage* closestHitShader = static_cast<D3DShaderStage*>(inDesc.closestHitShader);
	D3DShaderStage* missShader = static_cast<D3DShaderStage*>(inDesc.missShader);
	// #todo-dxr: anyHitShader, intersectionShader
	D3DShaderStage* anyHitShader = static_cast<D3DShaderStage*>(nullptr/*desc.anyHitShader*/); 
	D3DShaderStage* intersectionShader = static_cast<D3DShaderStage*>(nullptr/*desc.intersectionShader*/);

	CD3DX12_STATE_OBJECT_DESC d3d_desc{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

	// DXIL library
	auto createRTShaderSubobject = [&](D3DShaderStage* shaderStage)
	{
		if (shaderStage != nullptr)
		{
			D3D12_SHADER_BYTECODE shaderBytecode = shaderStage->getBytecode();
			auto lib = d3d_desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
			lib->SetDXILLibrary(&shaderBytecode);
			lib->DefineExport(shaderStage->getEntryPointW());
		}
	};
	createRTShaderSubobject(raygenShader);
	createRTShaderSubobject(closestHitShader);
	createRTShaderSubobject(missShader);
	//createRTShaderSubobject(anyHitShader); // #todo-dxr: anyHitShader, intersectionShader
	//createRTShaderSubobject(intersectionShader);

	// #todo-dxr: anyHitShader, intersectionShader
	// Hit group
	auto hitGroup = d3d_desc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	if (closestHitShader != nullptr)
	{
		hitGroup->SetClosestHitShaderImport(closestHitShader->getEntryPointW());
	}
	hitGroup->SetHitGroupExport(inDesc.hitGroupName.c_str());
	hitGroup->SetHitGroupType(into_d3d::hitGroupType(inDesc.hitGroupType));

	// Shader config
	auto shaderConfig = d3d_desc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	CHECK(inDesc.maxAttributeSizeInBytes < D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES);
	shaderConfig->Config(inDesc.maxPayloadSizeInBytes, inDesc.maxAttributeSizeInBytes);

	// Local root signature
	auto bindLocalRootSignature = [&](ShaderStage* shader, ID3D12RootSignature* rootSig)
	{
		if (shader != nullptr && rootSig != nullptr)
		{
			auto shaderName = static_cast<D3DShaderStage*>(shader)->getEntryPointW();

			auto localSig = d3d_desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
			localSig->SetRootSignature(rootSig);
			auto assoc = d3d_desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			assoc->SetSubobjectToAssociate(*localSig);
			assoc->AddExport(shaderName);
		}
	};
	bindLocalRootSignature(inDesc.raygenShader, localRootSignatureRaygen.Get());
	bindLocalRootSignature(inDesc.closestHitShader, localRootSignatureClosestHit.Get());
	bindLocalRootSignature(inDesc.missShader, localRootSignatureMiss.Get());
	// #todo-dxr: anyHitShader, intersectionShader
	//bindLocalRootSignature(desc.anyHitShader, localRootSignatureAnyHit.Get());
	//bindLocalRootSignature(desc.intersectionShader, localRootSignatureIntersection.Get());

	// Global root signature
	auto globalSig = d3d_desc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	globalSig->SetRootSignature(globalRootSignature.Get());

	// Pipeline config
	auto pipelineConfig = d3d_desc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	pipelineConfig->Config(inDesc.maxTraceRecursionDepth);

	D3D12_STATE_OBJECT_DESC stateObjectDesc = d3d_desc;
	HR(device->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&rawRTPSO)));
	HR(rawRTPSO.As(&rawProperties));
}

const D3DShaderParameter* D3DRaytracingPipelineStateObject::findGlobalShaderParameter(const std::string& name) const
{
	auto it = globalParameterHashMap.find(name);
	return (it == globalParameterHashMap.end()) ? nullptr : it->second;
}

void D3DRaytracingPipelineStateObject::createRootSignatures(ID3D12Device* device, const RaytracingPipelineStateObjectDesc& desc)
{
	D3DShaderStage* raygen = static_cast<D3DShaderStage*>(desc.raygenShader);
	D3DShaderStage* closestHit = static_cast<D3DShaderStage*>(desc.closestHitShader);
	D3DShaderStage* miss = static_cast<D3DShaderStage*>(desc.missShader);
	// #todo-dxr: anyHitShader, intersectionShader
	D3DShaderStage* anyHit = static_cast<D3DShaderStage*>(nullptr/*desc.anyHitShader*/);
	D3DShaderStage* intersection = static_cast<D3DShaderStage*>(nullptr/*desc.intersectionShader*/);
	CHECK(raygen == nullptr || raygen->isPushConstantsDeclared());
	CHECK(closestHit == nullptr || closestHit->isPushConstantsDeclared());
	CHECK(miss == nullptr || miss->isPushConstantsDeclared());
	CHECK(anyHit == nullptr || anyHit->isPushConstantsDeclared());
	CHECK(intersection == nullptr || intersection->isPushConstantsDeclared());

	std::vector<std::string> allLocalParameters;
	{
		// If a parameter is used in multiple stages, exclude redundant reflections of it here.
		std::vector<std::string> temp;
		temp.insert(temp.end(), desc.raygenLocalParameters.begin(), desc.raygenLocalParameters.end());
		temp.insert(temp.end(), desc.closestHitLocalParameters.begin(), desc.closestHitLocalParameters.end());
		temp.insert(temp.end(), desc.missLocalParameters.begin(), desc.missLocalParameters.end());
		// #todo-dxr: anyHitShader, intersectionShader
		//temp.insert(temp.end(), desc.anyHitParameters.begin(), desc.anyHitParameters.end());
		//temp.insert(temp.end(), desc.missLocalParameters.begin(), desc.missLocalParameters.end());
		std::sort(temp.begin(), temp.end());
		for (size_t i = 0; i < temp.size(); ++i)
		{
			if (i == 0 || temp[i] != temp[i - 1]) allLocalParameters.push_back(temp[i]);
		}
	}

	// Create global root signature.
	auto globalRootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	buildShaderParameterTable(globalParameterTable, { raygen, closestHit, miss, anyHit, intersection }, allLocalParameters, ESpecialParameterSetPolicy::DiscardSet);
	createRootSignatureFromParameterTable(globalRootSignature, device, globalRootSignatureFlags, globalParameterTable, desc.staticSamplers);
	createShaderParameterHashMap(globalParameterHashMap, globalParameterTable);

	auto localRootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	if (desc.raygenLocalParameters.size() > 0)
	{
		buildShaderParameterTable(localParameterTableRaygen, { raygen }, desc.raygenLocalParameters, ESpecialParameterSetPolicy::AcceptOnlySet);
		createRootSignatureFromParameterTable(localRootSignatureRaygen, device, localRootSignatureFlags, localParameterTableRaygen);
	}
	if (desc.closestHitLocalParameters.size() > 0)
	{
		buildShaderParameterTable(localParameterTableClosestHit, { closestHit }, desc.closestHitLocalParameters, ESpecialParameterSetPolicy::AcceptOnlySet);
		createRootSignatureFromParameterTable(localRootSignatureClosestHit, device, localRootSignatureFlags, localParameterTableClosestHit);
	}
	if (desc.missLocalParameters.size() > 0)
	{
		buildShaderParameterTable(localParameterTableMiss, { miss }, desc.missLocalParameters, ESpecialParameterSetPolicy::AcceptOnlySet);
		createRootSignatureFromParameterTable(localRootSignatureMiss, device, localRootSignatureFlags, localParameterTableMiss);
	}
	// #todo-dxr: anyHitShader, intersectionShader
}

// --------------------------------------------------------
// D3DIndirectCommandGenerator

D3DIndirectCommandGenerator::~D3DIndirectCommandGenerator()
{
	if (memblock != nullptr)
	{
		::free(memblock);
		memblock = nullptr;
	}
}

void D3DIndirectCommandGenerator::initialize(
	const CommandSignatureDesc& inSigDesc,
	uint32 inMaxCommandCount)
{
	byteStride = into_d3d::calcCommandSignatureByteStride(inSigDesc, paddingBytes);
	maxCommandCount = inMaxCommandCount;

	memblock = reinterpret_cast<uint8*>(::malloc(byteStride * inMaxCommandCount));
}

void D3DIndirectCommandGenerator::resizeMaxCommandCount(uint32 newMaxCount)
{
	CHECK(byteStride != 0 && currentWritePtr == nullptr);

	maxCommandCount = newMaxCount;
	if (memblock != nullptr)
	{
		::free(memblock);
	}
	memblock = reinterpret_cast<uint8*>(::malloc(byteStride * maxCommandCount));
}

void D3DIndirectCommandGenerator::beginCommand(uint32 commandIx)
{
	CHECK(currentWritePtr == nullptr && commandIx < maxCommandCount);
	currentWritePtr = memblock + byteStride * commandIx;
}

void D3DIndirectCommandGenerator::writeConstant32(uint32 constant)
{
	CHECK(currentWritePtr != nullptr);
	::memcpy_s(currentWritePtr, sizeof(uint32), &constant, sizeof(uint32));
	currentWritePtr += sizeof(uint32);
}

void D3DIndirectCommandGenerator::writeVertexBufferView(VertexBuffer* vbuffer)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_VERTEX_BUFFER_VIEW view = static_cast<D3DVertexBuffer*>(vbuffer)->getVertexBufferView();
	::memcpy_s(currentWritePtr, sizeof(D3D12_VERTEX_BUFFER_VIEW), &view, sizeof(D3D12_VERTEX_BUFFER_VIEW));
	currentWritePtr += sizeof(D3D12_VERTEX_BUFFER_VIEW);
}

void D3DIndirectCommandGenerator::writeIndexBufferView(IndexBuffer* ibuffer)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_INDEX_BUFFER_VIEW view = static_cast<D3DIndexBuffer*>(ibuffer)->getIndexBufferView();
	::memcpy_s(currentWritePtr, sizeof(D3D12_INDEX_BUFFER_VIEW), &view, sizeof(D3D12_INDEX_BUFFER_VIEW));
	currentWritePtr += sizeof(D3D12_INDEX_BUFFER_VIEW);
}

void D3DIndirectCommandGenerator::writeDrawArguments(
	uint32 vertexCountPerInstance,
	uint32 instanceCount,
	uint32 startVertexLocation,
	uint32 startInstanceLocation)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_DRAW_ARGUMENTS args{
		.VertexCountPerInstance = vertexCountPerInstance,
		.InstanceCount = instanceCount,
		.StartVertexLocation = startVertexLocation,
		.StartInstanceLocation = startInstanceLocation
	};
	::memcpy_s(currentWritePtr, sizeof(D3D12_DRAW_ARGUMENTS), &args, sizeof(D3D12_DRAW_ARGUMENTS));
	currentWritePtr += sizeof(D3D12_DRAW_ARGUMENTS);
}

void D3DIndirectCommandGenerator::writeDrawIndexedArguments(
	uint32 indexCountPerInstance,
	uint32 instanceCount,
	uint32 startIndexLocation,
	int32 baseVertexLocation,
	uint32 startInstanceLocation)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_DRAW_INDEXED_ARGUMENTS args{
		.IndexCountPerInstance = indexCountPerInstance,
		.InstanceCount = instanceCount,
		.StartIndexLocation = startIndexLocation,
		.BaseVertexLocation = baseVertexLocation,
		.StartInstanceLocation = startInstanceLocation,
	};
	::memcpy_s(currentWritePtr, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), &args, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
	currentWritePtr += sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
}

void D3DIndirectCommandGenerator::writeDispatchArguments(
	uint32 threadGroupCountX,
	uint32 threadGroupCountY,
	uint32 threadGroupCountZ)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_DISPATCH_ARGUMENTS args{
		.ThreadGroupCountX = threadGroupCountX,
		.ThreadGroupCountY = threadGroupCountY,
		.ThreadGroupCountZ = threadGroupCountZ
	};
	::memcpy_s(currentWritePtr, sizeof(D3D12_DISPATCH_ARGUMENTS), &args, sizeof(D3D12_DISPATCH_ARGUMENTS));
	currentWritePtr += sizeof(D3D12_DISPATCH_ARGUMENTS);
}

void D3DIndirectCommandGenerator::writeConstantBufferView(ConstantBufferView* view)
{
	D3D12_GPU_VIRTUAL_ADDRESS addr = static_cast<D3DConstantBufferView*>(view)->getGPUVirtualAddress();
	::memcpy_s(currentWritePtr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS), &addr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
	currentWritePtr += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
}

void D3DIndirectCommandGenerator::writeShaderResourceView(ShaderResourceView* view)
{
	D3D12_GPU_VIRTUAL_ADDRESS addr = static_cast<D3DShaderResourceView*>(view)->getGPUVirtualAddress();
	::memcpy_s(currentWritePtr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS), &addr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
	currentWritePtr += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
}

void D3DIndirectCommandGenerator::writeUnorderedAccessView(UnorderedAccessView* view)
{
	D3D12_GPU_VIRTUAL_ADDRESS addr = static_cast<D3DUnorderedAccessView*>(view)->getGPUVirtualAddress();
	::memcpy_s(currentWritePtr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS), &addr, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
	currentWritePtr += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
}

void D3DIndirectCommandGenerator::writeDispatchMeshArguments(
	uint32 threadGroupCountX,
	uint32 threadGroupCountY,
	uint32 threadGroupCountZ)
{
	CHECK(currentWritePtr != nullptr);
	D3D12_DISPATCH_MESH_ARGUMENTS args{
		.ThreadGroupCountX = threadGroupCountX,
		.ThreadGroupCountY = threadGroupCountY,
		.ThreadGroupCountZ = threadGroupCountZ
	};
	::memcpy_s(currentWritePtr, sizeof(D3D12_DISPATCH_MESH_ARGUMENTS), &args, sizeof(D3D12_DISPATCH_MESH_ARGUMENTS));
	currentWritePtr += sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
}

void D3DIndirectCommandGenerator::endCommand()
{
	CHECK(currentWritePtr != nullptr);
	::memset(currentWritePtr, 0, paddingBytes);
	currentWritePtr = nullptr;
}

void D3DIndirectCommandGenerator::copyToBuffer(RenderCommandList* commandList, uint32 numCommands, Buffer* destBuffer, uint64 destOffset)
{
	CHECK(numCommands <= maxCommandCount);
	destBuffer->singleWriteToGPU(commandList, memblock, byteStride * numCommands, destOffset);
}
