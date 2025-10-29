#include "d3d_into.h"
#include "d3d_shader.h"
#include "d3d_pipeline_state.h"
#include "d3d_resource.h"
#include "d3d_buffer.h"

namespace into_d3d
{
	void graphicsPipelineDesc(
		const GraphicsPipelineDesc& inDesc,
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& outDesc,
		TempAlloc& tempAlloc)
	{
		::memset(&outDesc, 0, sizeof(outDesc));

		outDesc.pRootSignature = NULL; // You must provide this on your own.
		if (inDesc.vs != nullptr) outDesc.VS = static_cast<D3DShaderStage*>(inDesc.vs)->getBytecode();
		if (inDesc.ps != nullptr) outDesc.PS = static_cast<D3DShaderStage*>(inDesc.ps)->getBytecode();
		if (inDesc.ds != nullptr) outDesc.DS = static_cast<D3DShaderStage*>(inDesc.ds)->getBytecode();
		if (inDesc.hs != nullptr) outDesc.HS = static_cast<D3DShaderStage*>(inDesc.hs)->getBytecode();
		if (inDesc.gs != nullptr) outDesc.GS = static_cast<D3DShaderStage*>(inDesc.gs)->getBytecode();
		blendDesc(inDesc.blendDesc, outDesc.BlendState);
		outDesc.SampleMask = inDesc.sampleMask;
		rasterizerDesc(inDesc.rasterizerDesc, outDesc.RasterizerState);
		depthstencilDesc(inDesc.depthstencilDesc, outDesc.DepthStencilState);
		inputLayout(inDesc.inputLayout, outDesc.InputLayout, tempAlloc);
		outDesc.PrimitiveTopologyType = primitiveTopologyType(inDesc.primitiveTopologyType);
		outDesc.NumRenderTargets = inDesc.numRenderTargets;
		for (uint32 i = 0; i < 8; ++i)
		{
			outDesc.RTVFormats[i] = pixelFormat(inDesc.rtvFormats[i]);
		}
		outDesc.DSVFormat = pixelFormat(inDesc.dsvFormat);
		sampleDesc(inDesc.sampleDesc, outDesc.SampleDesc);
	}

	D3D12_BARRIER_SYNC barrierSync(EBarrierSync sync)
	{
		return (D3D12_BARRIER_SYNC)sync;
	}

	D3D12_BARRIER_ACCESS barrierAccess(EBarrierAccess access)
	{
		return (D3D12_BARRIER_ACCESS)access;
	}

	D3D12_BARRIER_LAYOUT barrierLayout(EBarrierLayout layout)
	{
		// D3D12_BARRIER_LAYOUT uses the same value for COMMON and PRESENT...
		if (layout == EBarrierLayout::Common)
		{
			return D3D12_BARRIER_LAYOUT_COMMON;
		}
		return (D3D12_BARRIER_LAYOUT)layout;
	}

	D3D12_BARRIER_SUBRESOURCE_RANGE barrierSubresourceRange(const BarrierSubresourceRange& range)
	{
		D3D12_BARRIER_SUBRESOURCE_RANGE d3dRange{
			.IndexOrFirstMipLevel = range.indexOrFirstMipLevel,
			.NumMipLevels         = range.numMipLevels,
			.FirstArraySlice      = range.firstArraySlice,
			.NumArraySlices       = range.numArraySlices,
			.FirstPlane           = range.firstPlane,
			.NumPlanes            = range.numPlanes,
		};
		return d3dRange;
	}

	D3D12_TEXTURE_BARRIER_FLAGS textureBarrierFlags(ETextureBarrierFlags flags)
	{
		return (D3D12_TEXTURE_BARRIER_FLAGS)flags;
	}

	D3D12_BUFFER_BARRIER bufferBarrier(const BufferBarrier& barrier)
	{
		D3D12_BUFFER_BARRIER d3dBarrier{
			.SyncBefore   = into_d3d::barrierSync(barrier.syncBefore),
			.SyncAfter    = into_d3d::barrierSync(barrier.syncAfter),
			.AccessBefore = into_d3d::barrierAccess(barrier.accessBefore),
			.AccessAfter  = into_d3d::barrierAccess(barrier.accessAfter),
			.pResource    = into_d3d::id3d12Resource(barrier.buffer),
			.Offset       = 0,
			.Size         = UINT64_MAX,
		};
		return d3dBarrier;
	}

