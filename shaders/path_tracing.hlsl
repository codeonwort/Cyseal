#include "common.hlsl"
#include "raytracing_common.hlsl"
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
#define MAX_PATH_LEN              6
#define SURFACE_NORMAL_OFFSET     0.001
#define REFRACTION_START_OFFSET   0.01

// Temp boost sky light.
#define SKYBOX_BOOST              1.0

#define RANDOM_SEQUENCE_WIDTH     64
#define RANDOM_SEQUENCE_HEIGHT    64
#define RANDOM_SEQUENCE_LENGTH    (RANDOM_SEQUENCE_WIDTH * RANDOM_SEQUENCE_HEIGHT)

// Limit history count for realtime mode.
#define MAX_REALTIME_HISTORY      64

struct PathTracingUniform
{
	float4      randFloats0[RANDOM_SEQUENCE_LENGTH / 4];
	float4      randFloats1[RANDOM_SEQUENCE_LENGTH / 4];
	float4x4    prevViewProjInvMatrix;
	float4x4    prevViewProjMatrix;
	uint        renderTargetWidth;
	uint        renderTargetHeight;
	uint        bInvalidateHistory; // If nonzero, force invalidate the whole history.
	uint        bLimitHistory;
};

struct ClosestHitPushConstants
{
	uint objectID;
};

// ---------------------------------------------------------
// Global root signature

RaytracingAccelerationStructure    rtScene               : register(t0, space0);
ByteAddressBuffer                  gIndexBuffer          : register(t1, space0);
ByteAddressBuffer                  gVertexBuffer         : register(t2, space0);
StructuredBuffer<GPUSceneItem>     gpuSceneBuffer        : register(t3, space0);
StructuredBuffer<Material>         materials             : register(t4, space0);
TextureCube                        skybox                : register(t5, space0);
Texture2D                          sceneDepthTexture     : register(t6, space0);
Texture2D                          prevSceneDepthTexture : register(t7, space0);
Texture2D                          sceneNormalTexture    : register(t8, space0);
Texture2D                          prevColorTexture      : register(t9, space0);
RWTexture2D<float4>                currentColorTexture   : register(u0, space0);
RWTexture2D<float4>                currentMoment         : register(u1, space0);
RWTexture2D<float4>                prevMoment            : register(u2, space0);
ConstantBuffer<SceneUniform>       sceneUniform          : register(b0, space0);
ConstantBuffer<PathTracingUniform> pathTracingUniform    : register(b1, space0);

// Material binding
#define TEMP_MAX_SRVS 1024
Texture2D albedoTextures[TEMP_MAX_SRVS]     : register(t0, space3); // bindless in another space

SamplerState albedoSampler : register(s0, space0);
SamplerState skyboxSampler : register(s1, space0);
SamplerState linearSampler : register(s2, space0);

// ---------------------------------------------------------
// Local root signature (closest hit)

ConstantBuffer<ClosestHitPushConstants> g_closestHitCB : register(b0, space2);

// ---------------------------------------------------------

// #todo: For triangle primitives only. Need to support user-defined attrib for procedural primitives.
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

	float  metalMask;
	uint   materialID;
	float  indexOfRefraction;
	uint   _pad0;

	float3 transmittance;
	uint   _pad1;
};

