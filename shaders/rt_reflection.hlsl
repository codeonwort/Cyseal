//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#include "common.hlsl"

//#ifndef SHADER_STAGE
//    #error Definition of SHADER_STAGE must be provided
//#endif
//#define SHADER_STAGE_RAYGEN     1
//#define SHADER_STAGE_CLOSESTHIT 2
//#define SHADER_STAGE_MISS       3

//*********************************************************
//#include "RaytracingHlslCompat.h"
#define MATERIAL_ID_NONE          0
#define MATERIAL_ID_DEFAULTLIT    1

// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
// TMin should be kept small to prevent missing geometry at close contact areas.
// #todo: See 'Ray Tracing Gems' series.
#define RAYGEN_T_MIN              0.001
#define RAYGEN_T_MAX              10000.0

struct Viewport
{
    float left;
    float top;
    float right;
    float bottom;
};

struct RayGenConstantBuffer
{
    Viewport viewport;
};
//*********************************************************

// Global root signature
RaytracingAccelerationStructure rtScene      : register(t0, space0);
RWTexture2D<float4>             renderTarget : register(u0, space0);
// #todo: Not visible from raygen record, but only visible hitgroup record.
RWTexture2D<float4>             gbufferA     : register(u1, space0);
ConstantBuffer<SceneUniform>    sceneUniform : register(b0, space0);

// Local root signature (raygen)
ConstantBuffer<RayGenConstantBuffer> g_rayGenCB   : register(b0, space1);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float3 surfaceNormal;
    uint   materialID;
    float  hitTime;
};

bool IsInsideViewport(float2 p, Viewport viewport)
{
    return (p.x >= viewport.left && p.x <= viewport.right)
        && (p.y >= viewport.top && p.y <= viewport.bottom);
}

void generateCameraRay(uint2 texel, out float3 origin, out float3 direction)
{
    float2 xy = float2(texel)+0.5;
    float2 screenPos = (xy / DispatchRaysDimensions().xy) * 2.0 - 1.0;
    screenPos.y = -screenPos.y;

    float4 worldPos = mul(float4(screenPos, 0.0, 1.0), sceneUniform.viewProjInvMatrix);
    worldPos.xyz /= worldPos.w;

    origin = sceneUniform.cameraPosition.xyz;
    direction = normalize(worldPos.xyz - origin);
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayOrigin, rayDir;
    generateCameraRay(DispatchRaysIndex().xy, rayOrigin, rayDir);

    // Actually no need to do RT for primary visibility.
    // We can reconstruct surface normal and worldPos from gbuffers and sceneDepth.
    // I'm just practicing DXR here.
    RayPayload primaryPayload = { float3(0, 0, 0), MATERIAL_ID_NONE, -1.0 };
    {
        // Trace the ray.
        // Set the ray's extents.
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.Direction = rayDir;
        ray.TMin = RAYGEN_T_MIN;
        ray.TMax = RAYGEN_T_MAX;

        // #todo: Only bottom-left spike ball is being hit
        TraceRay(rtScene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, primaryPayload);
    }

    if (primaryPayload.materialID != MATERIAL_ID_NONE)
    {
        renderTarget[DispatchRaysIndex().xy] = float4(0, 1, 0, 0);
        //renderTarget[DispatchRaysIndex().xy] = float4(0.5 + 0.5 * primaryPayload.surfaceNormal, 0);
    }
    else
    {
        renderTarget[DispatchRaysIndex().xy] = float4(1, 0, 0, 0);
    }

#if 0
    RayPayload secondaryPayload = { float3(0, 0, 0), MATERIAL_ID_NONE, -1.0 };
    if (primaryPayload.materialID != MATERIAL_ID_NONE)
    {
        RayDesc ray;
        ray.Origin = primaryPayload.hitTime * rayDir + rayOrigin;
        ray.Direction = primaryPayload.surfaceNormal;
        ray.TMin = RAYGEN_T_MIN;
        ray.TMax = RAYGEN_T_MAX;

        TraceRay(rtScene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, secondaryPayload);
    }

    if (RAYGEN_T_MIN < secondaryPayload.hitTime && secondaryPayload.hitTime < RAYGEN_T_MAX)
    {
        renderTarget[DispatchRaysIndex().xy] = float4(0, 1, 0, 0);
    }
    else
    {
        renderTarget[DispatchRaysIndex().xy] = float4(1, 0, 0, 0);
    }
#endif
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    //float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    //payload.surfaceNormal = float4(barycentrics, 1);

    // Temp logic for primary visibility
    // TODO: Read vertex normal from vertex buffers
    {
        float4 hitPos = float4(WorldRayOrigin() + RayTCurrent() * WorldRayDirection(), 1.0);
        hitPos = mul(hitPos, sceneUniform.viewProjMatrix);
        hitPos.xyz /= hitPos.w;
        hitPos.xy = 2.0 * hitPos.xy - 1.0;

        uint2 texel = uint2(hitPos.xy * DispatchRaysDimensions().xy);
        payload.surfaceNormal = gbufferA[texel].xyz;
    }

    payload.materialID = MATERIAL_ID_DEFAULTLIT;
    payload.hitTime = RayTCurrent();
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload.materialID = MATERIAL_ID_NONE;
    payload.hitTime = -1.0;
}

#endif // RAYTRACING_HLSL
