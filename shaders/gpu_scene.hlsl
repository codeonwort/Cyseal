#include "common.hlsl"

#define FLT_MAX (3.402823466e+38F)

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint numElements;
};

ConstantBuffer<PushConstants> pushConstants       : register(b0);
ConstantBuffer<SceneUniform> sceneUniform         : register(b1);
RWStructuredBuffer<MeshData> gpuSceneBuffer       : register(u0);
RWStructuredBuffer<MeshData> culledGpuSceneBuffer : register(u1);
// #todo-wip-cull: Cull something
//RWBuffer<uint> numVisibleItems                    : register(u2);

// ------------------------------------------------------------------------
// Compute shader

AABB calculateWorldBounds(float3 localMin, float3 localMax, float4x4 localToWorld)
{
    float3 vs[8];
    for (uint i = 0; i < 8; ++i)
    {
        vs[i].x = (i & 1) ? localMin.x : localMax.x;
        vs[i].y = ((i >> 1) & 1) ? localMin.y : localMax.y;
        vs[i].z = ((i >> 2) & 1) ? localMin.z : localMax.z;
    }
    float3 worldMin = FLT_MAX;
    float3 worldMax = -FLT_MAX;
    for (uint i = 0; i < 8; ++i)
    {
        vs[i] = mul(localToWorld, float4(vs[i], 1.0)).xyz;
        worldMin = min(worldMin, vs[i]);
        worldMax = max(worldMax, vs[i]);
    }
    AABB ret;
    ret.minBounds = worldMin;
    ret.maxBounds = worldMax;
    return ret;
}

bool hitTest_AABB_plane(AABB box, Plane3D plane)
{
    float3 boxCenter = 0.5 * (box.maxBounds + box.minBounds);
    float3 boxHalfSize = 0.5 * (box.maxBounds - box.minBounds);
    float r = dot(boxHalfSize, abs(plane.normal));
    float s = dot(boxCenter, plane.normal) - plane.distance;
    return -r <= s;
}
bool hitTest_AABB_frustum(AABB box, Frustum3D frustum)
{
    for (int i = 0; i < 6; ++i)
    {
        if (!hitTest_AABB_plane(box, frustum.planes[i]))
        {
            return false;
        }
    }
    return true;
}
bool hitTest_AABB_frustumNoFarPlane(AABB box, Frustum3D frustum)
{
    for (int i = 0; i < 5; ++i)
    {
        if (!hitTest_AABB_plane(box, frustum.planes[i]))
        {
            return false;
        }
    }
    return true;
}

[numthreads(1, 1, 1)]
void mainCS(uint3 tid: SV_DispatchThreadID)
{
    uint objectID = tid.x;
    if (objectID >= pushConstants.numElements)
    {
        return;
    }

    MeshData sceneItem = gpuSceneBuffer[objectID];

    AABB worldBounds = calculateWorldBounds(
        sceneItem.localMinBounds,
        sceneItem.localMaxBounds,
        sceneItem.modelMatrix);

    bool bInFrustum = hitTest_AABB_frustum(worldBounds, sceneUniform.cameraFrustum);

    // #todo-wip-cull: Cull something
    //if (bInFrustum)
    //{
    //    uint nextItemIndex;
    //    InterlockedAdd(numVisibleItems[0], 1, nextItemIndex);
    //    culledGpuSceneBuffer[nextItemIndex] = sceneItem;
    //}

    culledGpuSceneBuffer[objectID] = sceneItem;
}
