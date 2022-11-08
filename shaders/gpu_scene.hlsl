#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint numElements;
};

ConstantBuffer<PushConstants> pushConstants       : register(b0);
//ConstantBuffer<SceneUniform> sceneUniform       : register(b1);
RWStructuredBuffer<MeshData> gpuSceneBuffer       : register(u0);
RWStructuredBuffer<MeshData> culledGpuSceneBuffer : register(u1);

// ------------------------------------------------------------------------
// Compute shader

[numthreads(1, 1, 1)]
void mainCS(uint3 tid: SV_DispatchThreadID)
{
    uint objectID = tid.x;
    if (objectID >= pushConstants.numElements)
    {
        return;
    }

    // #todo-wip: Cull something
    culledGpuSceneBuffer[objectID] = gpuSceneBuffer[objectID];
}
