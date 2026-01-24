#include "common.hlsl"

#define COMMAND_TYPE_EVICT  0
#define COMMAND_TYPE_ALLOC  1
#define COMMAND_TYPE_UPDATE 2

struct GPUSceneAllocCommand
{
	uint         sceneItemIndex;
	uint         _pad0;
	uint         _pad1;
	uint         _pad2;
	GPUSceneItem sceneItem;
};
struct GPUSceneEvictCommand
{
	uint         sceneItemIndex;
};
struct GPUSceneUpdateCommand
{
	uint         sceneItemIndex;
	uint         _pad0;
	uint         _pad1;
	uint         _pad2;
	float4x4     localToWorld;
	float4x4     prevLocalToWorld;
};

#if !defined(COMMAND_TYPE)
	#error COMMAND_TYPE was not defined
#elif COMMAND_TYPE == COMMAND_TYPE_EVICT
	#define COMMAND_STRUCT GPUSceneEvictCommand
#elif COMMAND_TYPE == COMMAND_TYPE_ALLOC
	#define COMMAND_STRUCT GPUSceneAllocCommand
#elif COMMAND_TYPE == COMMAND_TYPE_UPDATE
	#define COMMAND_STRUCT GPUSceneUpdateCommand
#endif

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint numCommands;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants>    pushConstants;

RWStructuredBuffer<GPUSceneItem> gpuSceneBuffer;
StructuredBuffer<COMMAND_STRUCT> commandBuffer;

// ------------------------------------------------------------------------
// Compute shader

[numthreads(1, 1, 1)]
void mainCS(uint3 tid: SV_DispatchThreadID)
{
    uint commandID = tid.x;
    if (commandID >= pushConstants.numCommands)
    {
        return;
    }

#if COMMAND_TYPE == COMMAND_TYPE_EVICT
	GPUSceneEvictCommand cmd = commandBuffer.Load(commandID);
	GPUSceneItem item = gpuSceneBuffer[cmd.sceneItemIndex];
	item.flags = item.flags & (~GPU_SCENE_ITEM_FLAG_BIT_IS_VALID);
	gpuSceneBuffer[cmd.sceneItemIndex] = item;
	
#elif COMMAND_TYPE == COMMAND_TYPE_ALLOC
	GPUSceneAllocCommand cmd = commandBuffer.Load(commandID);
    gpuSceneBuffer[cmd.sceneItemIndex] = cmd.sceneItem;
	
#elif COMMAND_TYPE == COMMAND_TYPE_UPDATE
	GPUSceneUpdateCommand cmd = commandBuffer.Load(commandID);
	GPUSceneItem item = gpuSceneBuffer[cmd.sceneItemIndex];
	item.localToWorld = cmd.localToWorld;
	item.prevLocalToWorld = cmd.prevLocalToWorld;
    gpuSceneBuffer[cmd.sceneItemIndex] = item;
#endif
}
