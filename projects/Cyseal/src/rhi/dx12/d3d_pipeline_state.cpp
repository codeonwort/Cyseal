#include "d3d_pipeline_state.h"
#include "d3d_buffer.h"
#include "d3d_into.h"
#include "rhi/render_command.h"
#include "rhi/buffer.h"
#include "util/logging.h"

// --------------------------------------------------------
// common

DEFINE_LOG_CATEGORY_STATIC(LogD3DPipelineState);

static void buildShaderParameterTable(D3DShaderParameterTable& dstTable, const std::vector<ShaderStage*>& shaderStages)
{
	struct InvalidParamInfo
	{
		D3DShaderStage* shader;
		D3DShaderParameter validParam;
		D3DShaderParameter invalidParam;
	};
	std::vector<InvalidParamInfo> invalidParamInfo;
	std::map<std::string, D3DShaderParameter> tempCache;
	auto appendParameters = [&invalidParamInfo, &tempCache](std::vector<D3DShaderParameter>& dst, const std::vector<D3DShaderParameter>& src, D3DShaderStage* srcShader) {
		for (const D3DShaderParameter& param : src)
		{
			auto it = tempCache.find(param.name);
			if (it == tempCache.end())
			{
				tempCache.insert(std::make_pair(param.name, param));
				dst.push_back(param);
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
		appendParameters(dstTable.rootConstants,       srcTable.rootConstants,       d3dShader);
		appendParameters(dstTable.constantBuffers,     srcTable.constantBuffers,     d3dShader);
		appendParameters(dstTable.rwStructuredBuffers, srcTable.rwStructuredBuffers, d3dShader);
		appendParameters(dstTable.rwBuffers,           srcTable.rwBuffers,           d3dShader);
		appendParameters(dstTable.structuredBuffers,   srcTable.structuredBuffers,   d3dShader);
		appendParameters(dstTable.textures,            srcTable.textures,            d3dShader);
		appendParameters(dstTable.samplers,            srcTable.samplers,            d3dShader);
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
static void createRootSignatureFromParameterTable(WRL::ComPtr<ID3D12RootSignature>& outRootSignature, ID3D12Device* device, D3DShaderParameterTable& inoutParameterTable)
{
	D3DShaderParameterTable& parameterTable = inoutParameterTable;

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
			.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
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
	build(parameterTable.textures);
	build(parameterTable.samplers);
}

// --------------------------------------------------------
// D3DGraphicsPipelineState

void D3DGraphicsPipelineState::initialize(ID3D12Device* device, const GraphicsPipelineDesc& inDesc)
{
	createRootSignature(device, inDesc.vs, inDesc.ps, inDesc.ds, inDesc.hs, inDesc.gs);

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

void D3DGraphicsPipelineState::createRootSignature(ID3D12Device* device, ShaderStage* inVertexShader, ShaderStage* inPixelShader, ShaderStage* inDomainShader, ShaderStage* inHullShader, ShaderStage* inGeometryShader)
{
	// #wip-dxc-reflection: Impose after converting all render passes.
	//CHECK(bPushConstantsDeclared);

	D3DShaderStage* vs = static_cast<D3DShaderStage*>(inVertexShader);
	D3DShaderStage* ps = static_cast<D3DShaderStage*>(inPixelShader);
	D3DShaderStage* ds = static_cast<D3DShaderStage*>(inDomainShader);
	D3DShaderStage* hs = static_cast<D3DShaderStage*>(inHullShader);
	D3DShaderStage* gs = static_cast<D3DShaderStage*>(inGeometryShader);

	buildShaderParameterTable(parameterTable, { vs, ps, ds, hs, gs });
	createRootSignatureFromParameterTable(rootSignature, device, parameterTable);
	createShaderParameterHashMap(parameterHashMap, parameterTable);
}

// --------------------------------------------------------
// D3DComputePipelineState

void D3DComputePipelineState::initialize(ID3D12Device* device, const ComputePipelineDesc& inDesc)
{
	CHECK(inDesc.cs != nullptr);
	D3DShaderStage* shaderStage = static_cast<D3DShaderStage*>(inDesc.cs);

	createRootSignature(device, shaderStage);

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

void D3DComputePipelineState::createRootSignature(ID3D12Device* device, D3DShaderStage* computeShader)
{
	// #wip-dxc-reflection: Impose after converting all render passes.
	//CHECK(bPushConstantsDeclared);

	parameterTable = computeShader->getParameterTable(); // There is only one shader, just do deep copy rather than calling buildShaderParameterTable().
	createRootSignatureFromParameterTable(rootSignature, device, parameterTable);
	createShaderParameterHashMap(parameterHashMap, parameterTable);
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
