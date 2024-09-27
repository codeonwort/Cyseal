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

	D3D12_RESOURCE_STATES bufferMemoryLayout(EBufferMemoryLayout layout)
	{
		switch (layout)
		{
			case EBufferMemoryLayout::COMMON                : return D3D12_RESOURCE_STATE_COMMON;
			case EBufferMemoryLayout::PIXEL_SHADER_RESOURCE : return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			case EBufferMemoryLayout::UNORDERED_ACCESS      : return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			case EBufferMemoryLayout::COPY_SRC              : return D3D12_RESOURCE_STATE_COPY_SOURCE;
			case EBufferMemoryLayout::COPY_DEST             : return D3D12_RESOURCE_STATE_COPY_DEST;
			case EBufferMemoryLayout::INDIRECT_ARGUMENT     : return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		}
		CHECK_NO_ENTRY();
		return D3D12_RESOURCE_STATE_COMMON;
	}

	D3D12_RESOURCE_STATES textureMemoryLayout(ETextureMemoryLayout layout)
	{
		switch (layout)
		{
			case ETextureMemoryLayout::COMMON                : return D3D12_RESOURCE_STATE_COMMON;
			case ETextureMemoryLayout::RENDER_TARGET         : return D3D12_RESOURCE_STATE_RENDER_TARGET;
			case ETextureMemoryLayout::DEPTH_STENCIL_TARGET  : return D3D12_RESOURCE_STATE_DEPTH_WRITE;
			case ETextureMemoryLayout::PIXEL_SHADER_RESOURCE : return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			case ETextureMemoryLayout::UNORDERED_ACCESS      : return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			case ETextureMemoryLayout::COPY_SRC              : return D3D12_RESOURCE_STATE_COPY_SOURCE;
			case ETextureMemoryLayout::COPY_DEST             : return D3D12_RESOURCE_STATE_COPY_DEST;
			case ETextureMemoryLayout::PRESENT               : return D3D12_RESOURCE_STATE_PRESENT;
		}
		CHECK_NO_ENTRY();
		return D3D12_RESOURCE_STATE_COMMON;
	}

	D3D12_RESOURCE_BARRIER resourceBarrier(const BufferMemoryBarrier& barrier)
	{
		D3D12_RESOURCE_BARRIER d3dBarrier{
			.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
			.Flags      = D3D12_RESOURCE_BARRIER_FLAG_NONE,
			.Transition = {
				.pResource   = into_d3d::id3d12Resource(barrier.buffer),
				// #todo-barrier: offset and size like VkBufferMemoryBarrier?
				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
				.StateBefore = into_d3d::bufferMemoryLayout(barrier.stateBefore),
				.StateAfter  = into_d3d::bufferMemoryLayout(barrier.stateAfter),
			},
		};
		return d3dBarrier;
	}

	D3D12_RESOURCE_BARRIER resourceBarrier(const TextureMemoryBarrier& barrier)
	{
		D3D12_RESOURCE_BARRIER d3dBarrier{
			.Type       = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
			.Flags      = D3D12_RESOURCE_BARRIER_FLAG_NONE,
			.Transition = {
				.pResource   = into_d3d::id3d12Resource(barrier.texture),
				.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, // #todo-barrier: DX12 texture subresource
				.StateBefore = into_d3d::textureMemoryLayout(barrier.stateBefore),
				.StateAfter  = into_d3d::textureMemoryLayout(barrier.stateAfter),
			},
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

	void indirectArgument(const IndirectArgumentDesc& inDesc, D3D12_INDIRECT_ARGUMENT_DESC& outDesc, D3DGraphicsPipelineState* pipelineState)
	{
		outDesc.Type = into_d3d::indirectArgumentType(inDesc.type);
		if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW)
		{
			outDesc.VertexBuffer.Slot = inDesc.vertexBuffer.slot;
		}
		else if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT)
		{
			outDesc.Constant.RootParameterIndex = pipelineState->findShaderParameter(inDesc.name)->rootParameterIndex;
			outDesc.Constant.DestOffsetIn32BitValues = inDesc.constant.destOffsetIn32BitValues;
			outDesc.Constant.Num32BitValuesToSet = inDesc.constant.num32BitValuesToSet;
		}
		else if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW)
		{
			outDesc.ConstantBufferView.RootParameterIndex = inDesc.constantBufferView.rootParameterIndex;
		}
		else if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW)
		{
			outDesc.ShaderResourceView.RootParameterIndex = inDesc.shaderResourceView.rootParameterIndex;
		}
		else if (outDesc.Type == D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW)
		{
			outDesc.UnorderedAccessView.RootParameterIndex = inDesc.unorderedAccessView.rootParameterIndex;
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
			case EIndirectArgumentType::DISPATCH_RAYS:         CHECK_NO_ENTRY(); return 0; // #todo-indirect-draw
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

	void commandSignature(
		const CommandSignatureDesc& inDesc,
		D3D12_COMMAND_SIGNATURE_DESC& outDesc,
		D3DGraphicsPipelineState* pipelineState,
		TempAlloc& tempAlloc)
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
