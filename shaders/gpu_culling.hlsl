#include "common.hlsl"
#include "indirect_arguments.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
	uint numDrawCommands;
};

// #todo-cull: Specific to base pass
struct IDrawCommand
{
	uint                         sceneItemIndex; // index in gpu scene buffer
	D3D12_VERTEX_BUFFER_VIEW     positionBufferView;
	D3D12_VERTEX_BUFFER_VIEW     nonPositionBufferView;
	D3D12_INDEX_BUFFER_VIEW      indexBufferView;
	D3D12_DRAW_INDEXED_ARGUMENTS drawIndexedArguments;
};

ConstantBuffer<PushConstants> pushConstants;
ConstantBuffer<SceneUniform> sceneUniform;
StructuredBuffer<GPUSceneItem> gpuSceneBuffer;
StructuredBuffer<IDrawCommand> drawCommandBuffer;
RWStructuredBuffer<IDrawCommand> culledDrawCommandBuffer;
RWBuffer<uint> drawCounterBuffer;

// ------------------------------------------------------------------------
// Compute shader

AABB calculateWorldBounds(float3 localMin, float3 localMax, float4x4 localToWorld)
{
	float3 worldMin = FLT_MAX;
	float3 worldMax = -FLT_MAX;
	for (uint i = 0; i < 8; ++i)
	{
		float3 v;
		v.x = (i & 1) ? localMin.x : localMax.x;
		v.y = ((i >> 1) & 1) ? localMin.y : localMax.y;
		v.z = ((i >> 2) & 1) ? localMin.z : localMax.z;
		v = mul(float4(v, 1.0), localToWorld).xyz;
		worldMin = min(worldMin, v);
		worldMax = max(worldMax, v);
	}
	AABB ret;
	ret.minBounds = worldMin;
	ret.maxBounds = worldMax;
	return ret;
}

// true if AABB is on positive side of the plane.
bool hitTest_AABB_plane(AABB box, Plane3D plane)
{
	float3 boxCenter = 0.5 * (box.maxBounds + box.minBounds);
	float3 boxHalfSize = 0.5 * (box.maxBounds - box.minBounds);
	float r = dot(boxHalfSize, abs(plane.normal));
	float s = dot(boxCenter, plane.normal) - plane.distance;
	return -r <= s;
}
// true if AABB is inside of the frustum.
bool hitTest_AABB_frustum(AABB box, Frustum3D frustum)
{
	for (int i = 0; i < 6; ++i)
	{
		if (!hitTest_AABB_plane(box, frustum.planes[i]))
		{
			return false;
		}
	}
	return true;
}
// Same as hitTest_AABB_frustum(), but ignore the far plane.
bool hitTest_AABB_frustumNoFarPlane(AABB box, Frustum3D frustum)
{
	for (int i = 0; i < 5; ++i)
	{
		if (!hitTest_AABB_plane(box, frustum.planes[i]))
		{
			return false;
		}
	}
	return true;
}

[numthreads(1, 1, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	uint objectID = drawCommandBuffer[tid.x].sceneItemIndex;
	GPUSceneItem sceneItem = gpuSceneBuffer[objectID];

	AABB worldBounds = calculateWorldBounds(
		sceneItem.localMinBounds,
		sceneItem.localMaxBounds,
		sceneItem.localToWorld);
	
	bool bInFrustum = hitTest_AABB_frustum(worldBounds, sceneUniform.cameraFrustum);

	if (bInFrustum)
	{
		uint nextItemIndex;
		InterlockedAdd(drawCounterBuffer[0], 1, nextItemIndex);

		culledDrawCommandBuffer[nextItemIndex] = drawCommandBuffer[tid.x];
	}
}
