#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct BlurUniform
{
    float4 kernelAndOffset[25]; // (kernel, offsetX, offsetY, _unused)
    float cPhi;
    float nPhi;
    float pPhi;
    float _pad0;
    uint textureWidth;
    uint textureHeight;
    uint stepWidth;
    uint _pad1;
};

ConstantBuffer<SceneUniform>   sceneUniform;
ConstantBuffer<BlurUniform>    blurUniform;
RWTexture2D<float4>            inColorTexture;
RWTexture2D<float4>            inNormalTexture;
Texture2D<float>               inDepthTexture;
RWTexture2D<float4>            outputTexture;

// ------------------------------------------------------------------------
// Compute shader

float2 getResolution()
{
    return float2(blurUniform.textureWidth, blurUniform.textureHeight);
}

float3 getWorldPosition(uint2 tid)
{
    float sceneDepth = inDepthTexture.Load(int3(tid.xy, 0)).r;
    float2 uv = (float2(tid.xy) + float2(0.5, 0.5)) / getResolution();
#if REVERSE_Z
    float z = sceneDepth; // clipZ is [0,1] in Reverse-Z
#else
    float z = sceneDepth * 2.0 - 1.0; // Use this if not Reverse-Z
#endif
    float4 positionCS = float4(uv * 2.0 - 1.0, z, 1.0);
    float4 positionVS = mul(positionCS, sceneUniform.projInvMatrix);
    positionVS /= positionVS.w; // Perspective division
    float4 positionWS = mul(positionVS, sceneUniform.viewInvMatrix);
    return positionWS.xyz;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= blurUniform.textureWidth || tid.y >= blurUniform.textureHeight)
    {
        return;
    }

    float2 resolution = getResolution();
    float stepWidth = float(blurUniform.stepWidth);

    float2 uv0 = (float2(tid.xy) + float2(0.5, 0.5)) / resolution;
    float3 color0 = inColorTexture[tid.xy].xyz;
    float3 normal0 = inNormalTexture[tid.xy].xyz;
    float3 pos0 = getWorldPosition(tid.xy);

    float2 step = 1.0 / resolution;
    float3 sum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 25; ++i)
    {
        float4 params = blurUniform.kernelAndOffset[i];
        float kernel = params.x;
        float2 offset = params.yz;

        float3 diff; float distSq;

        float2 uv1 = uv0 + offset * step * stepWidth;
        uint2 neighborTexel = uint2(uv1 * resolution);
        float3 color1 = inColorTexture[neighborTexel].xyz;
        float3 normal1 = inNormalTexture[neighborTexel].xyz;
        float3 pos1 = getWorldPosition(neighborTexel);

        diff = color0 - color1;
        distSq = dot(diff, diff);
        float colorWeight = min(1.0, exp(-distSq / blurUniform.cPhi));

        diff = normal0 - normal1;
        distSq = max(0.0, dot(diff, diff) / (stepWidth * stepWidth));
        float normalWeight = min(1.0, exp(-distSq / blurUniform.nPhi));

        diff = pos0 - pos1;
        distSq = dot(diff, diff);
        float posWeight = min(1.0, exp(-distSq / blurUniform.pPhi));

        float weight = colorWeight * normalWeight * posWeight;
        sum += color1 * weight * kernel;
        weightSum += weight * kernel;
    }
    float3 finalColor = sum / weightSum;

    outputTexture[tid.xy] = float4(finalColor, 1.0);
}
