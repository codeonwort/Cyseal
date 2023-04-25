#ifndef _COMMON_H
#define _COMMON_H

#define PI         3.14159265
#define TWO_PI     6.28318530718
#define HALF_PI    1.57079632679489661923

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

struct SceneUniform
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4x4 viewProjMatrix;

    float4x4 viewInvMatrix;
    float4x4 projInvMatrix;
    float4x4 viewProjInvMatrix;

    Frustum3D cameraFrustum;

    float4 cameraPosition; // (x, y, z, ?)
    float4 sunDirection;   // (x, y, z, ?)
    float4 sunIlluminance; // (r, g, b, ?)
};

void computeTangentFrame(float3 N, out float3 T, out float3 B)
{
    float3 v = abs(N.z) < 0.99 ? float3(0, 0, 1) : float3(1, 0, 0);
    T = normalize(cross(v, N));
    B = normalize(cross(N, T));
}

#endif // _COMMON_H