	D3D12_TEXTURE_BARRIER textureBarrier(const TextureBarrier& barrier)
	{
		D3D12_TEXTURE_BARRIER d3dBarrier{
			.SyncBefore   = into_d3d::barrierSync(barrier.syncBefore),
			.SyncAfter    = into_d3d::barrierSync(barrier.syncAfter),
			.AccessBefore = into_d3d::barrierAccess(barrier.accessBefore),
			.AccessAfter  = into_d3d::barrierAccess(barrier.accessAfter),
			.LayoutBefore = into_d3d::barrierLayout(barrier.layoutBefore),
			.LayoutAfter  = into_d3d::barrierLayout(barrier.layoutAfter),
			.pResource    = into_d3d::id3d12Resource(barrier.texture),
			.Subresources = into_d3d::barrierSubresourceRange(barrier.subresources),
			.Flags        = into_d3d::textureBarrierFlags(barrier.flags),
		};
		return d3dBarrier;
	}

	D3D12_GLOBAL_BARRIER globalBarrier(const GlobalBarrier& barrier)
	{
		D3D12_GLOBAL_BARRIER d3dBarrier{
			.SyncBefore   = into_d3d::barrierSync(barrier.syncBefore),
			.SyncAfter    = into_d3d::barrierSync(barrier.syncAfter),
			.AccessBefore = into_d3d::barrierAccess(barrier.accessBefore),
			.AccessAfter  = into_d3d::barrierAccess(barrier.accessAfter),
		};
		return d3dBarrier;
	}

	void raytracingGeometryDesc(const RaytracingGeometryDesc& inDesc, D3D12_RAYTRACING_GEOMETRY_DESC& outDesc)
	{
		outDesc.Type = raytracingGeometryType(inDesc.type);
		outDesc.Flags = raytracingGeometryFlags(inDesc.flags);

		if (inDesc.type == ERaytracingGeometryType::Triangles)
		{
			D3D12_VERTEX_BUFFER_VIEW vbuf = static_cast<D3DVertexBuffer*>(inDesc.triangles.vertexBuffer)->getVertexBufferView();
			D3D12_INDEX_BUFFER_VIEW ibuf = static_cast<D3DIndexBuffer*>(inDesc.triangles.indexBuffer)->getIndexBufferView();

			if (inDesc.triangles.transform3x4Buffer == nullptr)
			{
				outDesc.Triangles.Transform3x4 = 0;
			}
			else
			{
				D3DBuffer* tbuf = static_cast<D3DBuffer*>(inDesc.triangles.transform3x4Buffer);
				D3D12_GPU_VIRTUAL_ADDRESS addr = into_d3d::id3d12Resource(tbuf)->GetGPUVirtualAddress();
				addr += inDesc.triangles.transformIndex * 48; // 48 = sizeof(transform3x4)
				outDesc.Triangles.Transform3x4 = addr;
			}
			outDesc.Triangles.IndexFormat = pixelFormat(inDesc.triangles.indexFormat);
			outDesc.Triangles.VertexFormat = pixelFormat(inDesc.triangles.vertexFormat);
			outDesc.Triangles.IndexCount = inDesc.triangles.indexCount;
			outDesc.Triangles.VertexCount = inDesc.triangles.vertexCount;
			outDesc.Triangles.IndexBuffer = ibuf.BufferLocation;
			outDesc.Triangles.VertexBuffer.StartAddress = vbuf.BufferLocation;
			outDesc.Triangles.VertexBuffer.StrideInBytes = vbuf.StrideInBytes;
		}
		else if (inDesc.type == ERaytracingGeometryType::ProceduralPrimitiveAABB)
		{
			// #todo-dxr: ProceduralPrimitiveAABB
			CHECK_NO_ENTRY();
		}
		else
		{
			CHECK_NO_ENTRY();
		}
	}

