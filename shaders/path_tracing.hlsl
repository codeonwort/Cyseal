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
//#define TRACE_MODE                TRACE_DIFFUSE_GI
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

struct PassUniform
{
	float4      randFloats0[RANDOM_SEQUENCE_LENGTH / 4];
	float4      randFloats1[RANDOM_SEQUENCE_LENGTH / 4];
	uint        renderTargetWidth;
	uint        renderTargetHeight;
};

struct ClosestHitPushConstants
{
	uint objectID;
};

// ---------------------------------------------------------
// Global root signature

ConstantBuffer<SceneUniform> sceneUniform            : register(b0, space0);
ConstantBuffer<PassUniform> passUniform              : register(b1, space0);
RaytracingAccelerationStructure    rtScene           : register(t0, space0);
ByteAddressBuffer                  gIndexBuffer      : register(t1, space0);
ByteAddressBuffer                  gVertexBuffer     : register(t2, space0);
StructuredBuffer<GPUSceneItem>     gpuSceneBuffer    : register(t3, space0);
StructuredBuffer<Material>         materials         : register(t4, space0);
TextureCube                        skybox            : register(t5, space0);
Texture2D                          sceneDepthTexture : register(t6, space0);
RWTexture2D<float4>                raytracingTexture : register(u0, space0);

// Material binding
#define TEMP_MAX_SRVS 1024
Texture2D albedoTextures[TEMP_MAX_SRVS]     : register(t0, space3); // bindless in another space

SamplerState albedoSampler : register(s0, space0);
SamplerState skyboxSampler : register(s1, space0);
SamplerState linearSampler : register(s2, space0);

// ---------------------------------------------------------
// Local root signature (closest hit)

[[vk::push_constant]]
ConstantBuffer<ClosestHitPushConstants> g_closestHitCB : register(b0, space2);

// ---------------------------------------------------------

// #todo: For triangle primitives only. Need to support user-defined attrib for procedural primitives.
typedef BuiltInTriangleIntersectionAttributes IntersectionAttributes;

// Should match with path_tracing_pass.cpp
struct [raypayload] RayPayload
{
	float3 surfaceNormal     : write(closesthit) : read(caller);
	float  roughness         : write(closesthit) : read(caller);

	float3 albedo            : write(closesthit) : read(caller);
	float  hitTime           : write(closesthit) : read(caller);

	float3 emission          : write(closesthit) : read(caller);
	uint   objectID          : write(closesthit) : read(caller);

	float  metalMask         : write(closesthit) : read(caller);
	uint   materialID        : write(closesthit) : read(caller);
	float  indexOfRefraction : write(closesthit) : read(caller);
	uint   _pad0             : write()           : read();

	float3 transmittance     : write(closesthit) : read(caller);
	uint   _pad1             : write()           : read();
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
	return float2(passUniform.renderTargetWidth, passUniform.renderTargetHeight);
}

float2 getScreenUV(uint2 texel)
{
	return (float2(texel) + float2(0.5, 0.5)) / getScreenResolution();
}

float2 getRandoms(uint2 texel, uint bounce)
{
	uint first = texel.x + passUniform.renderTargetWidth * texel.y;
	uint seq0 = (first + bounce) % RANDOM_SEQUENCE_LENGTH;
	uint seq1 = (first + bounce) % RANDOM_SEQUENCE_LENGTH;
	float rand0 = passUniform.randFloats0[seq0 / 4][seq0 % 4];
	float rand1 = passUniform.randFloats1[seq1 / 4][seq1 % 4];
	return float2(rand0, rand1);
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

		// Construct next ray.
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
	uint2 texel = DispatchRaysIndex().xy;
	float2 screenUV = getScreenUV(texel);

	float3 cameraRayOrigin, cameraRayDir;
	generateCameraRay(texel, cameraRayOrigin, cameraRayDir);

	float sceneDepth = sceneDepthTexture.Load(int3(texel, 0)).r;
	float3 positionWS = getWorldPositionFromSceneDepth(screenUV, sceneDepth, sceneUniform.viewProjInvMatrix);
	float linearDepth = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix);

#if TRACE_MODE == TRACE_AMBIENT_OCCLUSION
	float ambientOcclusion = traceAmbientOcclusion(texel, cameraRayOrigin, cameraRayDir);
	float3 Li = ambientOcclusion.xxx;
#elif (TRACE_MODE == TRACE_DIFFUSE_GI || TRACE_MODE == TRACE_FULL_GI)
	float3 Li = traceIncomingRadiance(texel, cameraRayOrigin, cameraRayDir);
#endif

	raytracingTexture[texel] = float4(Li, 1.0);
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
