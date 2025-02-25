#ifndef _COMMON_H
#define _COMMON_H

// ---------------------------------------------------------
// Constants

#define PI         3.14159265
#define TWO_PI     6.28318530718
#define HALF_PI    1.57079632679489661923
#define FLT_MAX    (3.402823466e+38F)

// ---------------------------------------------------------
// Defines

// #todo: Toggle Reverse-Z
#define REVERSE_Z  1

#if REVERSE_Z
    #define DEVICE_Z_FAR 0.0
#else
    #define DEVICE_Z_FAR 1.0
#endif

// ---------------------------------------------------------
// Math

struct AABB
{
    float3 minBounds;
    float3 maxBounds;
};

struct Plane3D
{
    float3 normal; // Surface normal
    float distance; // Length of perp vector from O to the plane
};

struct Frustum3D
{
    // 0: top, 1: bottom, 2: left, 3: right, 4: near, 5: far
    Plane3D planes[6];
};

void computeTangentFrame(float3 N, out float3 T, out float3 B)
{
    float3 v = abs(N.z) < 0.99 ? float3(0, 0, 1) : float3(1, 0, 0);
    T = normalize(cross(v, N));
    B = normalize(cross(N, T));
}

// ---------------------------------------------------------
// Space Transform

bool uvOutOfBounds(float2 uv)
{
    return (any(uv < float2(0, 0)) || any(uv >= float2(1, 1)));
}

float getNdcZ(float sceneDepth)
{
    return sceneDepth; // clipZ is always [0,1] in DirectX
}

// positionCS = (x, y, z, 1)
float2 clipSpaceToTextureUV(float4 positionCS)
{
    return float2(0.5, -0.5) * positionCS.xy + float2(0.5, 0.5);
}

// screenUV : starts at top-left
// positionCS : +x is right, +y is up on screen, z is [0,1], w is 1
float4 getPositionCS(float2 screenUV, float deviceZ)
{
    return float4(2.0 * screenUV.x - 1.0, 1.0 - 2.0 * screenUV.y, getNdcZ(deviceZ), 1.0);
}

// positionCS = (x, y, z, 1), viewProjInv = inverse of view-projection matrix
float3 clipSpaceToWorldSpace(float4 positionCS, float4x4 viewProjInv)
{
    float4 positionWS = mul(positionCS, viewProjInv);
    positionWS /= positionWS.w;
    return positionWS.xyz;
}

// positionCS = (x, y, z, 1)
float4 worldSpaceToClipSpace(float3 positionWS, float4x4 viewProj)
{
    float4 positionCS = mul(float4(positionWS, 1.0), viewProj);
    positionCS /= positionCS.w;
    return positionCS;
}

float3 getWorldPositionFromSceneDepth(float2 screenUV, float sceneDepth, float4x4 viewProjInv)
{
    float4 positionCS = getPositionCS(screenUV, getNdcZ(sceneDepth));
    float4 positionWS = mul(positionCS, viewProjInv);
    return positionWS.xyz / positionWS.w;
}

float3 transformDirection(float3 dir, float4x4 transform)
{
    return mul(float4(dir, 0.0), transform).xyz;
}

float getLinearDepth(float2 screenUV, float sceneDepth, float4x4 projInv)
{
    float4 positionCS = getPositionCS(screenUV, getNdcZ(sceneDepth));
    float4 projected = mul(positionCS, projInv);
    return abs(projected.z / projected.w);
}

// ---------------------------------------------------------
// GPUScene

struct GPUSceneItem
{
    float4x4 modelMatrix; // local to world
    float3   localMinBounds;
    uint     positionBufferOffset;
    float3   localMaxBounds;
    uint     nonPositionBufferOffset;
    uint     indexBufferOffset;
    float3   _pad0;
};

struct Material
{
    float3 albedoMultiplier;
    float  roughness;
    uint   albedoTextureIndex;
    float3 emission;
};

struct SceneUniform
{
    float4x4  viewMatrix;
    float4x4  projMatrix;
    float4x4  viewProjMatrix;

    float4x4  viewInvMatrix;
    float4x4  projInvMatrix;
    float4x4  viewProjInvMatrix;

    float4    screenResolution;
    Frustum3D cameraFrustum;
    float4    cameraPosition; // (x, y, z, ?)
    float4    sunDirection;   // (x, y, z, ?)
    float4    sunIlluminance; // (r, g, b, ?)
};

// ---------------------------------------------------------
// GBuffer

struct GBufferData
{
    float3 albedo;
    float  roughness;
    float3 normalWS;
    float  metallic;
};

GBufferData decodeGBuffers(float4 gbuffer0, float4 gbuffer1)
{
    GBufferData data;
    data.albedo    = gbuffer0.xyz;
    data.roughness = gbuffer0.w;
    data.normalWS  = gbuffer1.xyz;
    data.metallic  = 0.0; // #todo: metallic
    return data;
}
void encodeGBuffers(in GBufferData data, out float4 gbuffer0, out float4 gbuffer1)
{
    gbuffer0.xyz = data.albedo;
    gbuffer0.w   = data.roughness;
    gbuffer1.xyz = data.normalWS;
    gbuffer1.w   = data.metallic;
}

#endif // _COMMON_H