	void dispatchRaysDesc(const DispatchRaysDesc& inDesc, D3D12_DISPATCH_RAYS_DESC& outDesc)
	{
		auto getGpuAddress = [](RaytracingShaderTable* table) {
			return static_cast<D3DRaytracingShaderTable*>(table)->getGpuVirtualAddress();
		};
		auto getSizeInBytes = [](RaytracingShaderTable* table) {
			return static_cast<D3DRaytracingShaderTable*>(table)->getSizeInBytes();
		};
		auto getStrideInBytes = [](RaytracingShaderTable* table) {
			return static_cast<D3DRaytracingShaderTable*>(table)->getStrideInBytes();
		};

		outDesc = D3D12_DISPATCH_RAYS_DESC{
			.RayGenerationShaderRecord = D3D12_GPU_VIRTUAL_ADDRESS_RANGE{ getGpuAddress(inDesc.raygenShaderTable), getSizeInBytes(inDesc.raygenShaderTable) },
			.MissShaderTable           = D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE{ getGpuAddress(inDesc.missShaderTable), getSizeInBytes(inDesc.missShaderTable), getStrideInBytes(inDesc.missShaderTable) },
			.HitGroupTable             = D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE{ getGpuAddress(inDesc.hitGroupTable), getSizeInBytes(inDesc.hitGroupTable), getStrideInBytes(inDesc.hitGroupTable) },
			.CallableShaderTable       = D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE{ 0, 0, 0 }, // #todo-dxr: CallableShaderTable for dispatchRays()
			.Width                     = inDesc.width,
			.Height                    = inDesc.height,
			.Depth                     = inDesc.depth,
		};
	}

