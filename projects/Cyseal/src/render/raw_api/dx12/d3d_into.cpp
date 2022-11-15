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

		outDesc.pRootSignature = static_cast<D3DRootSignature*>(inDesc.rootSignature)->getRaw();
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

	void computePipelineDesc(
		const ComputePipelineDesc& inDesc,
		D3D12_COMPUTE_PIPELINE_STATE_DESC& outDesc)
	{
		::memset(&outDesc, 0, sizeof(outDesc));
		CHECK(inDesc.cs != nullptr);

		outDesc.pRootSignature = static_cast<D3DRootSignature*>(inDesc.rootSignature)->getRaw();
		outDesc.CS = static_cast<D3DShaderStage*>(inDesc.cs)->getBytecode();
		outDesc.NodeMask = (UINT)inDesc.nodeMask;
		// #todo-dx12: Compute shader - CachedPSO, Flags
		outDesc.CachedPSO.pCachedBlob = NULL;
		outDesc.CachedPSO.CachedBlobSizeInBytes = 0;
		outDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	}

	D3D12_RESOURCE_BARRIER resourceBarrier(const ResourceBarrier& barrier)
	{
		ID3D12Resource* d3dResource = static_cast<D3DResource*>(barrier.resource)->getRaw();

		D3D12_RESOURCE_BARRIER d3dBarrier;
		d3dBarrier.Type = (D3D12_RESOURCE_BARRIER_TYPE)barrier.type;
		d3dBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		switch (barrier.type)
		{
			case EResourceBarrierType::Transition:
				d3dBarrier.Transition.pResource = d3dResource;
				// #todo-barrier: Subresource index?
				d3dBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				d3dBarrier.Transition.StateBefore = (D3D12_RESOURCE_STATES)barrier.stateBefore;
				d3dBarrier.Transition.StateAfter = (D3D12_RESOURCE_STATES)barrier.stateAfter;
				break;
			case EResourceBarrierType::Aliasing:
				CHECK_NO_ENTRY();
				break;
			case EResourceBarrierType::UAV:
				CHECK_NO_ENTRY();
				break;
			default:
				break;
		}
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
				D3DStructuredBuffer* tbuf = static_cast<D3DStructuredBuffer*>(inDesc.triangles.transform3x4Buffer);
				D3D12_GPU_VIRTUAL_ADDRESS addr = tbuf->getGPUVirtualAddress();
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
			// #todo-wip-rt: AABB
			CHECK_NO_ENTRY();
		}
		else
		{
			CHECK_NO_ENTRY();
		}
	}

}
