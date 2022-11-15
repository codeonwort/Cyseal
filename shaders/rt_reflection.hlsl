//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-raytracing-hlsl-reference

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#include "common.hlsl"

//#ifndef SHADER_STAGE
//    #error Definition of SHADER_STAGE must be provided
//#endif
//#define SHADER_STAGE_RAYGEN     1
//#define SHADER_STAGE_CLOSESTHIT 2
//#define SHADER_STAGE_MISS       3

//*********************************************************
//#include "RaytracingHlslCompat.h"
#define OBJECT_ID_NONE            0xffff

// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
// TMin should be kept small to prevent missing geometry at close contact areas.
// #todo: See 'Ray Tracing Gems' series.
#define RAYGEN_T_MIN              0.001
#define RAYGEN_T_MAX              10000.0

struct VertexAttributes
{
	float3 normal;
	float2 texcoord;
};

struct Viewport
{
	float left;
	float top;
	float right;
	float bottom;
};

struct RayGenConstantBuffer
{
	Viewport viewport;
};
struct ClosestHitPushConstants
{
	uint objectID;
};
//*********************************************************

// Global root signature
RaytracingAccelerationStructure rtScene        : register(t0, space0);
ByteAddressBuffer               gIndexBuffer   : register(t1, space0);
ByteAddressBuffer               gVertexBuffer  : register(t2, space0);
StructuredBuffer<MeshData>      gpuSceneBuffer : register(t3, space0);
RWTexture2D<float4>             renderTarget   : register(u0, space0);
RWTexture2D<float4>             gbufferA       : register(u1, space0);
ConstantBuffer<SceneUniform>    sceneUniform   : register(b0, space0);

// Local root signature (raygen)
ConstantBuffer<RayGenConstantBuffer> g_rayGenCB : register(b0, space1);

// Local root signature (closest hit)
ConstantBuffer<ClosestHitPushConstants> g_closestHitCB : register(b0, space2);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
	float3 surfaceNormal;
	uint   objectID;
	float  hitTime;
};

bool IsInsideViewport(float2 p, Viewport viewport)
{
	return (p.x >= viewport.left && p.x <= viewport.right)
		&& (p.y >= viewport.top && p.y <= viewport.bottom);
}

void generateCameraRay(uint2 texel, out float3 origin, out float3 direction)
{
	float2 xy = float2(texel) + 0.5;
	float2 screenPos = (xy / DispatchRaysDimensions().xy) * 2.0 - 1.0;
	screenPos.y = -screenPos.y;

	float4 worldPos = mul(float4(screenPos, 0.0, 1.0), sceneUniform.viewProjInvMatrix);
	worldPos.xyz /= worldPos.w;

	origin = sceneUniform.cameraPosition.xyz;
	direction = normalize(worldPos.xyz - origin);
}

