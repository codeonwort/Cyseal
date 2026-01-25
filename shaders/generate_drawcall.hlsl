#include "common.hlsl"
#include "indirect_arguments.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

// #wip: Add uniform parameters for each pipeline permutation.
struct PassUniform
{
	// #wip: Make sure all static meshes share the same vertex/index buffers.
	D3D12_GPU_VIRTUAL_ADDRESS vertexBufferPoolAddress;
	D3D12_GPU_VIRTUAL_ADDRESS indexBufferPoolAddress;
	uint maxDrawCommands;
	uint pipelineKey;
};

// #wip: Specific to base pass
struct StaticMeshDrawCommand
{
	uint                         sceneItemIndex; // index in gpu scene buffer
	D3D12_VERTEX_BUFFER_VIEW     positionBufferView;
	D3D12_VERTEX_BUFFER_VIEW     nonPositionBufferView;
	D3D12_INDEX_BUFFER_VIEW      indexBufferView;
	D3D12_DRAW_INDEXED_ARGUMENTS drawIndexedArguments;
};

ConstantBuffer<PassUniform>      passUniform;
StructuredBuffer<GPUSceneItem>   gpuSceneBuffer;
RWStructuredBuffer<IDrawCommand> drawCommandBuffer;
RWBuffer<uint>                   drawCounterBuffer;

// ------------------------------------------------------------------------
// Compute shader

[numthreads(1, 1, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	GPUSceneItem sceneItem = gpuSceneBuffer.Load(tid.x);
	
	if ((sceneItem.flags & GPU_SCENE_ITEM_FLAG_BIT_IS_VALID) == 0)
	{
		return;
	}
	
	uint drawID;
	InterlockedAdd(drawCounterBuffer[0], 1, drawID);
	
	IDrawCommand cmd;
	cmd.sceneItemIndex = tid.x;
	// #wip: Fill cmd

	drawCommandBuffer[drawID] = cmd;
}
