#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
	uint width;
	uint height;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

Texture2D<GBUFFER0_DATATYPE>  gbuffer0;
Texture2D<GBUFFER1_DATATYPE>  gbuffer1;
RWTexture2D<float4>           rwNormal;
RWTexture2D<float>            rwRoughness;

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

		rwNormal[tid] = float4(gbufferData.normalWS, 1);
		rwRoughness[tid] = gbufferData.roughness;
	}
}
