#ifndef _RAYTRACING_COMMON_H
#define _RAYTRACING_COMMON_H

#include "common.hlsl"

namespace hwrt
{
	// Vertex attributes except for position
	struct NonPositionAttributes
	{
		float3 normal;
		float2 texcoord;
	};

	struct PrimitiveHitResult
	{
		float2 texcoord;
		float3 surfaceNormal;
	};

	float2 applyBarycentrics(float3 bary, float2 v0, float2 v1, float2 v2)
	{
		return bary.x * v0 + bary.y * v1 + bary.z * v2;
	}
	float3 applyBarycentrics(float3 bary, float3 v0, float3 v1, float3 v2)
	{
		return bary.x * v0 + bary.y * v1 + bary.z * v2;
	}

	PrimitiveHitResult onPrimitiveHit(
		uint primitiveIndex,
		float2 barycentrics,
		GPUSceneItem sceneItem,
		ByteAddressBuffer gVertexBuffer,
		ByteAddressBuffer gIndexBuffer)
	{
		// A triangle has 3 indices, 4 = sizeof(uint32)
		const uint triangleIndexStride = 3 * 4;

		// Byte offset of first index in gIndexBuffer
		uint firstIndexOffset = primitiveIndex * triangleIndexStride + sceneItem.indexBufferOffset;
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

		float3 bary = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

		float2 texcoord = applyBarycentrics(bary, v0.texcoord, v1.texcoord, v2.texcoord);

		float3 surfaceNormal = applyBarycentrics(bary, v0.normal, v1.normal, v2.normal);
		surfaceNormal = normalize(transformDirection(surfaceNormal, sceneItem.modelMatrix));

		PrimitiveHitResult ret;
		ret.texcoord = texcoord;
		ret.surfaceNormal = surfaceNormal;
		return ret;
	}
}

#endif // _RAYTRACING_COMMON_H
