#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint packedWidthHeight;
};

ConstantBuffer<PushConstants> pushConstants;
Texture2D<float4>             inSceneColor;
Texture2D<GBUFFER0_DATATYPE>  inGBuffer0;
Texture2D<GBUFFER1_DATATYPE>  inGBuffer1;
RWTexture2D<float4>           outColor;
RWTexture2D<float4>           outAlbedo;
RWTexture2D<float4>           outNormal;

// ------------------------------------------------------------------------
// Compute shader

[numthreads(8, 8, 1)]
void mainCS(uint2 tid : SV_DispatchThreadID)
{
    uint width = (pushConstants.packedWidthHeight >> 16) & 0xFFFF;
    uint height = pushConstants.packedWidthHeight & 0xFFFF;

    if (tid.x < width && tid.y < height)
    {
        GBUFFER0_DATATYPE gbuffer0Data = inGBuffer0.Load(int3(tid, 0));
        GBUFFER1_DATATYPE gbuffer1Data = inGBuffer1.Load(int3(tid, 0));
        GBufferData gbufferData = decodeGBuffers(gbuffer0Data, gbuffer1Data);

        outColor[tid] = float4(inSceneColor.Load(int3(tid, 0)).rgb, 1);
        outAlbedo[tid] = float4(gbufferData.albedo, 1);
        outNormal[tid] = float4(gbufferData.normalWS, 1);
    }
}
