#include "common.hlsl"
#include "bsdf.hlsl"

//#ifndef SHADER_STAGE
//    #error Definition of SHADER_STAGE must be provided
//#endif
//#define SHADER_STAGE_RAYGEN     1
//#define SHADER_STAGE_CLOSESTHIT 2
//#define SHADER_STAGE_MISS       3

// ---------------------------------------------------------
#define OBJECT_ID_NONE            0xffff

#define TRACE_AMBIENT_OCCLUSION   0
#define TRACE_DIFFUSE_GI          1
#define TRACE_FULL_GI             2
#define TRACE_MODE                TRACE_FULL_GI
//#define TRACE_MODE                TRACE_AMBIENT_OCCLUSION

// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
// TMin should be kept small to prevent missing geometry at close contact areas.
// #todo: See 'Ray Tracing Gems' series.
#define RAYGEN_T_MIN              0.001
#define RAYGEN_T_MAX              10000.0
#define MAX_BOUNCE                8
#define SURFACE_NORMAL_OFFSET     0.001

// Temp boost sky light.
#define SKYBOX_BOOST              1.0

#define RANDOM_SEQUENCE_WIDTH     64
#define RANDOM_SEQUENCE_HEIGHT    64
#define RANDOM_SEQUENCE_LENGTH    (RANDOM_SEQUENCE_WIDTH * RANDOM_SEQUENCE_HEIGHT)

struct PathTracingUniform
{
	float4 randFloats0[RANDOM_SEQUENCE_LENGTH / 4];
	float4 randFloats1[RANDOM_SEQUENCE_LENGTH / 4];
	float4x4 prevViewInvMatrix;
	float4x4 prevProjInvMatrix;
	float4x4 prevViewProjMatrix;
	uint renderTargetWidth;
	uint renderTargetHeight;
	uint bInvalidateHistory; // If nonzero, force invalidate the whole history.
	uint _pad0;
};

struct VertexAttributes
{
	float3 normal;
	float2 texcoord;
};

struct ClosestHitPushConstants
{
	uint objectID;
};

// Global root signature
RaytracingAccelerationStructure    rtScene               : register(t0, space0);
ByteAddressBuffer                  gIndexBuffer          : register(t1, space0);
ByteAddressBuffer                  gVertexBuffer         : register(t2, space0);
StructuredBuffer<GPUSceneItem>     gpuSceneBuffer        : register(t3, space0);
TextureCube                        skybox                : register(t4, space0);
Texture2D                          sceneDepthTexture     : register(t5, space0);
RWTexture2D<float4>                currentColorTexture   : register(u0, space0);
RWTexture2D<float4>                prevColorTexture      : register(u1, space0);
RWTexture2D<float>                 prevSceneDepthTexture : register(u2, space0);
RWTexture2D<float4>                currentMoment         : register(u3, space0);
RWTexture2D<float4>                prevMoment            : register(u4, space0);
ConstantBuffer<SceneUniform>       sceneUniform          : register(b0, space0);
ConstantBuffer<PathTracingUniform> pathTracingUniform    : register(b1, space0);
// Material binding
#define TEMP_MAX_SRVS 1024
ConstantBuffer<Material> materials[]        : register(b0, space3); // bindless in another space
Texture2D albedoTextures[TEMP_MAX_SRVS]     : register(t0, space3); // bindless in another space

// Local root signature (closest hit)
ConstantBuffer<ClosestHitPushConstants> g_closestHitCB : register(b0, space2);

SamplerState albedoSampler                  : register(s0, space0);
SamplerState skyboxSampler                  : register(s1, space0);

typedef BuiltInTriangleIntersectionAttributes IntersectionAttributes;
// Should match with path_tracing_pass.cpp
struct RayPayload
{
	float3 surfaceNormal;
	float  roughness;

	float3 albedo;
	float  hitTime;

	float3 emission;
	uint   objectID;
};

