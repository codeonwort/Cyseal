#include "common.hlsl"

//#ifndef SHADER_STAGE
//    #error Definition of SHADER_STAGE must be provided
//#endif
//#define SHADER_STAGE_RAYGEN     1
//#define SHADER_STAGE_CLOSESTHIT 2
//#define SHADER_STAGE_MISS       3

//*********************************************************
#define OBJECT_ID_NONE            0xffff

// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
// TMin should be kept small to prevent missing geometry at close contact areas.
// #todo: See 'Ray Tracing Gems' series.
#define RAYGEN_T_MIN              0.001
#define RAYGEN_T_MAX              10000.0
#define MAX_BOUNCE                3
#define SURFACE_NORMAL_OFFSET     0.001

#define RANDOM_SEQUENCE_WIDTH     64
#define RANDOM_SEQUENCE_HEIGHT    64
#define RANDOM_SEQUENCE_LENGTH    (RANDOM_SEQUENCE_WIDTH * RANDOM_SEQUENCE_HEIGHT)

struct PathTracingUniform
{
	float4 thetaSeq[RANDOM_SEQUENCE_LENGTH / 4];
	float4 phiSeq[RANDOM_SEQUENCE_LENGTH / 4];
	uint renderTargetWidth;
	uint renderTargetHeight;
	uint bInvalidateHistory;
	uint _pad0;
};

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
RaytracingAccelerationStructure    rtScene            : register(t0, space0);
ByteAddressBuffer                  gIndexBuffer       : register(t1, space0);
ByteAddressBuffer                  gVertexBuffer      : register(t2, space0);
StructuredBuffer<GPUSceneItem>     gpuSceneBuffer     : register(t3, space0);
TextureCube                        skybox             : register(t4, space0);
RWTexture2D<float4>                renderTarget       : register(u0, space0);
ConstantBuffer<SceneUniform>       sceneUniform       : register(b0, space0);
ConstantBuffer<PathTracingUniform> pathTracingUniform : register(b1, space0);

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

typedef BuiltInTriangleIntersectionAttributes IntersectionAttributes;
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

void computeTangentFrame(float3 N, out float3 T, out float3 B)
{
	B = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1.0, 0.0, 0.0);
	T = normalize(cross(B, N));
	B = cross(N, T);
}

[shader("raygeneration")]
void MainRaygen()
{
	uint2 targetTexel = DispatchRaysIndex().xy;

	float3 cameraRayOrigin, cameraRayDir;
	generateCameraRay(targetTexel, cameraRayOrigin, cameraRayDir);

	RayPayload cameraRayPayload = createRayPayload();
	RayDesc cameraRay;
	{
		// Trace the ray.
		// Set the ray's extents.
		cameraRay.Origin = cameraRayOrigin;
		cameraRay.Direction = cameraRayDir;
		cameraRay.TMin = RAYGEN_T_MIN;
		cameraRay.TMax = RAYGEN_T_MAX;

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
			cameraRay,
			cameraRayPayload);
	}

	// Current pixel is sky.
	if (cameraRayPayload.objectID == OBJECT_ID_NONE)
	{
		renderTarget[targetTexel] = float4(1.0, 1.0, 1.0, 0.0);
		return;
	}

	// Debug visualization
	//renderTarget[targetTexel] = float4(0.5 + 0.5 * cameraRayPayload.surfaceNormal, cameraRayPayload.objectID);
	//renderTarget[targetTexel] = float4(cameraRayPayload.albedo, cameraRayPayload.objectID);
	//return;

	uint firstSeqLinearIndex = targetTexel.x + pathTracingUniform.renderTargetWidth * targetTexel.y;

	RayPayload currentRayPayload = cameraRayPayload;
	RayDesc currentRayDesc = cameraRay;

	uint numBounces = 0;
	while (numBounces < MAX_BOUNCE)
	{
		float3 surfaceNormal = currentRayPayload.surfaceNormal;
		float3 surfaceTangent, surfaceBitangent;
		computeTangentFrame(surfaceNormal, surfaceTangent, surfaceBitangent);

		float3 surfacePosition = currentRayPayload.hitTime * currentRayDesc.Direction + currentRayDesc.Origin;
		surfacePosition += SURFACE_NORMAL_OFFSET * surfaceNormal; // Slightly push toward N

		uint seqLinearIndex = (firstSeqLinearIndex + numBounces) % RANDOM_SEQUENCE_LENGTH;
		float theta = pathTracingUniform.thetaSeq[seqLinearIndex / 4][seqLinearIndex % 4];
		float phi = pathTracingUniform.phiSeq[seqLinearIndex / 4][seqLinearIndex % 4];
		float3 aoRayDir = float3(cos(phi) * cos(theta), sin(phi) * cos(theta), sin(theta));
		aoRayDir = (surfaceTangent * aoRayDir.x) + (surfaceBitangent * aoRayDir.y) + (surfaceNormal * aoRayDir.z);

		RayPayload aoRayPayload = createRayPayload();
		RayDesc aoRay;
		aoRay.Origin = surfacePosition;
		aoRay.Direction = aoRayDir;
		aoRay.TMin = RAYGEN_T_MIN;
		aoRay.TMax = RAYGEN_T_MAX;

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
			aoRay,
			aoRayPayload);

		if (aoRayPayload.objectID == OBJECT_ID_NONE)
		{
			break;
		}
		numBounces += 1;
		currentRayPayload = aoRayPayload;
		currentRayDesc = aoRay;
	}

	float visibility = (numBounces == MAX_BOUNCE) ? 0.0 : pow(0.9, numBounces);

	float prevVisibility, historyCount;
	if (pathTracingUniform.bInvalidateHistory == 0)
	{
		prevVisibility = renderTarget[targetTexel].x;
		historyCount = renderTarget[targetTexel].w;
	}
	else
	{
		prevVisibility = 0.0;
		historyCount = 0;
	}

	visibility = lerp(prevVisibility, visibility, 1.0 / (1.0 + historyCount));
	historyCount += 1;

	renderTarget[targetTexel] = float4(visibility.xxx, historyCount);
}

