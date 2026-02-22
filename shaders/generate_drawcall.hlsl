#include "common.hlsl"
#include "material.hlsl"
#include "indirect_draw_common.hlsl"

#define IDrawCommand StaticMeshDrawCommand

// #wip: Hard-coded for DirectX
#define DXGI_FORMAT_R16_UINT 57
#define DXGI_FORMAT_R32_UINT 42

// ------------------------------------------------------------------------
// Resource bindings

// #wip: Add uniform parameters for each pipeline permutation.
struct PassUniform
{
	// #wip: Make sure all static meshes share the same vertex/index buffers.
	D3D12_GPU_VIRTUAL_ADDRESS vertexBufferPoolAddress;
	D3D12_GPU_VIRTUAL_ADDRESS indexBufferPoolAddress;
	uint                      pipelineKey; // Target pipeline key
	uint3                     _pad0;
};

ConstantBuffer<PassUniform>      passUniform;
StructuredBuffer<GPUSceneItem>   gpuSceneBuffer;
StructuredBuffer<Material>       materialBuffer;
RWStructuredBuffer<IDrawCommand> drawCommandBuffer;
RWBuffer<uint>                   drawCounterBuffer;

// ------------------------------------------------------------------------
// Compute shader

D3D12_VERTEX_BUFFER_VIEW createVertexBufferView(uint bufferOffset, uint sizeAndStridePacked)
{
	uint2 countAndStride = unpackVertexCountAndStride(sizeAndStridePacked);
	
	D3D12_VERTEX_BUFFER_VIEW view;
	view.BufferLocation = passUniform.vertexBufferPoolAddress;
	if (view.BufferLocation.y > 0xffffffff - bufferOffset)
	{
		view.BufferLocation.x += 1;
		uint msb = (1 << 31);
		view.BufferLocation.y = (view.BufferLocation.y & ~msb) + (bufferOffset & ~msb);
	}
	else
	{
		view.BufferLocation.y += bufferOffset;
	}
	view.SizeInBytes = countAndStride.x * countAndStride.y;
	view.StrideInBytes = countAndStride.y;
	return view;
}

D3D12_INDEX_BUFFER_VIEW createIndexBufferView(uint bufferOffset, uint sizeAndFormatPacked)
{
	uint2 sizeAndFormat = unpackIndexSizeAndFormat(sizeAndFormatPacked);
	
	D3D12_INDEX_BUFFER_VIEW view;
	view.BufferLocation = passUniform.indexBufferPoolAddress;
	if (view.BufferLocation.y > 0xffffffff - bufferOffset)
	{
		view.BufferLocation.x += 1;
		uint msb = (1 << 31);
		view.BufferLocation.y = (view.BufferLocation.y & ~msb) + (bufferOffset & ~msb);
	}
	else
	{
		view.BufferLocation.y += bufferOffset;
	}
	view.SizeInBytes = sizeAndFormat.x;
	view.Format = (sizeAndFormat.y == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	return view;
}

[numthreads(1, 1, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	GPUSceneItem sceneItem = gpuSceneBuffer.Load(tid.x);
	
	if ((sceneItem.flags & GPU_SCENE_ITEM_FLAG_BIT_IS_VALID) == 0)
	{
		return;
	}
	
	Material material = materialBuffer.Load(tid.x);
	
	if (material.pipelineKey != passUniform.pipelineKey)
	{
		return;
	}
	
	uint drawID;
	InterlockedAdd(drawCounterBuffer[0], 1, drawID);
	
	IDrawCommand cmd;
	cmd.sceneItemIndex = tid.x;
	cmd.positionBufferView    = createVertexBufferView(sceneItem.positionBufferOffset, sceneItem.positionSizeAndStridePacked);
	cmd.nonPositionBufferView = createVertexBufferView(sceneItem.nonPositionBufferOffset, sceneItem.nonPositionSizeAndStridePacked);
	cmd.indexBufferView       = createIndexBufferView(sceneItem.indexBufferOffset, sceneItem.indexSizeAndFormatPacked);
	cmd.drawIndexedArguments.IndexCountPerInstance = sceneItem.indexCount;
	cmd.drawIndexedArguments.InstanceCount         = 1;
	cmd.drawIndexedArguments.StartIndexLocation    = 0;
	cmd.drawIndexedArguments.BaseVertexLocation    = 0;
	cmd.drawIndexedArguments.StartInstanceLocation = 0;

	drawCommandBuffer[drawID] = cmd;
}
