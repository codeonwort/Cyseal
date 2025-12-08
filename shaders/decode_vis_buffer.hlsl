#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
	uint packedTextureSize;  // low 16-bit: width, high 16-bit: height
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

ConstantBuffer<SceneUniform>  sceneUniform;
ByteAddressBuffer             gIndexBuffer;
ByteAddressBuffer             gVertexBuffer;

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
	if (tid.x >= texSize.x || tid.y >= texSize.y)
	{
		return;
	}
	
	float deviceZ = sceneDepthTexture.Load(int3(tid.xy, 0)).r;
	
	if (deviceZ == DEVICE_Z_FAR)
	{
		rwOutputTexture[tid.xy] = float4(0, 0, 0, 0);
		return;
	}
	
	float2 screenUV = (float2(tid.xy) + 0.5) / float2(texSize);
	float3 posWS = getWorldPositionFromSceneDepth(screenUV, deviceZ, sceneUniform.viewProjInvMatrix);
	
	uint visPacked = visBufferTexture.Load(int3(tid.xy, 0)).r;
	VisibilityBufferData visUnpacked = decodeVisibilityBuffer(visPacked);
	uint primID = visUnpacked.primitiveID;
	
	// #wip: Fetch triangle data via primID.
	
	rwOutputTexture[tid.xy] = float4(screenUV, 0, 0);
}
