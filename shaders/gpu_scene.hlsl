#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint numElements;
};

ConstantBuffer<PushConstants> pushConstants : register(b0);
//ConstantBuffer<SceneUniform> sceneUniform : register(b1);
RWStructuredBuffer<MeshData> gpuSceneBuffer : register(u0);

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

    MeshData meshData;
    // #todo-wip: Fill meshData correctly
    // ...
    meshData.modelMatrix = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
    meshData.albedoMultiplier = float4(1, 1, 1, 1);
    gpuSceneBuffer[objectID] = meshData;
}