RayPayload createRayPayload()
{
	RayPayload payload;
	payload.surfaceNormal = float3(0, 0, 0);
	payload.roughness     = 1.0;
	payload.albedo        = float3(0, 0, 0);
	payload.hitTime       = -1.0;
	payload.emission      = float3(0, 0, 0);
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

float2 getScreenResolution()
{
	return float2(pathTracingUniform.renderTargetWidth, pathTracingUniform.renderTargetHeight);
}

float3 getWorldPositionFromSceneDepth(float2 screenUV, float sceneDepth)
{
	float z = sceneDepth * 2.0 - 1.0; // Use this if not Reverse-Z
	//float z = sceneDepth; // clipZ is [0,1] in Reverse-Z
	float4 positionCS = float4(screenUV * 2.0 - 1.0, z, 1.0);
	float4 positionVS = mul(positionCS, sceneUniform.projInvMatrix);
	positionVS /= positionVS.w; // Perspective division
	float4 positionWS = mul(positionVS, sceneUniform.viewInvMatrix);
	return positionWS.xyz;
}

float3 getPrevWorldPosition(float3 currPositionWS)
{
	float4 positionCS = mul(float4(currPositionWS, 1.0), pathTracingUniform.prevViewProjMatrix);
	float2 screenUV = 0.5 * (positionCS.xy / positionCS.w) + float2(0.5, 0.5);
	if (any(screenUV < float2(0, 0)) || any(screenUV >= float2(1, 1)))
	{
		return float3(FLT_MAX, FLT_MAX, FLT_MAX);
	}
	float2 resolution = getScreenResolution();
	float sceneDepth = prevSceneDepthTexture[int2(screenUV * resolution)].r;

	float z = sceneDepth * 2.0 - 1.0; // Use this if not Reverse-Z
	//float z = sceneDepth; // clipZ is [0,1] in Reverse-Z
	positionCS = float4(screenUV * 2.0 - 1.0, z, 1.0); // [-1,1]
	float4 positionVS = mul(positionCS, pathTracingUniform.prevProjInvMatrix);
	positionVS /= positionVS.w; // Perspective division
	float4 positionWS = mul(positionVS, pathTracingUniform.prevViewInvMatrix);
	return positionWS.xyz;
}

float getLuminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Return a random direction given u0, u1 in [0, 1)
float3 cosineWeightedHemisphereSample(float u0, float u1)
{
	float theta = acos(sqrt(u0));
	float phi = u1 * PI * 2.0;
	return float3(cos(phi) * cos(theta), sin(phi) * cos(theta), sin(theta));
}

float3 traceIncomingRadiance(uint2 targetTexel, float3 cameraRayOrigin, float3 cameraRayDir)
{
	uint firstSeqLinearIndex = targetTexel.x + pathTracingUniform.renderTargetWidth * targetTexel.y;

	RayPayload currentRayPayload = createRayPayload();

	RayDesc currentRay;
	currentRay.Origin = cameraRayOrigin;
	currentRay.Direction = cameraRayDir;
	currentRay.TMin = RAYGEN_T_MIN;
	currentRay.TMax = RAYGEN_T_MAX;

	float3 reflectanceHistory[MAX_BOUNCE + 1];
	float3 radianceHistory[MAX_BOUNCE + 1];
	float pdfHistory[MAX_BOUNCE + 1];
	uint numBounces = 0;

	while (numBounces < MAX_BOUNCE)
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

		// Hit the sky. Sample the skybox.
		if (currentRayPayload.objectID == OBJECT_ID_NONE)
		{
			radianceHistory[numBounces] = SKYBOX_BOOST * skybox.SampleLevel(skyboxSampler, currentRay.Direction, 0.0).rgb;
			reflectanceHistory[numBounces] = 1;
			pdfHistory[numBounces] = 1;
			break;
		}
		// Emissive shape. Exit the loop.
		else if (any(currentRayPayload.emission > 0))
		{
			radianceHistory[numBounces] = currentRayPayload.emission;
			reflectanceHistory[numBounces] = 1;
			pdfHistory[numBounces] = 1;
			break;
		}

		float3 surfaceNormal = currentRayPayload.surfaceNormal;
		float3 surfaceTangent, surfaceBitangent;
		computeTangentFrame(surfaceNormal, surfaceTangent, surfaceBitangent);

		float3 surfacePosition = currentRayPayload.hitTime * currentRay.Direction + currentRay.Origin;

#if TRACE_MODE == TRACE_FULL_GI
		uint seqLinearIndex0 = (firstSeqLinearIndex + numBounces) % RANDOM_SEQUENCE_LENGTH;
		uint seqLinearIndex1 = (firstSeqLinearIndex + numBounces) % RANDOM_SEQUENCE_LENGTH;
		float rand0 = pathTracingUniform.randFloats0[seqLinearIndex0 / 4][seqLinearIndex0 % 4];
		float rand1 = pathTracingUniform.randFloats1[seqLinearIndex1 / 4][seqLinearIndex1 % 4];

		// #todo-pathtracing: Handle transmission.
		float3 scatteredReflectance, scatteredDir;
		float scatteredPdf;
		microfactBRDF(
			currentRay.Direction, surfaceNormal,
			currentRayPayload.albedo,
			currentRayPayload.roughness,
			0.0, // #todo-pathtracing: No metallic yet
			rand0, rand1,
			scatteredReflectance, scatteredDir, scatteredPdf);
		
		// #todo-pathtracing: It happens :(
		if (any(isnan(scatteredReflectance)) || any(isnan(scatteredDir)))
		{
			scatteredPdf = 0.0;
		}

#else
		// Diffuse term only
		uint seqLinearIndex = (firstSeqLinearIndex + numBounces) % RANDOM_SEQUENCE_LENGTH;
		float rand0 = pathTracingUniform.randFloats0[seqLinearIndex / 4][seqLinearIndex % 4];
		float rand1 = pathTracingUniform.randFloats1[seqLinearIndex / 4][seqLinearIndex % 4];

		float3 scatteredReflectance = currentRayPayload.albedo;
		float3 scatteredDir = cosineWeightedHemisphereSample(rand0, rand1);
		scatteredDir = (surfaceTangent * scatteredDir.x) + (surfaceBitangent * scatteredDir.y) + (surfaceNormal * scatteredDir.z);
		float scatteredPdf = 1;
#endif

		radianceHistory[numBounces] = 0;
		reflectanceHistory[numBounces] = scatteredReflectance;
		pdfHistory[numBounces] = scatteredPdf;

		if (scatteredPdf <= 0.0)
		{
			break;
		}

		currentRay.Origin = surfacePosition + SURFACE_NORMAL_OFFSET * surfaceNormal;
		currentRay.Direction = scatteredDir;
		//currentRay.TMin = RAYGEN_T_MIN;
		//currentRay.TMax = RAYGEN_T_MAX;

		numBounces += 1;
	}

	float3 Li = 0;
	if (numBounces < MAX_BOUNCE)
	{
		for (uint i = 0; i <= numBounces; ++i)
		{
			uint j = numBounces - i;
			if (pdfHistory[j] > 0.0)
			{
				Li = reflectanceHistory[j] * (Li + radianceHistory[j]) / pdfHistory[j];
			}
		}
	}

	return Li;
}

