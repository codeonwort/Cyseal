#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
	uint packedTextureSize;  // low 16-bit: width, high 16-bit: height
};

[[vk::push_constant]]
ConstantBuffer<PushConstants>  pushConstants;

ConstantBuffer<SceneUniform>   sceneUniform;
ByteAddressBuffer              gIndexBuffer;
ByteAddressBuffer              gVertexBuffer;
StructuredBuffer<GPUSceneItem> gpuSceneBuffer;

Texture2D                      sceneDepthTexture;
Texture2D<uint>                visBufferTexture;
RWTexture2D<float4>            rwOutputTexture;

uint2 unpackTextureSize()
{
	uint xy = pushConstants.packedTextureSize;
	return uint2(xy & 0xffff, (xy >> 16) & 0xffff);
}

// ------------------------------------------------------------------------
// Kernel

// Vertex attributes except for position
struct NonPositionAttributes
{
	float3 normal;
	float2 texcoord;
};

// Triangle properties in object space
struct PrimData
{
	float3 p0, p1, p2; // vertex positions
	float3 n0, n1, n2; // vertex normals
};

PrimData fetchPrimitive(VisibilityBufferData visData)
{
	// A triangle has 3 indices, 4 = sizeof(uint32)
	const uint triangleIndexStride = 3 * 4;
	
	uint objectID = visData.objectID;
	uint primID = visData.primitiveID;
	
	GPUSceneItem sceneItem = gpuSceneBuffer.Load(objectID);
	
	// Byte offset of first index in gIndexBuffer
	uint firstIndexOffset = primID * triangleIndexStride + sceneItem.indexBufferOffset;
	uint3 indices = gIndexBuffer.Load<uint3>(firstIndexOffset);
	
	// position = float3 = 12 bytes
	uint posOffset = sceneItem.positionBufferOffset;
	float3 p0 = gVertexBuffer.Load<float3>(posOffset + 12 * indices.x);
	float3 p1 = gVertexBuffer.Load<float3>(posOffset + 12 * indices.y);
	float3 p2 = gVertexBuffer.Load<float3>(posOffset + 12 * indices.z);
	
	// (normal, texcoord) = (float3, float2) = total 20 bytes
	uint nonPosOffset = sceneItem.nonPositionBufferOffset;
	NonPositionAttributes v0 = gVertexBuffer.Load<NonPositionAttributes>(nonPosOffset + 20 * indices.x);
	NonPositionAttributes v1 = gVertexBuffer.Load<NonPositionAttributes>(nonPosOffset + 20 * indices.y);
	NonPositionAttributes v2 = gVertexBuffer.Load<NonPositionAttributes>(nonPosOffset + 20 * indices.z);
	
	PrimData primData;
	primData.p0 = p0;
	primData.p1 = p1;
	primData.p2 = p2;
	primData.n0 = v0.normal;
	primData.n1 = v1.normal;
	primData.n2 = v2.normal;
	return primData;
}

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
	
	PrimData primData = fetchPrimitive(visUnpacked);
	
	// #wip: Need to calculate barycentric UV
	// shoot ray from camera to posWS, intersect with triangle, ...
	rwOutputTexture[tid.xy] = float4(0.5 + 0.5 * primData.n0, 0);
}
