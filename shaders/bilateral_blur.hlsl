#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants0
{
    uint width;
};
struct PushConstants1
{
    uint height;
};

ConstantBuffer<PushConstants0> pushConstants0;
ConstantBuffer<PushConstants1> pushConstants1;
RWTexture2D<float4>            inputTexture;
RWTexture2D<float4>            outputTexture;

// ------------------------------------------------------------------------
// Compute shader

[numthreads(8, 8, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= pushConstants0.width || tid.y >= pushConstants1.height)
    {
        return;
    }

    outputTexture[tid.xy] = inputTexture[tid.xy];
}
