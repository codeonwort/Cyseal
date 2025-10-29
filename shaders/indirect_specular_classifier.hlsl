#include "common.hlsl"
#include "indirect_arguments.hlsl"

// ---------------------------------------------------------
// Shader parameters

struct PushConstants
{
	uint packedTextureSize; // high 16-bit: height, low 16-bit: low
};

ConstantBuffer<PushConstants> pushConstants;
Texture2D                     sceneDepthTexture;
RWStructuredBuffer<uint>      rwTileCoordBuffer; // High 16-bit = y coord, low 16-bit = x coord. A tile is 8x8.
RWStructuredBuffer<uint>      rwTileCounterBuffer;

RWStructuredBuffer<D3D12_DISPATCH_RAYS_DESC> rwIndirectArgumentBuffer; // For prepareIndirectRaysCS
RWStructuredBuffer<D3D12_DISPATCH_ARGUMENTS> rwAmdReprojArgumentBuffer; // For ffx_denoiser_reproject_reflections_pass.hlsl

// ---------------------------------------------------------
// Shader: tileClassificationCS

groupshared uint g_validPixels;

// x in low 16-bit, y in high 16-bit.
uint2 unpackUint16x2(uint packed)
{
	return uint2(packed & 0xffff, (packed >> 16) & 0xffff);
}
uint packUint16x2(uint x, uint y)
{
	return ((y & 0xffff) << 16) | (x & 0xffff);
}

[numthreads(8, 8, 1)]
void tileClassificationCS(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID)
{
	uint2 texSize = unpackUint16x2(pushConstants.packedTextureSize);
	if (any(tid.xy >= texSize))
	{
		return;
	}
	
	if (gtid.x == 0 && gtid.y == 0)
	{
		g_validPixels = 0;
	}
	GroupMemoryBarrierWithGroupSync();
	
	float sceneDepth = sceneDepthTexture.Load(int3(tid.xy, 0)).r;

	if (sceneDepth != DEVICE_Z_FAR)
	{
		InterlockedAdd(g_validPixels, 1);
	}
	GroupMemoryBarrierWithGroupSync();
	
	if (gtid.x == 0 && gtid.y == 0 && g_validPixels > 0)
	{
		uint tileIx;
		InterlockedAdd(rwTileCounterBuffer[0], 1, tileIx);
		
		rwTileCoordBuffer[tileIx] = packUint16x2(tid.x, tid.y);
	}
}

// ---------------------------------------------------------
// Shader: prepareIndirectRaysCS

[numthreads(1, 1, 1)]
void prepareIndirectRaysCS()
{
	D3D12_DISPATCH_RAYS_DESC desc = rwIndirectArgumentBuffer[0];
	uint counter = rwTileCounterBuffer[0];
	
	DeviceMemoryBarrier();
	
	// x = tile linear index, y = linearized index for 8x8 pixels in a tile
	desc.Width = counter;
	desc.Height = 64;
	desc.Depth = 1;
	
	rwIndirectArgumentBuffer[0] = desc;
	
	D3D12_DISPATCH_ARGUMENTS amdDesc;
	amdDesc.ThreadGroupCountX = counter;
	amdDesc.ThreadGroupCountY = 1;
	amdDesc.ThreadGroupCountZ = 1;
	
	rwAmdReprojArgumentBuffer[0] = amdDesc;
}
