#ifndef _COMMON_H
#define _COMMON_H

#include "material.hlsl"

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

float4 textureUVToClipSpace(float2 uv)
{
    return float4((uv - float2(0.5, 0.5)) * float2(2, -2), 0, 1);
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

// Should match with GPUSceneItem in gpu_scene.cpp
struct GPUSceneItem
{
    float4x4 localToWorld;
    float4x4 prevLocalToWorld;
    float3   localMinBounds;
    uint     positionBufferOffset;
    float3   localMaxBounds;
    uint     nonPositionBufferOffset;
    uint     indexBufferOffset;
    float3   _pad0;
};

struct SceneUniform
{
    float4x4  viewMatrix;
    float4x4  projMatrix;
    float4x4  viewProjMatrix;

    float4x4  viewInvMatrix;
    float4x4  projInvMatrix;
    float4x4  viewProjInvMatrix;
    
    float4x4  prevViewProjMatrix;
    float4x4  prevViewProjInvMatrix;

    float4    screenResolution;
    Frustum3D cameraFrustum;
    float4    cameraPosition; // (x, y, z, ?)
    float4    sunDirection;   // (x, y, z, ?)
    float4    sunIlluminance; // (r, g, b, ?)
};

// ---------------------------------------------------------
// Visibility buffer

#define VISIBILITY_BUFFER_PRIMITIVE_ID_BITS 22
#define VISIBILITY_BUFFER_PRIMITIVE_ID_MASK ((1 << VISIBILITY_BUFFER_PRIMITIVE_ID_BITS) - 1)

struct VisibilityBufferData
{
	uint objectID;
	uint primitiveID;
};

// Assumes that primitiveID < 65536.
uint encodeVisibilityBuffer(VisibilityBufferData data)
{
	// #todo-visibility: primitiveID goes too large because I don't have meshlet yet :(
	return (data.objectID << VISIBILITY_BUFFER_PRIMITIVE_ID_BITS) | (data.primitiveID & VISIBILITY_BUFFER_PRIMITIVE_ID_MASK);
}

VisibilityBufferData decodeVisibilityBuffer(uint packed)
{
	VisibilityBufferData unpacked;
	unpacked.objectID = packed >> VISIBILITY_BUFFER_PRIMITIVE_ID_BITS;
	unpacked.primitiveID = packed & VISIBILITY_BUFFER_PRIMITIVE_ID_MASK;
	return unpacked;
}

// ---------------------------------------------------------
// GBuffer

#define GBUFFER0_DATATYPE uint4
#define GBUFFER1_DATATYPE float4

struct GBufferData
{
    float3 albedo;
    float  roughness;
    float3 normalWS;
    float  metalMask;

    uint   materialID;
    float  indexOfRefraction;
};

GBufferData decodeGBuffers(GBUFFER0_DATATYPE gbuffer0, GBUFFER1_DATATYPE gbuffer1)
{
    uint3 packedAlbedo = uint3(gbuffer0.x >> 16, gbuffer0.x & 0xFFFF, gbuffer0.y >> 16);
    uint packedIoR = gbuffer0.y & 0xFFFF;

    float3 albedo = f16tof32(packedAlbedo);
    float ior = f16tof32(packedIoR);

    GBufferData data;
    data.albedo            = albedo;
    data.indexOfRefraction = ior;
    data.roughness         = asfloat(gbuffer0.z);
    data.materialID        = gbuffer0.w;
    data.normalWS          = gbuffer1.xyz;
    data.metalMask         = gbuffer1.w;
    return data;
}
void encodeGBuffers(in GBufferData data, out GBUFFER0_DATATYPE gbuffer0, out GBUFFER1_DATATYPE gbuffer1)
{
    uint3 packedAlbedo = f32tof16(data.albedo); // Each contained in low 16 bits.
    uint packedIoR = f32tof16(data.indexOfRefraction);

    gbuffer0.x   = (packedAlbedo.x << 16) | packedAlbedo.y;
    gbuffer0.y   = (packedAlbedo.z << 16) | packedIoR;
    gbuffer0.z   = asuint(data.roughness);
    gbuffer0.w   = data.materialID;
    gbuffer1.xyz = data.normalWS;
    gbuffer1.w   = data.metalMask;
}

#endif // _COMMON_H
