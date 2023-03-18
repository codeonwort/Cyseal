#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint numElements;
};

ConstantBuffer<PushConstants> pushConstants       : register(b0);
ConstantBuffer<SceneUniform> sceneUniform         : register(b1);
RWStructuredBuffer<MeshData> gpuSceneBuffer       : register(u0);

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

    MeshData sceneItem = gpuSceneBuffer[objectID];

    // Do nothing because I'm uploading the entire scene every frame.
}
