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
#define MAX_BOUNCE                3

struct VertexAttributes
{
	float3 normal;
	float2 texcoord;
};

struct RayGenConstantBuffer
{
	float4x4 dummyValue;
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
TextureCube                     skybox         : register(t4, space0);
RWTexture2D<float4>             renderTarget   : register(u0, space0);
RWTexture2D<float4>             gbufferA       : register(u1, space0);
ConstantBuffer<SceneUniform>    sceneUniform   : register(b0, space0);

// Local root signature (raygen)
ConstantBuffer<RayGenConstantBuffer> g_rayGenCB : register(b0, space1);

// Local root signature (closest hit)
ConstantBuffer<ClosestHitPushConstants> g_closestHitCB : register(b0, space2);

// Material binding
#define TEMP_MAX_SRVS 1024
ConstantBuffer<Material> materials[]        : register(b0, space3); // bindless in another space
Texture2D albedoTextures[TEMP_MAX_SRVS]     : register(t0, space3); // bindless in another space

SamplerState albedoSampler                  : register(s0, space0);
SamplerState skyboxSampler                  : register(s1, space0);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
	float3 surfaceNormal;
	float  roughness;

	float3 albedo;
	float  hitTime;

	uint   objectID;
};

RayPayload createRayPayload()
{
	RayPayload payload;
	payload.surfaceNormal = float3(0, 0, 0);
	payload.roughness     = 1.0;
	payload.albedo        = float3(0, 0, 0);
	payload.hitTime       = -1.0;
	payload.objectID      = OBJECT_ID_NONE;
	return payload;
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
	float3 cameraRayOrigin, cameraRayDir;
	generateCameraRay(DispatchRaysIndex().xy, cameraRayOrigin, cameraRayDir);

	// Actually no need to do RT for primary visibility.
	// We can reconstruct surface normal and worldPos from gbuffers and sceneDepth.
	// I'm just practicing DXR here.
	RayPayload primaryPayload = createRayPayload();
	{
		// Trace the ray.
		// Set the ray's extents.
		RayDesc ray;
		ray.Origin = cameraRayOrigin;
		ray.Direction = cameraRayDir;
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

	// Hit the sky. Let's sample skybox.
	if (primaryPayload.objectID == OBJECT_ID_NONE)
	{
		float3 skyLight = skybox.SampleLevel(skyboxSampler, cameraRayDir, 0.0).rgb;
		renderTarget[DispatchRaysIndex().xy] = float4(skyLight, OBJECT_ID_NONE);
		return;
	}

	RayPayload currentPayload = primaryPayload;
	float3 indirectLighting = float3(0.0, 0.0, 0.0);
	float reflectiveness = 1.0;

	float3 currentRayOrigin = cameraRayOrigin;
	float3 currentRayDir = cameraRayDir;

	for (uint i = 0; i < MAX_BOUNCE; ++i)
	{
		bool bReflective = (currentPayload.objectID != OBJECT_ID_NONE) && (currentPayload.roughness < 1.0);
		if (!bReflective)
		{
			break;
		}

		RayDesc ray;
		ray.Origin = (currentPayload.hitTime * currentRayDir + currentRayOrigin);
		ray.Origin += 0.001 * currentPayload.surfaceNormal; // Slightly push origin
		ray.Direction = reflect(currentRayDir, currentPayload.surfaceNormal);
		ray.TMin = RAYGEN_T_MIN;
		ray.TMax = RAYGEN_T_MAX;

		currentRayOrigin = ray.Origin;
		currentRayDir = ray.Direction;

		uint instanceInclusionMask = ~0; // Do not ignore anything
		uint rayContributionToHitGroupIndex = 0;
		uint multiplierForGeometryContributionToHitGroupIndex = 1;
		uint missShaderIndex = 0;
		RayPayload nextPayload = createRayPayload();

		TraceRay(
			rtScene,
			RAY_FLAG_NONE,
			instanceInclusionMask,
			rayContributionToHitGroupIndex,
			multiplierForGeometryContributionToHitGroupIndex,
			missShaderIndex,
			ray,
			nextPayload);

		if (nextPayload.objectID == OBJECT_ID_NONE)
		{
			currentPayload = nextPayload;
			break;
		}

		reflectiveness *= (1.0 - currentPayload.roughness);
		indirectLighting += nextPayload.albedo * reflectiveness;
		currentPayload = nextPayload;
	}

	if (currentPayload.objectID == OBJECT_ID_NONE)
	{
		float3 skyLight = skybox.SampleLevel(skyboxSampler, currentRayDir, 0.0).rgb;
		indirectLighting += skyLight;
	}
	renderTarget[DispatchRaysIndex().xy] = float4(indirectLighting, currentPayload.objectID);
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
	// (normal, texcoord) = (float3, float2) = total 20 bytes
	VertexAttributes v0 = gVertexBuffer.Load<VertexAttributes>(meshData.nonPositionBufferOffset + 20 * indices.x);
	VertexAttributes v1 = gVertexBuffer.Load<VertexAttributes>(meshData.nonPositionBufferOffset + 20 * indices.y);
	VertexAttributes v2 = gVertexBuffer.Load<VertexAttributes>(meshData.nonPositionBufferOffset + 20 * indices.z);

	float3 barycentrics = float3(
		1 - attr.barycentrics.x - attr.barycentrics.y,
		attr.barycentrics.x,
		attr.barycentrics.y);
	
	float2 texcoord = barycentrics.x * v0.texcoord
		+ barycentrics.y * v1.texcoord
		+ barycentrics.z * v2.texcoord;

	Material material = materials[objectID];
	// https://asawicki.info/news_1608_direct3d_12_-_watch_out_for_non-uniform_resource_index
	Texture2D albedoTex = albedoTextures[NonUniformResourceIndex(material.albedoTextureIndex)];

	float3 surfaceNormal = normalize(
		barycentrics.x * v0.normal
		+ barycentrics.y * v1.normal
		+ barycentrics.z * v2.normal);
	surfaceNormal = normalize(mul(float4(surfaceNormal, 0.0), meshData.modelMatrix).xyz);
	payload.surfaceNormal = surfaceNormal;

	payload.roughness = material.roughness;
	payload.albedo = albedoTex.SampleLevel(albedoSampler, texcoord, 0.0).rgb
		* material.albedoMultiplier.rgb;

	payload.hitTime = RayTCurrent();
	payload.objectID = objectID;
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
	payload.objectID = OBJECT_ID_NONE;
	payload.hitTime = -1.0;
}

#endif // RAYTRACING_HLSL
