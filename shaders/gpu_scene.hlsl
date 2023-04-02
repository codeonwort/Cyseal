#include "common.hlsl"

#define COMMAND_TYPE_UPDATE 0
//#define COMMAND_TYPE_ADD    1
//#define COMMAND_TYPE_REMOVE 2

struct GPUSceneCommand
{
    uint commandType;
    uint sceneItemIndex;
    uint _pad0;
    uint _pad1;
    GPUSceneItem sceneItem;
};

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint numCommands;
};

ConstantBuffer<PushConstants> pushConstants       : register(b0);
ConstantBuffer<SceneUniform> sceneUniform         : register(b1);
RWStructuredBuffer<GPUSceneItem> gpuSceneBuffer   : register(u0);
StructuredBuffer<GPUSceneCommand> commandBuffer   : register(t0);

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

    GPUSceneCommand cmd = commandBuffer[commandID];
    if (cmd.commandType == COMMAND_TYPE_UPDATE)
    {
        gpuSceneBuffer[cmd.sceneItemIndex] = cmd.sceneItem;
    }
}
