#include "common.hlsl"
#include "material.hlsl"
#include "indirect_draw_common.hlsl"

#define IDrawCommand StaticMeshDrawCommand

// ------------------------------------------------------------------------
// Resource bindings

struct PassUniform
{
	// #wip: Make sure all static meshes share the same vertex/index buffers.
	D3D12_GPU_VIRTUAL_ADDRESS vertexBufferPoolAddress;
	D3D12_GPU_VIRTUAL_ADDRESS indexBufferPoolAddress;
	uint                      rawDeviceFormatR16UInt;
	uint                      rawDeviceFormatR32UInt;
};

ConstantBuffer<PassUniform>      passUniform;
StructuredBuffer<GPUSceneItem>   gpuSceneBuffer;
StructuredBuffer<Material>       materialBuffer;
StructuredBuffer<uint>           drawOffsetBuffer;
RWStructuredBuffer<IDrawCommand> drawCommandBuffer;
RWBuffer<uint>                   drawCounterBuffer;

// ------------------------------------------------------------------------
// Compute shader

D3D12_VERTEX_BUFFER_VIEW createVertexBufferView(uint bufferOffset, uint sizeAndStridePacked)
{
	uint2 countAndStride = unpackVertexCountAndStride(sizeAndStridePacked);
	
	D3D12_VERTEX_BUFFER_VIEW view;
	view.BufferLocation = passUniform.vertexBufferPoolAddress;
	if (view.BufferLocation.x > 0xffffffff - bufferOffset)
	{
		view.BufferLocation.y += 1;
		uint msb = (1 << 31);
		view.BufferLocation.x = (view.BufferLocation.x & ~msb) + (bufferOffset & ~msb);
	}
	else
	{
		view.BufferLocation.x += bufferOffset;
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
	if (view.BufferLocation.x > 0xffffffff - bufferOffset)
	{
		view.BufferLocation.y += 1;
		uint msb = (1 << 31);
		view.BufferLocation.x = (view.BufferLocation.x & ~msb) + (bufferOffset & ~msb);
	}
	else
	{
		view.BufferLocation.x += bufferOffset;
	}
	view.SizeInBytes = sizeAndFormat.x;
	// See packIndexSizeAndFormat()
	view.Format = (sizeAndFormat.y == 2) ? passUniform.rawDeviceFormatR16UInt : passUniform.rawDeviceFormatR32UInt;
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
	
	uint drawID;
	InterlockedAdd(drawCounterBuffer[material.pipelineFreeNumber], 1, drawID);
	
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

	uint drawIDOffset = drawOffsetBuffer.Load(material.pipelineFreeNumber);
	drawCommandBuffer[drawIDOffset + drawID] = cmd;
}
