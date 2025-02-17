// https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-raytracing-hlsl-reference

#include "common.hlsl"

//#ifndef SHADER_STAGE
//    #error Definition of SHADER_STAGE must be provided
//#endif
//#define SHADER_STAGE_RAYGEN     1
//#define SHADER_STAGE_CLOSESTHIT 2
//#define SHADER_STAGE_MISS       3

// ---------------------------------------------------------

#define OBJECT_ID_NONE            0xffff

// Set TMin to a non-zero small value to avoid aliasing issues due to floating point errors.
// TMin should be kept small to prevent missing geometry at close contact areas.
// #todo-shadows: Bad shadow masks on sphere surfaces :(
#define RAYGEN_T_MIN              0.05
#define RAYGEN_T_MAX              10000.0
#define SURFACE_NORMAL_OFFSET     0.05

struct VertexAttributes
{
	float3 normal;
	float2 texcoord;
};

struct ClosestHitPushConstants
{
	uint objectID;
};

// ---------------------------------------------------------
// Global root signature

ConstantBuffer<SceneUniform>            sceneUniform            : register(b0, space0);
ByteAddressBuffer                       gIndexBuffer            : register(t0, space0);
ByteAddressBuffer                       gVertexBuffer           : register(t1, space0);
StructuredBuffer<GPUSceneItem>          gpuSceneBuffer          : register(t2, space0);
RaytracingAccelerationStructure         rtScene                 : register(t3, space0);
RWTexture2D<float>                      renderTarget            : register(u0, space0);

// ---------------------------------------------------------
// Local root signature (closest hit)

ConstantBuffer<ClosestHitPushConstants> g_closestHitCB          : register(b0, space2);

// ---------------------------------------------------------

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
	float3 surfaceNormal;
	float  hitTime;
	uint   objectID;
};

RayPayload createRayPayload()
{
	RayPayload payload;
	payload.surfaceNormal = float3(0, 0, 0);
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

float traceShadowing(uint2 texel, float3 rayOrigin, float3 rayDir)
{
	RayPayload currentRayPayload = createRayPayload();

	RayDesc currentRay;
	currentRay.Origin = rayOrigin;
	currentRay.Direction = rayDir;
	currentRay.TMin = RAYGEN_T_MIN;
	currentRay.TMax = RAYGEN_T_MAX;

	bool bIsSkyPixel = false;

	// Shoot ray from camera to screen.
	{
		uint instanceInclusionMask = ~0; // Do not ignore anything
		uint rayContributionToHitGroupIndex = 0;
		uint multiplierForGeometryContributionToHitGroupIndex = 1;
		uint missShaderIndex = 0;
		TraceRay(
			rtScene,
			RAY_FLAG_NONE,
			instanceInclusionMask,
			rayContributionToHitGroupIndex,
			multiplierForGeometryContributionToHitGroupIndex,
			missShaderIndex,
			currentRay,
			currentRayPayload);

		// Hit the sky.
		if (currentRayPayload.objectID == OBJECT_ID_NONE)
		{
			bIsSkyPixel = true;
		}
	}

	if (bIsSkyPixel)
	{
		return 1.0;
	}

	// Shoot ray from surface to Sun.
	float3 surfaceNormal = currentRayPayload.surfaceNormal;
	float3 surfacePosition = currentRayPayload.hitTime * currentRay.Direction + currentRay.Origin;
	currentRay.Origin = surfacePosition + SURFACE_NORMAL_OFFSET * surfaceNormal;
	currentRay.Direction = -(sceneUniform.sunDirection.xyz);
	//currentRay.TMin = RAYGEN_T_MIN;
	//currentRay.TMax = RAYGEN_T_MAX;
	{
		uint instanceInclusionMask = ~0; // Do not ignore anything
		uint rayContributionToHitGroupIndex = 0;
		uint multiplierForGeometryContributionToHitGroupIndex = 1;
		uint missShaderIndex = 0;
		TraceRay(
			rtScene,
			RAY_FLAG_NONE,
			instanceInclusionMask,
			rayContributionToHitGroupIndex,
			multiplierForGeometryContributionToHitGroupIndex,
			missShaderIndex,
			currentRay,
			currentRayPayload);
	
		// Hit the sky.
		if (currentRayPayload.objectID == OBJECT_ID_NONE)
		{
			bIsSkyPixel = true;
		}
	}
	
	return bIsSkyPixel ? 1.0 : 0.0;
}

[shader("raygeneration")]
void MainRaygen()
{
	uint2 texel = DispatchRaysIndex().xy;

	float3 cameraPosition, cameraToScreen;
	generateCameraRay(texel, cameraPosition, cameraToScreen);
	
	float shadowMask = traceShadowing(texel, cameraPosition, cameraToScreen);

	renderTarget[texel] = shadowMask;
}

[shader("closesthit")]
void MainClosestHit(inout RayPayload payload, in MyAttributes attr)
{
	uint objectID = g_closestHitCB.objectID;
	GPUSceneItem sceneItem = gpuSceneBuffer[objectID];
	
	// #todo: Make raytracing_common.hlsl for raytracing passes
	
	uint triangleIndexStride = 3 * 4; // A triangle has 3 indices, 4 = sizeof(uint32)
	// Byte offset of first index in gIndexBuffer
	uint firstIndexOffset = PrimitiveIndex() * triangleIndexStride + sceneItem.indexBufferOffset;
	uint3 indices = gIndexBuffer.Load<uint3>(firstIndexOffset);

	// position = float3 = 12 bytes
	float3 p0 = gVertexBuffer.Load<float3>(sceneItem.positionBufferOffset + 12 * indices.x);
	float3 p1 = gVertexBuffer.Load<float3>(sceneItem.positionBufferOffset + 12 * indices.y);
	float3 p2 = gVertexBuffer.Load<float3>(sceneItem.positionBufferOffset + 12 * indices.z);
	// (normal, texcoord) = (float3, float2) = total 20 bytes
	VertexAttributes v0 = gVertexBuffer.Load<VertexAttributes>(sceneItem.nonPositionBufferOffset + 20 * indices.x);
	VertexAttributes v1 = gVertexBuffer.Load<VertexAttributes>(sceneItem.nonPositionBufferOffset + 20 * indices.y);
	VertexAttributes v2 = gVertexBuffer.Load<VertexAttributes>(sceneItem.nonPositionBufferOffset + 20 * indices.z);

	float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
	
	float2 texcoord = barycentrics.x * v0.texcoord + barycentrics.y * v1.texcoord + barycentrics.z * v2.texcoord;

	float3 surfaceNormal = barycentrics.x * v0.normal + barycentrics.y * v1.normal + barycentrics.z * v2.normal;
	surfaceNormal = normalize(transformDirection(surfaceNormal, sceneItem.modelMatrix));

	// Output payload
	payload.surfaceNormal = surfaceNormal;
	payload.hitTime       = RayTCurrent();
	payload.objectID      = objectID;
}

[shader("miss")]
void MainMiss(inout RayPayload payload)
{
	payload.hitTime       = -1.0;
	payload.objectID      = OBJECT_ID_NONE;
}