RayPayload createRayPayload()
{
	RayPayload payload;
	payload.surfaceNormal     = float3(0, 0, 0);
	payload.roughness         = 1.0;
	payload.albedo            = float3(0, 0, 0);
	payload.hitTime           = -1.0;
	payload.emission          = float3(0, 0, 0);
	payload.objectID          = OBJECT_ID_NONE;
	payload.metalMask         = 0.0f;
	payload.materialID        = MATERIAL_ID_NONE;
	payload.indexOfRefraction = IOR_AIR;
	payload.transmittance     = float3(0, 0, 0);
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

float2 getRandoms(uint2 texel, uint bounce)
{
	uint first = texel.x + pathTracingUniform.renderTargetWidth * texel.y;
	uint seq0 = (first + bounce) % RANDOM_SEQUENCE_LENGTH;
	uint seq1 = (first + bounce) % RANDOM_SEQUENCE_LENGTH;
	float rand0 = pathTracingUniform.randFloats0[seq0 / 4][seq0 % 4];
	float rand1 = pathTracingUniform.randFloats1[seq1 / 4][seq1 % 4];
	return float2(rand0, rand1);
}

struct PrevFrameInfo
{
	bool   bValid;
	float3 positionWS;
	float  linearDepth;
	float3 color;
	float2 moments;
	float  historyCount;
};

// Returns true if reprojection is valid.
PrevFrameInfo getReprojectedInfo(float3 currPositionWS)
{
	float4 positionCS = worldSpaceToClipSpace(currPositionWS, pathTracingUniform.prevViewProjMatrix);
	float2 screenUV = clipSpaceToTextureUV(positionCS);

	PrevFrameInfo info;
	if (uvOutOfBounds(screenUV))
	{
		info.bValid = false;
		return info;
	}

	float2 resolution = getScreenResolution();
	int2 targetTexel = int2(screenUV * resolution);
	float sceneDepth = prevSceneDepthTexture.Load(int3(targetTexel, 0)).r;
	positionCS = getPositionCS(screenUV, sceneDepth);

	float4 momentsAndHistory = prevMoment[targetTexel];
	
	info.bValid = true;
	info.positionWS = clipSpaceToWorldSpace(positionCS, pathTracingUniform.prevViewProjInvMatrix);
	info.linearDepth = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix); // Assume projInv is invariant
	info.color = prevColorTexture.SampleLevel(linearSampler, screenUV, 0).rgb;
	info.moments = momentsAndHistory.xy;
	info.historyCount = momentsAndHistory.w;
	return info;
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

// ---------------------------------------------------------
// Light sampling

float3 sampleSky(float3 dir)
{
	return SKYBOX_BOOST* skybox.SampleLevel(skyboxSampler, dir, 0.0).rgb;
}

float3 traceSun(float3 rayOrigin, float3 surfaceNormal)
{
	RayPayload rayPayload = createRayPayload();

	RayDesc rayDesc;
	rayDesc.Origin = rayOrigin;
	rayDesc.Direction = sceneUniform.sunDirection.xyz;
	rayDesc.TMin = RAYGEN_T_MIN;
	rayDesc.TMax = RAYGEN_T_MAX;

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
		rayDesc,
		rayPayload);

	if (rayPayload.objectID == OBJECT_ID_NONE)
	{
		float NdotW = dot(sceneUniform.sunDirection.xyz, -surfaceNormal);
		return NdotW * sceneUniform.sunIlluminance;
	}

	return 0;
}

float3 traceLightSources(float3 rayOrigin, float3 surfaceNormal)
{
	float3 E = 0;
	
	// #todo: Pick one light source
	E += traceSun(rayOrigin, surfaceNormal);
	
	return E;
}

// ---------------------------------------------------------
// Shader stages