	template<typename TPipelineState>
	void indirectArgument(const IndirectArgumentDesc& inDesc, D3D12_INDIRECT_ARGUMENT_DESC& outDesc, TPipelineState* pipelineState)
	{
		auto findParam = [pipelineState](const std::string& pname) -> const D3DShaderParameter* {
			if constexpr (std::is_same_v<TPipelineState, D3DGraphicsPipelineState>)
			{
				return pipelineState->findShaderParameter(pname);
			}
			if constexpr (std::is_same_v<TPipelineState, D3DComputePipelineState>)
			{
				return pipelineState->findShaderParameter(pname);
			}
			if constexpr (std::is_same_v<TPipelineState, D3DRaytracingPipelineStateObject>)
			{
				return pipelineState->findGlobalShaderParameter(pname);
			}
		};

		outDesc.Type = into_d3d::indirectArgumentType(inDesc.type);
		if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW)
		{
			outDesc.VertexBuffer.Slot = inDesc.vertexBuffer.slot;
		}
		else if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT)
		{
			CHECK(pipelineState != nullptr);
			outDesc.Constant.RootParameterIndex = findParam(inDesc.name)->rootParameterIndex;
			outDesc.Constant.DestOffsetIn32BitValues = inDesc.constant.destOffsetIn32BitValues;
			outDesc.Constant.Num32BitValuesToSet = inDesc.constant.num32BitValuesToSet;
		}
		else if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW)
		{
			CHECK(pipelineState != nullptr);
			outDesc.ConstantBufferView.RootParameterIndex = findParam(inDesc.name)->rootParameterIndex;
		}
		else if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW)
		{
			CHECK(pipelineState != nullptr);
			outDesc.ShaderResourceView.RootParameterIndex = findParam(inDesc.name)->rootParameterIndex;
		}
		else if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW)
		{
			CHECK(pipelineState != nullptr);
			outDesc.UnorderedAccessView.RootParameterIndex = findParam(inDesc.name)->rootParameterIndex;
		}
	}

	uint32 calcIndirectArgumentByteStride(const IndirectArgumentDesc& inDesc)
	{
		switch (inDesc.type)
		{
			case EIndirectArgumentType::DRAW:                  return sizeof(D3D12_DRAW_ARGUMENTS);
			case EIndirectArgumentType::DRAW_INDEXED:          return sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
			case EIndirectArgumentType::DISPATCH:              return sizeof(D3D12_DISPATCH_ARGUMENTS);
			case EIndirectArgumentType::VERTEX_BUFFER_VIEW:    return sizeof(D3D12_VERTEX_BUFFER_VIEW);
			case EIndirectArgumentType::INDEX_BUFFER_VIEW:     return sizeof(D3D12_INDEX_BUFFER_VIEW);
			case EIndirectArgumentType::CONSTANT:              return (4 * inDesc.constant.num32BitValuesToSet);
			case EIndirectArgumentType::CONSTANT_BUFFER_VIEW:  return sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
			case EIndirectArgumentType::SHADER_RESOURCE_VIEW:  return sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
			case EIndirectArgumentType::UNORDERED_ACCESS_VIEW: return sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
			case EIndirectArgumentType::DISPATCH_RAYS:         return sizeof(D3D12_DISPATCH_RAYS_DESC);
			case EIndirectArgumentType::DISPATCH_MESH:         return sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
		}
		return 0;
	}

	uint32 calcCommandSignatureByteStride(const CommandSignatureDesc& inDesc, uint32& outPaddingBytes)
	{
		uint32 byteStride = 0;
		for (const IndirectArgumentDesc& desc : inDesc.argumentDescs)
		{
			byteStride += into_d3d::calcIndirectArgumentByteStride(desc);
		}
		uint32 unpaddedStride = byteStride;
		byteStride = (byteStride + 3) & ~3; // DirectX-Specs: 4-byte aligned
		outPaddingBytes = byteStride - unpaddedStride;
		return byteStride;
	}

	template<typename TPipelineState>
	void commandSignature(const CommandSignatureDesc& inDesc, D3D12_COMMAND_SIGNATURE_DESC& outDesc, TPipelineState* pipelineState, TempAlloc& tempAlloc)
	{
		uint32 numArgumentDescs = (uint32)inDesc.argumentDescs.size();
		D3D12_INDIRECT_ARGUMENT_DESC* tempArgumentDescs = tempAlloc.allocIndirectArgumentDescs(numArgumentDescs);
		for (uint32 i = 0; i < numArgumentDescs; ++i)
		{
			into_d3d::indirectArgument(inDesc.argumentDescs[i], tempArgumentDescs[i], pipelineState);
		}

		uint32 unusedPaddingBytes;

		outDesc.ByteStride = into_d3d::calcCommandSignatureByteStride(inDesc, unusedPaddingBytes);
		outDesc.NumArgumentDescs = numArgumentDescs;
		outDesc.pArgumentDescs = tempArgumentDescs;
		outDesc.NodeMask = inDesc.nodeMask;
	}
}

// Explicit template instantiation
template void into_d3d::indirectArgument<D3DGraphicsPipelineState>(const IndirectArgumentDesc& inDesc, D3D12_INDIRECT_ARGUMENT_DESC& outDesc, D3DGraphicsPipelineState* pipelineState);
template void into_d3d::indirectArgument<D3DRaytracingPipelineStateObject>(const IndirectArgumentDesc& inDesc, D3D12_INDIRECT_ARGUMENT_DESC& outDesc, D3DRaytracingPipelineStateObject* pipelineState);

template void into_d3d::commandSignature<D3DGraphicsPipelineState>(const CommandSignatureDesc& inDesc, D3D12_COMMAND_SIGNATURE_DESC& outDesc, D3DGraphicsPipelineState* pipelineState, TempAlloc& tempAlloc);
template void into_d3d::commandSignature<D3DComputePipelineState>(const CommandSignatureDesc& inDesc, D3D12_COMMAND_SIGNATURE_DESC& outDesc, D3DComputePipelineState* pipelineState, TempAlloc& tempAlloc);
template void into_d3d::commandSignature<D3DRaytracingPipelineStateObject>(const CommandSignatureDesc& inDesc, D3D12_COMMAND_SIGNATURE_DESC& outDesc, D3DRaytracingPipelineStateObject* pipelineState, TempAlloc& tempAlloc);