float traceAmbientOcclusion(uint2 targetTexel, float3 cameraRayOrigin, float3 cameraRayDir)
{
	RayPayload cameraRayPayload = createRayPayload();
	RayDesc cameraRay;
	{
		cameraRay.Origin = cameraRayOrigin;
		cameraRay.Direction = cameraRayDir;
		cameraRay.TMin = RAYGEN_T_MIN;
		cameraRay.TMax = RAYGEN_T_MAX;

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
			cameraRay,
			cameraRayPayload);
	}

	// Current pixel is the sky.
	if (cameraRayPayload.objectID == OBJECT_ID_NONE)
	{
		return 1.0;
	}

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
		float theta = pathTracingUniform.randFloats0[seqLinearIndex / 4][seqLinearIndex % 4];
		float phi = pathTracingUniform.randFloats1[seqLinearIndex / 4][seqLinearIndex % 4];
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
		else if (any(aoRayPayload.emission > 0))
		{
			break;
		}
		numBounces += 1;
		currentRayPayload = aoRayPayload;
		currentRayDesc = aoRay;
	}

	float ambientOcclusion = (numBounces == MAX_BOUNCE) ? 0.0 : pow(0.9, numBounces);
	return ambientOcclusion;
}

[shader("raygeneration")]
void MainRaygen()
{
	uint2 targetTexel = DispatchRaysIndex().xy;
	float2 screenUV = (float2(targetTexel) + float2(0.5, 0.5)) / getScreenResolution();

	float3 cameraRayOrigin, cameraRayDir;
	generateCameraRay(targetTexel, cameraRayOrigin, cameraRayDir);

	float sceneDepth = sceneDepthTexture.Load(int3(targetTexel, 0)).r;
	float3 positionWS = getWorldPositionFromSceneDepth(screenUV, sceneDepth);
	float3 prevPositionWS = getPrevWorldPosition(positionWS);

	bool bTemporalReprojection = (pathTracingUniform.bInvalidateHistory == 0) && length(positionWS - prevPositionWS) <= 0.01; // 1.0 = 1 meter

#if TRACE_MODE == TRACE_AMBIENT_OCCLUSION
	float ambientOcclusion = traceAmbientOcclusion(targetTexel, cameraRayOrigin, cameraRayDir);
	
	float2 moments = 0;
	float variance = 0;

	float prevAmbientOcclusion, historyCount;
	if (bTemporalReprojection)
	{
		prevAmbientOcclusion = prevColorTexture[targetTexel].x;
		historyCount = prevMoment[targetTexel].w;
	}
	else
	{
		prevAmbientOcclusion = 0.0;
		historyCount = 0;
	}

	ambientOcclusion = lerp(prevAmbientOcclusion, ambientOcclusion, 1.0 / (1.0 + historyCount));
	historyCount += 1;

	currentColorTexture[targetTexel] = float4(ambientOcclusion.xxx, 1);
#elif (TRACE_MODE == TRACE_DIFFUSE_GI || TRACE_MODE == TRACE_FULL_GI)

	float3 Li = traceIncomingRadiance(targetTexel, cameraRayOrigin, cameraRayDir);
	float3 prevLi;
	float2 prevMoments;
	float variance;
	float historyCount;
	if (bTemporalReprojection)
	{
		prevLi = prevColorTexture[targetTexel].xyz;
		prevMoments = prevMoment[targetTexel].xy;
		historyCount = prevMoment[targetTexel].w;
	}
	else
	{
		prevLi = 0;
		prevMoments = 0;
		historyCount = 0;
	}

	Li = lerp(prevLi, Li, 1.0 / (1.0 + historyCount));

	float2 moments;
	moments.x = getLuminance(Li);
	moments.y = moments.x * moments.x;
	moments = lerp(prevMoments, moments, 1.0 / (1.0 + historyCount));

	variance = max(0.0, moments.y - moments.x * moments.x);

	historyCount += 1;

	currentColorTexture[targetTexel] = float4(Li, 1);
#endif

	// Update prevColorTexture also to simplify blur pass setup.
	prevColorTexture[targetTexel] = currentColorTexture[targetTexel];

	prevSceneDepthTexture[targetTexel] = sceneDepth;
	currentMoment[targetTexel] = float4(moments.x, moments.y, variance, historyCount);
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
	payload.emission = material.emission;

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
