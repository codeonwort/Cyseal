#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
	uint packedTextureSize;  // low 16-bit: width, high 16-bit: height
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

Texture2D                     sceneDepthTexture;
Texture2D                     visBufferTexture;
RWTexture2D<float4>           rwOutputTexture;

uint2 unpackTextureSize()
{
	uint xy = pushConstants.packedTextureSize;
	return uint2(xy & 0xffff, (xy >> 16) & 0xffff);
}

// ------------------------------------------------------------------------
// Kernel

[numthreads(8, 8, 1)]
void mainCS(uint3 tid: SV_DispatchThreadID)
{
	uint2 texSize = unpackTextureSize();
	if (tid.x < texSize.x && tid.y < texSize.y)
	{
		float2 screenUV = (float2(tid.xy) + 0.5) / float2(texSize);
		rwOutputTexture[tid.xy] = float4(screenUV, 0, 0);
	}
}
