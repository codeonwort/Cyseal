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
	float4x4 localToWorld;
};

struct RayHitResult
{
	bool bHit;
	float2 barycentricUV;
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
	primData.localToWorld = sceneItem.localToWorld;
	return primData;
}

// v0, v1, v2: triangle vertex positions
// n0, n1, n2: triangle vertex normals
// o, d: ray origin and direction
RayHitResult intersectRayTriangle(
	float3 v0, float3 v1, float3 v2,
	float3 n0, float3 n1, float3 n2,
	float3 o, float3 d, float t_min = 0.0, float t_max = FLT_MAX)
{
	RayHitResult ret;
	ret.bHit = false;
	ret.barycentricUV = float2(0, 0);
	
	float3 n = normalize(cross(v1 - v0, v2 - v0));
	float t = dot((v0 - o), n) / dot(d, n);
	float3 p = o + t * d;

	if (t < t_min || t > t_max)
	{
		return ret;
	}

	float3 u = v1 - v0;
	float3 v = v2 - v0;
	float3 w = p - v0;

	float uv = dot(u, v);
	float wv = dot(w, v);
	float uu = dot(u, u);
	float vv = dot(v, v);
	float wu = dot(w, u);
	float uvuv = uv * uv;
	float uuvv = uu * vv;

	float paramU = (uv * wv - vv * wu) / (uvuv - uuvv);
	float paramV = (uv * wu - uu * wv) / (uvuv - uuvv);

	if (0.0f <= paramU && 0.0f <= paramV && paramU + paramV <= 1.0f)
	{
		// #wip: interpolate vertex attributes
		// s0, t0 : texcoord of vertex 0.
		//ret.t = t;
		//ret.p = p;
		//ret.n = normalize((1 - paramU - paramV) * n0 + paramU * n1 + paramV * n2);
		//ret.texcoord.x = (1 - paramU - paramV) * s0 + paramU * s1 + paramV * s2;
		//ret.texcoord.y = (1 - paramU - paramV) * t0 + paramU * t1 + paramV * t2;
		ret.bHit = true;
		ret.barycentricUV = float2(paramU, paramV);
	}
	
	return ret;
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
	
	float3 cameraPos = sceneUniform.cameraPosition.xyz;
	float3 cameraDir = normalize(posWS - cameraPos);
	
	float3 p0 = mul(float4(primData.p0, 1.0), primData.localToWorld).xyz;
	float3 p1 = mul(float4(primData.p1, 1.0), primData.localToWorld).xyz;
	float3 p2 = mul(float4(primData.p2, 1.0), primData.localToWorld).xyz;
	float3 n0 = mul(float4(primData.n0, 0.0), primData.localToWorld).xyz;
	float3 n1 = mul(float4(primData.n1, 0.0), primData.localToWorld).xyz;
	float3 n2 = mul(float4(primData.n2, 0.0), primData.localToWorld).xyz;
	
	RayHitResult hitResult = intersectRayTriangle(p0, p1, p2, n0, n1, n2, cameraPos, cameraDir);
	if (hitResult.bHit)
	{
		rwOutputTexture[tid.xy] = float4(hitResult.barycentricUV, 0, 0);
	}
	else
	{
		// #wip: Actually should not happen
		rwOutputTexture[tid.xy] = float4(1, 0, 0, 0);
	}
}
