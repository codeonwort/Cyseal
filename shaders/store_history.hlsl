#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint width;
	uint height;
};

ConstantBuffer<PushConstants> pushConstants;
Texture2D<GBUFFER0_DATATYPE>  gbuffer0;
Texture2D<GBUFFER1_DATATYPE>  gbuffer1;
RWTexture2D<float4>           rwPrevNormal;
RWTexture2D<float>            rwPrevRoughness;

// ------------------------------------------------------------------------
// Compute shader

[numthreads(8, 8, 1)]
void mainCS(uint2 tid : SV_DispatchThreadID)
{
	uint width = pushConstants.width;
	uint height = pushConstants.height;

    if (tid.x < width && tid.y < height)
    {
        GBUFFER0_DATATYPE gbuffer0Data = gbuffer0.Load(int3(tid, 0));
        GBUFFER1_DATATYPE gbuffer1Data = gbuffer1.Load(int3(tid, 0));
        GBufferData gbufferData = decodeGBuffers(gbuffer0Data, gbuffer1Data);

        rwPrevNormal[tid] = float4(gbufferData.normalWS, 1);
		rwPrevRoughness[tid] = gbufferData.roughness;
    }
}
