#include "common.hlsl"
#include "material.hlsl"
#include "indirect_draw_common.hlsl"

#define IDrawCommand StaticMeshDrawCommand

// ------------------------------------------------------------------------
// Resource bindings

// #wip: Add uniform parameters for each pipeline permutation.
struct PassUniform
{
	// #wip: Make sure all static meshes share the same vertex/index buffers.
	D3D12_GPU_VIRTUAL_ADDRESS vertexBufferPoolAddress;
	D3D12_GPU_VIRTUAL_ADDRESS indexBufferPoolAddress;
	uint maxDrawCommands;
	uint pipelineKey; // Target pipeline key
};

ConstantBuffer<PassUniform>      passUniform;
StructuredBuffer<GPUSceneItem>   gpuSceneBuffer;
StructuredBuffer<Material>       materialBuffer;
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
	
	Material material = materialBuffer.Load(tid.x);
	
	if (material.pipelineKey != passUniform.pipelineKey)
	{
		return;
	}
	
	uint drawID;
	InterlockedAdd(drawCounterBuffer[0], 1, drawID);
	
	IDrawCommand cmd;
	cmd.sceneItemIndex = tid.x;
	// #wip: Need 'mesh view buffer' to fill vertex/index buffer views
	//cmd.positionBufferView    = 0;
	//cmd.nonPositionBufferView = 0;
	//cmd.indexBufferView       = 0;
	//cmd.drawIndexedArguments.IndexCountPerInstance = 0;
	//cmd.drawIndexedArguments.InstanceCount         = 1;
	//cmd.drawIndexedArguments.StartIndexLocation    = 0;
	//cmd.drawIndexedArguments.BaseVertexLocation    = 0;
	//cmd.drawIndexedArguments.StartInstanceLocation = 0;

	drawCommandBuffer[drawID] = cmd;
}