float3 traceIncomingRadiance(uint2 targetTexel, float3 cameraRayOrigin, float3 cameraRayDir)
{
	RayPayload currentRayPayload = createRayPayload();
	float prevIoR = IOR_AIR; // #todo-refraction: Assume primary ray is always in air.

	RayDesc currentRay;
	currentRay.Origin = cameraRayOrigin;
	currentRay.Direction = cameraRayDir;
	currentRay.TMin = RAYGEN_T_MIN;
	currentRay.TMax = RAYGEN_T_MAX;

	float3 Li = 0;
	float3 modulation = 1; // Accumulation of (brdf * cosine_term), better name?
	uint pathLen = 0;

	while (pathLen < MAX_PATH_LEN)
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

		const uint materialID = currentRayPayload.materialID;

		// Hit the sky. Sample the skybox.
		if (currentRayPayload.objectID == OBJECT_ID_NONE)
		{
			Li += modulation * sampleSky(currentRay.Direction);
			pathLen += 1;
			break;
		}

		// #todo-pathtracing: Sometimes surfaceNormal is NaN
		float3 surfaceNormal = currentRayPayload.surfaceNormal;
		float3 surfaceTangent, surfaceBitangent;
		computeTangentFrame(surfaceNormal, surfaceTangent, surfaceBitangent);

		float3 surfacePosition = currentRayPayload.hitTime * currentRay.Direction + currentRay.Origin;

		float3 nextRayOffset = 0;
		MicrofacetBRDFOutput brdfOutput;
#if TRACE_MODE == TRACE_FULL_GI
		if (materialID == MATERIAL_ID_DEFAULT_LIT)
		{
			float2 randoms = getRandoms(targetTexel, pathLen);

			brdfOutput = hwrt::evaluateDefaultLit(currentRay.Direction, surfaceNormal,
				currentRayPayload.albedo, currentRayPayload.roughness,
				currentRayPayload.metalMask, randoms);

			nextRayOffset = SURFACE_NORMAL_OFFSET * surfaceNormal;
		}
		else if (materialID == MATERIAL_ID_GLASS)
		{
			brdfOutput = hwrt::evaluateGlass(currentRay.Direction, surfaceNormal,
				prevIoR, currentRayPayload.indexOfRefraction, currentRayPayload.transmittance);

			nextRayOffset = REFRACTION_START_OFFSET * brdfOutput.outRayDir;
		}
		
		// #todo-pathtracing: Sometimes surfaceNormal is NaN so brdfOutput is also NaN.
		if (microfacetBRDFOutputHasNaN(brdfOutput))
		{
			brdfOutput.pdf = 0.0;
		}

#else
		// Diffuse term only
		float2 randoms = getRandoms(targetTexel, pathLen);
		float3 scatteredDir = cosineWeightedHemisphereSample(randoms.x, randoms.y);
		scatteredDir = (surfaceTangent * scatteredDir.x) + (surfaceBitangent * scatteredDir.y) + (surfaceNormal * scatteredDir.z);

		brdfOutput.diffuseReflectance = currentRayPayload.albedo;
		brdfOutput.specularReflectance = 0.0;
		brdfOutput.outRayDir = scatteredDir;
		brdfOutput.pdf = 1;

		nextRayOffset = SURFACE_NORMAL_OFFSET * surfaceNormal;
#endif
		
		float3 E = traceLightSources(surfacePosition, surfaceNormal);

		if (brdfOutput.pdf <= 0.0)
		{
			break;
		}
		
		/*
		L = Le0 + brdf0 * dot0 * Lr0
		  = Le0 + brdf0 * dot0 * (Le1 + brdf1 * dot1 * Lr1)
		  = Le0 + brdf0 * dot0 * (Le1 + brdf1 * dot1 * (Le2 + brdf2 * dot2 * Lr2))
		  = Le0
			+ (brdf0 * dot0 * Le1)
			+ (brdf0 * dot0 * brdf1 * dot1) * Le2
			+ (brdf0 * dot0 * brdf1 * dot1 * brdf2 * dot2) * Lr2
		#todo: Is this right? I think it should be like this, but then the result looks weird:
			1. Li += currentRayPayload.emission;
			2. modulation *= ...;
			3. Li += modulation * E;
		*/
		modulation *= (brdfOutput.diffuseReflectance + brdfOutput.specularReflectance) / brdfOutput.pdf; // cosine-weighted sampling, so no cosine term
		Li += modulation * (currentRayPayload.emission + E);

		currentRay.Origin = surfacePosition + nextRayOffset;
		currentRay.Direction = brdfOutput.outRayDir;
		//currentRay.TMin = RAYGEN_T_MIN;
		//currentRay.TMax = RAYGEN_T_MAX;

		pathLen += 1;
		prevIoR = currentRayPayload.indexOfRefraction;
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

	RayPayload currentRayPayload = cameraRayPayload;
	RayDesc currentRayDesc = cameraRay;

	uint pathLen = 0;
	while (pathLen < MAX_PATH_LEN)
	{
		float3 surfaceNormal = currentRayPayload.surfaceNormal;
		float3 surfaceTangent, surfaceBitangent;
		computeTangentFrame(surfaceNormal, surfaceTangent, surfaceBitangent);

		float3 surfacePosition = currentRayPayload.hitTime * currentRayDesc.Direction + currentRayDesc.Origin;
		surfacePosition += SURFACE_NORMAL_OFFSET * surfaceNormal; // Slightly push toward N

		float2 randoms = getRandoms(targetTexel, pathLen);
		float theta = randoms.x * 2.0 * PI;
		float phi = randoms.y * PI;
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
		pathLen += 1;
		currentRayPayload = aoRayPayload;
		currentRayDesc = aoRay;
	}

	float ambientOcclusion = (pathLen == MAX_PATH_LEN) ? 0.0 : pow(0.9, pathLen);
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
	float3 sceneNormal = sceneNormalTexture.Load(int3(targetTexel, 0)).xyz;
	float3 positionWS = getWorldPositionFromSceneDepth(screenUV, sceneDepth, sceneUniform.viewProjInvMatrix);
	float linearDepth = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix);

	PrevFrameInfo prevFrame = getReprojectedInfo(positionWS);

	float3 viewDir = normalize(sceneUniform.cameraPosition.xyz - positionWS);
	float zAlignment = 1.0 - dot(viewDir, sceneNormal);
	zAlignment = pow(zAlignment, 8);

	float depthDiff = abs(prevFrame.linearDepth - linearDepth) / linearDepth;
	float depthTolerance = lerp(1e-2f, 1e-1f, zAlignment);

	bool bClose = prevFrame.bValid && depthDiff < depthTolerance; // length(positionWS - prevPositionWS) <= 0.01; // 1.0 = 1 meter
	bool bTemporalReprojection = (pathTracingUniform.bInvalidateHistory == 0) && bClose;

