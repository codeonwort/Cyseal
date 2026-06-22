#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint width;
	uint height;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants>  pushConstants;

Texture2D                      momentTexture;
RWTexture2D<float>             rwVarianceTexture;

// ------------------------------------------------------------------------
// Compute shader

[numthreads(8, 8, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	if (tid.x >= pushConstants.width || tid.y >= pushConstants.height)
    {
        return;
    }
	
	float2 moments = momentTexture.Load(int3(tid.xy, 0)).xy;
	
	// In theory should not be negative, but it happens. Maybe precision issue?
	float variance = max(0, moments.y - moments.x * moments.x);
	
	rwVarianceTexture[tid.xy] = variance;
}