[shader("raygeneration")]
void MyRaygenShader()
{
	float3 rayOrigin, rayDir;
	generateCameraRay(DispatchRaysIndex().xy, rayOrigin, rayDir);

	// Actually no need to do RT for primary visibility.
	// We can reconstruct surface normal and worldPos from gbuffers and sceneDepth.
	// I'm just practicing DXR here.
	RayPayload primaryPayload = { float3(0, 0, 0), OBJECT_ID_NONE, -1.0 };
	{
		// Trace the ray.
		// Set the ray's extents.
		RayDesc ray;
		ray.Origin = rayOrigin;
		ray.Direction = rayDir;
		ray.TMin = RAYGEN_T_MIN;
		ray.TMax = RAYGEN_T_MAX;

		uint instanceInclusionMask = ~0; // Do not ignore anything
		uint rayContributionToHitGroupIndex = 0;
		// #todo: Need to satisfy one of following conditions.
		//        I don't understand hit groups enough yet...
		// 1) numShaderRecords for hitGroupShaderTable is 1 and this is 0
		// 2) numShaderRecords for hitGroupShaderTable is N and this is 1
		//    where N = number of geometries
		uint multiplierForGeometryContributionToHitGroupIndex = 1;
		uint missShaderIndex = 0;
		TraceRay(
			rtScene,
			RAY_FLAG_NONE,
			instanceInclusionMask,
			rayContributionToHitGroupIndex,
			multiplierForGeometryContributionToHitGroupIndex,
			missShaderIndex,
			ray,
			primaryPayload);
	}

	if (primaryPayload.objectID != OBJECT_ID_NONE)
	{
		renderTarget[DispatchRaysIndex().xy]
			= float4(0.5 + 0.5 * primaryPayload.surfaceNormal, primaryPayload.objectID);
	}
	else
	{
		renderTarget[DispatchRaysIndex().xy] = float4(0, 0, 0, OBJECT_ID_NONE);
	}

#if 0
	RayPayload secondaryPayload = { float3(0, 0, 0), OBJECT_ID_NONE, -1.0 };
	if (primaryPayload.objectID != OBJECT_ID_NONE)
	{
		RayDesc ray;
		ray.Origin = primaryPayload.hitTime * rayDir + rayOrigin;
		ray.Direction = primaryPayload.surfaceNormal;
		ray.TMin = RAYGEN_T_MIN;
		ray.TMax = RAYGEN_T_MAX;

		TraceRay(rtScene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, secondaryPayload);
	}

	if (RAYGEN_T_MIN < secondaryPayload.hitTime && secondaryPayload.hitTime < RAYGEN_T_MAX)
	{
		renderTarget[DispatchRaysIndex().xy] = float4(0, 1, 0, 0);
	}
	else
	{
		renderTarget[DispatchRaysIndex().xy] = float4(1, 0, 0, 0);
	}
#endif
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
	// #todo: I have no idea how is this automatically updated?
	// All I did is creating a bunch of shader records for hit group
	// and suddenly this value is equal to geometry index.
	uint objectID = g_closestHitCB.objectID;

	MeshData meshData = gpuSceneBuffer[objectID];
	
	// Get the base index of the triangle's first 32 bit index.
	uint triangleIndexStride = 3 * 4; // 4 = sizeof(uint32)
	uint baseIndex = PrimitiveIndex() * triangleIndexStride;
	baseIndex += meshData.indexBufferOffset;
	uint3 indices = gIndexBuffer.Load<uint3>(baseIndex);

	// position = float3 = 12 bytes
	float3 p0 = gVertexBuffer.Load<float3>(meshData.positionBufferOffset + 12 * indices.x);
	float3 p1 = gVertexBuffer.Load<float3>(meshData.positionBufferOffset + 12 * indices.y);
	float3 p2 = gVertexBuffer.Load<float3>(meshData.positionBufferOffset + 12 * indices.z);
	// normal, texcoord = float3, float2 = 20 bytes
	VertexAttributes v0 = gVertexBuffer.Load<VertexAttributes>(meshData.nonPositionBufferOffset + 20 * indices.x);
	VertexAttributes v1 = gVertexBuffer.Load<VertexAttributes>(meshData.nonPositionBufferOffset + 20 * indices.y);
	VertexAttributes v2 = gVertexBuffer.Load<VertexAttributes>(meshData.nonPositionBufferOffset + 20 * indices.z);

	float3 barycentrics = float3(
		1 - attr.barycentrics.x - attr.barycentrics.y,
		attr.barycentrics.x,
		attr.barycentrics.y);

	payload.surfaceNormal = normalize(
		barycentrics.x * v0.normal
		+ barycentrics.y * v1.normal
		+ barycentrics.z * v2.normal);

	payload.objectID = objectID;
	payload.hitTime = RayTCurrent();
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
	payload.objectID = OBJECT_ID_NONE;
	payload.hitTime = -1.0;
}

#endif // RAYTRACING_HLSL