[shader("closesthit")]
void MainClosestHit(inout RayPayload payload, in IntersectionAttributes attr)
{
	uint objectID = g_closestHitCB.objectID;

	GPUSceneItem sceneItem = gpuSceneBuffer[objectID];
	
	// Get the base index of the triangle's first 32 bit index.
	uint triangleIndexStride = 3 * 4; // 4 = sizeof(uint32)
	uint baseIndex = PrimitiveIndex() * triangleIndexStride;
	baseIndex += sceneItem.indexBufferOffset;
	uint3 indices = gIndexBuffer.Load<uint3>(baseIndex);

	// position = float3 = 12 bytes
	float3 p0 = gVertexBuffer.Load<float3>(sceneItem.positionBufferOffset + 12 * indices.x);
	float3 p1 = gVertexBuffer.Load<float3>(sceneItem.positionBufferOffset + 12 * indices.y);
	float3 p2 = gVertexBuffer.Load<float3>(sceneItem.positionBufferOffset + 12 * indices.z);
	// (normal, texcoord) = (float3, float2) = total 20 bytes
	VertexAttributes v0 = gVertexBuffer.Load<VertexAttributes>(sceneItem.nonPositionBufferOffset + 20 * indices.x);
	VertexAttributes v1 = gVertexBuffer.Load<VertexAttributes>(sceneItem.nonPositionBufferOffset + 20 * indices.y);
	VertexAttributes v2 = gVertexBuffer.Load<VertexAttributes>(sceneItem.nonPositionBufferOffset + 20 * indices.z);

	float3 barycentrics = float3(
		1 - attr.barycentrics.x - attr.barycentrics.y,
		attr.barycentrics.x,
		attr.barycentrics.y);
	
	float2 texcoord = barycentrics.x * v0.texcoord
		+ barycentrics.y * v1.texcoord
		+ barycentrics.z * v2.texcoord;

	Material material = materials[objectID];
	Texture2D albedoTex = albedoTextures[NonUniformResourceIndex(material.albedoTextureIndex)];

	float3 surfaceNormal = normalize(
		barycentrics.x * v0.normal
		+ barycentrics.y * v1.normal
		+ barycentrics.z * v2.normal);
	surfaceNormal = normalize(mul(float4(surfaceNormal, 0.0), sceneItem.modelMatrix).xyz);
	// Hmm if hit the back face I should flip surfaceNormal but how to know it?
	payload.surfaceNormal = surfaceNormal;

	payload.roughness = material.roughness;

	payload.albedo = albedoTex.SampleLevel(albedoSampler, texcoord, 0.0).rgb;
	payload.albedo *= material.albedoMultiplier.rgb;

	payload.hitTime = RayTCurrent();
	payload.objectID = objectID;
}

[shader("miss")]
void MainMiss(inout RayPayload payload)
{
	payload.objectID = OBJECT_ID_NONE;
	payload.hitTime = -1.0;
}
