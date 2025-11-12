#include "common.hlsl"

// ---------------------------------------------------------
// Shader parameters

struct PushConstants
{
	uint textureWidth;
	uint textureHeight;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

Texture2D                     inRadiance;
RWStructuredBuffer<uint>      rwTileCoordBuffer;
RWTexture2D<float4>           rwRadiance;

// ---------------------------------------------------------
// Shader

groupshared uint2 baseTexel;

[numthreads(8, 8, 1)]
void finalizeColorCS(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
	if (gtid.x == 0 && gtid.y == 0)
	{
		uint packed = rwTileCoordBuffer[gid.x];
		baseTexel = uint2(packed & 0xffff, (packed >> 16) & 0xffff);
	}
	GroupMemoryBarrierWithGroupSync();
	
	uint2 texel = baseTexel + gtid.xy;
	rwRadiance[texel] = inRadiance.Load(int3(texel, 0));
}