#if TRACE_MODE == TRACE_AMBIENT_OCCLUSION
	float ambientOcclusion = traceAmbientOcclusion(targetTexel, cameraRayOrigin, cameraRayDir);
	
	float2 moments = 0;
	float variance = 0;

	float prevAmbientOcclusion, historyCount;
	if (bTemporalReprojection)
	{
		prevAmbientOcclusion = prevFrame.color.x;
		historyCount = prevFrame.historyCount;
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
		prevLi = prevFrame.color;
		prevMoments = prevFrame.moments;
		historyCount = prevFrame.historyCount;
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

	if (pathTracingUniform.bLimitHistory != 0)
	{
		historyCount = min(historyCount, MAX_REALTIME_HISTORY);
	}

	currentMoment[targetTexel] = float4(moments.x, moments.y, variance, historyCount);
}

[shader("closesthit")]
void MainClosestHit(inout RayPayload payload, in IntersectionAttributes attr)
{
	uint objectID = g_closestHitCB.objectID;
	GPUSceneItem sceneItem = gpuSceneBuffer[objectID];

	hwrt::PrimitiveHitResult hitResult = hwrt::onPrimitiveHit(
		PrimitiveIndex(), attr.barycentrics, sceneItem, gVertexBuffer, gIndexBuffer);

	Material material = materials[objectID];
	Texture2D albedoTex = albedoTextures[NonUniformResourceIndex(material.albedoTextureIndex)];
	float3 albedo = albedoTex.SampleLevel(albedoSampler, hitResult.texcoord, 0.0).rgb * material.albedoMultiplier.rgb;
	
	// Output payload
	payload.surfaceNormal     = hitResult.surfaceNormal;
	payload.roughness         = material.roughness;
	payload.albedo            = albedo;
	payload.hitTime           = RayTCurrent();
	payload.emission          = material.emission;
	payload.objectID          = objectID;
	payload.metalMask         = material.metalMask;
	payload.materialID        = material.materialID;
	payload.indexOfRefraction = material.indexOfRefraction;
	payload.transmittance     = material.transmittance;
}

[shader("miss")]
void MainMiss(inout RayPayload payload)
{
	payload.hitTime       = -1.0;
	payload.objectID      = OBJECT_ID_NONE;
}
