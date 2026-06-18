#include "common.hlsl"
#include "raytracing_common.hlsl"
#include "bsdf.hlsl"

//#ifndef SHADER_STAGE
//    #error Definition of SHADER_STAGE must be provided
//#endif
//#define SHADER_STAGE_RAYGEN     1
//#define SHADER_STAGE_CLOSESTHIT 2
//#define SHADER_STAGE_MISS       3

// #wip: Stackless
#define STACKLESS 1

// ---------------------------------------------------------

#define OBJECT_ID_NONE            0xffff

// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
// TMin should be kept small to prevent missing geometry at close contact areas.
// #todo: See 'Ray Tracing Gems' series.
#define RAYGEN_T_MIN              0.001
#define RAYGEN_T_MAX              10000.0
#define MAX_PATH_LEN              3
#define SURFACE_NORMAL_OFFSET     0.001
// Precision of world position from scene depth is bad; need more bias.
#define GBUFFER_NORMAL_OFFSET     0.05

#define RANDOM_SEQUENCE_WIDTH     64
#define RANDOM_SEQUENCE_HEIGHT    64
#define RANDOM_SEQUENCE_LENGTH    (RANDOM_SEQUENCE_WIDTH * RANDOM_SEQUENCE_HEIGHT)

#define SKYBOX_BOOST              1.0
#define MAX_HISTORY               64

// EIndirectDiffuseMode
#define MODE_DISABLED             0
#define MODE_RANDOM_SAMPLED       1
#define MODE_STBN_SAMPLED         2

struct PassUniform
{
	float4      randFloats0[RANDOM_SEQUENCE_LENGTH / 4];
	float4      randFloats1[RANDOM_SEQUENCE_LENGTH / 4];
	uint        renderTargetWidth;
	uint        renderTargetHeight;
	uint        frameCounter;
	uint        mode;
};

struct ClosestHitPushConstants { uint objectID; };

// ---------------------------------------------------------
// Global root signature

ConstantBuffer<SceneUniform>            sceneUniform      : register(b0, space0);
ConstantBuffer<PassUniform>             passUniform       : register(b1, space0);
ByteAddressBuffer                       gIndexBuffer      : register(t0, space0);
ByteAddressBuffer                       gVertexBuffer     : register(t1, space0);
StructuredBuffer<GPUSceneItem>          gpuSceneBuffer    : register(t2, space0);
StructuredBuffer<Material>              materials         : register(t3, space0);
RaytracingAccelerationStructure         rtScene           : register(t4, space0);
TextureCube                             skybox            : register(t5, space0);
Texture3D                               stbnTexture       : register(t6, space0);
Texture2D<GBUFFER0_DATATYPE>            gbuffer0          : register(t7, space0);
Texture2D<GBUFFER1_DATATYPE>            gbuffer1          : register(t8, space0);
Texture2D                               sceneDepthTexture : register(t9, space0);
RWTexture2D<float4>                     raytracingTexture : register(u0, space0);

// Material resource binding
#define TEMP_MAX_SRVS 1024
Texture2D albedoTextures[TEMP_MAX_SRVS]     : register(t0, space3); // bindless in another space

// Samplers
SamplerState albedoSampler : register(s0, space0);
SamplerState skyboxSampler : register(s1, space0);
SamplerState linearSampler : register(s2, space0);
SamplerState pointSampler  : register(s3, space0);

// ---------------------------------------------------------
// Local root signature (closest hit)

[[vk::push_constant]]
ConstantBuffer<ClosestHitPushConstants> g_closestHitCB : register(b0, space2);

// ---------------------------------------------------------

// #todo: For triangle primitives only. Need to support user-defined attrib for procedural primitives.
typedef BuiltInTriangleIntersectionAttributes IntersectionAttributes;

struct [raypayload] RayPayload
{
	float3 surfaceNormal : read(caller) : write(closesthit, miss);
	float  roughness     : read(caller) : write(closesthit, miss);

	float3 albedo        : read(caller) : write(closesthit, miss);
	float  hitTime       : read(caller) : write(closesthit, miss);

	float3 emission      : read(caller) : write(closesthit, miss);
	uint   objectID      : read(caller) : write(closesthit, miss);

	float  metalMask     : read(caller) : write(closesthit, miss);
	uint3  _pad0         : read()       : write();
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

float3 sampleRandomDirectionCosineWeighted(uint2 texel, uint bounce)
{
	bool bUseSTBN = passUniform.mode == MODE_STBN_SAMPLED;

	if (bUseSTBN)
	{
		int x = int(texel.x & 127);
		int y = int(texel.y & 127);
		int z = (passUniform.frameCounter + bounce) & 63;
		float3 stbn = stbnTexture.Load(int4(x, y, z, 0)).rgb;
		stbn = 2.0 * stbn - 1.0;
		return normalize(stbn);
	}
	else
	{
		float2 randoms = getRandoms(texel, bounce);
		return cosineWeightedHemisphereSample(randoms.x, randoms.y);
	}
}

// ---------------------------------------------------------
// Light sampling

float3 sampleSky(float3 dir)
{
	return SKYBOX_BOOST * skybox.SampleLevel(skyboxSampler, dir, 0.0).rgb;
}

float3 traceSun(float3 rayOrigin)
{
	RayPayload rayPayload = createRayPayload();

	RayDesc rayDesc;
	rayDesc.Origin = rayOrigin;
	rayDesc.Direction = -(sceneUniform.sunDirection.xyz);
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
		return sceneUniform.sunIlluminance;
	}

	return 0;
}

float3 traceLightSources(float3 positionWS)
{
	// Only sun for now
	return traceSun(positionWS);
}

// ---------------------------------------------------------
// Shader stages

struct TangentFrame
{
	float3 N, T, B;
	float3x3 localToWorld;
	
	float3 transformDirection(float3 localDir)
	{
		return rotateVector(localDir, localToWorld);
	}
};

TangentFrame createTangentFrame(float3 normal)
{
	float3 T, B;
	computeTangentFrame(normal, T, B);
	
	TangentFrame frame;
	frame.N = normal;
	frame.T = T;
	frame.B = B;
	frame.localToWorld = float3x3(T, B, normal);
	
	return frame;
}

float3 traceIncomingRadiance(uint2 texel, float3 rayOrigin, float3 rayDir)
{
	RayPayload currentRayPayload = createRayPayload();

	RayDesc currentRay;
	currentRay.Origin = rayOrigin;
	currentRay.Direction = rayDir;
	currentRay.TMin = RAYGEN_T_MIN;
	currentRay.TMax = RAYGEN_T_MAX;
	
	// #wip: I'm missing cosine term and division by PI in various places.
	//       Did I omitted them because they will be cancelled out after all?
	//       or did I just do it wrong?
#if STACKLESS
	float3 Li = 0;
	float3 modulation = 1; // Accumulation of (brdf * cosine_term), better name?
#else
	float3 reflectanceHistory[MAX_PATH_LEN + 1];
	float3 radianceHistory[MAX_PATH_LEN + 1];
	float pdfHistory[MAX_PATH_LEN + 1];
#endif
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

		// Hit the sky. Sample the skybox.
		if (currentRayPayload.objectID == OBJECT_ID_NONE)
		{
#if STACKLESS
			Li += modulation * sampleSky(currentRay.Direction);
			pathLen += 1;
#else
			radianceHistory[pathLen] = sampleSky(currentRay.Direction);
			reflectanceHistory[pathLen] = 1;
			pdfHistory[pathLen] = 1;
#endif
			
			pathLen += 1;
			break;
		}

		// #todo: Sometimes surfaceNormal is NaN
		TangentFrame frame = createTangentFrame(currentRayPayload.surfaceNormal);

		float3 surfacePosition = currentRayPayload.hitTime * currentRay.Direction + currentRay.Origin;

		float2 randoms = getRandoms(texel, pathLen + 1);

		float3 scatteredReflectance = currentRayPayload.albedo;
		float scatteredPdf = 1;
		
		float3 Wi = sampleRandomDirectionCosineWeighted(texel, pathLen + 1);
		Wi = frame.transformDirection(Wi);
		
		// #todo: Sometimes surfaceNormal is NaN
		if (any(isnan(frame.N)) || any(isnan(Wi)))
		{
			scatteredPdf = 0.0;
		}

		if (scatteredPdf <= 0.0)
		{
			break;
		}

#if STACKLESS
		float3 diffuseReflectance = currentRayPayload.albedo;
		float pdf = 1;
		float3 radiance = currentRayPayload.emission + traceLightSources(surfacePosition);
		
		modulation *= (diffuseReflectance) / pdf; // cosine-weighted sampling, so no cosine term
		Li += modulation * radiance;
#else
		float3 E = traceLightSources(surfacePosition);
		
		radianceHistory[pathLen] = currentRayPayload.emission + E;
		reflectanceHistory[pathLen] = scatteredReflectance;
		pdfHistory[pathLen] = scatteredPdf;
#endif

		currentRay.Origin = surfacePosition + SURFACE_NORMAL_OFFSET * frame.N;
		currentRay.Direction = Wi;
		//currentRay.TMin = RAYGEN_T_MIN;
		//currentRay.TMax = RAYGEN_T_MAX;

		pathLen += 1;
	}

#if !STACKLESS
	float3 Li = 0;
	for (uint i = 0; i < pathLen; ++i)
	{
		uint j = pathLen - i - 1;
		Li = reflectanceHistory[j] * (Li + radianceHistory[j]) / pdfHistory[j];
	}
#endif

	return Li;
}

[shader("raygeneration")]
void MainRaygen()
{
	uint2 texel = DispatchRaysIndex().xy;
	float2 screenUV = getScreenUV(texel);

	float sceneDepth = sceneDepthTexture.Load(int3(texel, 0)).r;
	float3 positionWS = getWorldPositionFromSceneDepth(screenUV, sceneDepth, sceneUniform.viewProjInvMatrix);
	float3 viewDirection = normalize(positionWS - sceneUniform.cameraPosition.xyz);
	float linearDepth = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix);

	float3 Li = 0;
	if (sceneDepth != DEVICE_Z_FAR)
	{
		GBUFFER0_DATATYPE gbuffer0Data = gbuffer0.Load(int3(texel, 0));
		GBUFFER1_DATATYPE gbuffer1Data = gbuffer1.Load(int3(texel, 0));
		GBufferData gbufferData = decodeGBuffers(gbuffer0Data, gbuffer1Data);

		TangentFrame frame = createTangentFrame(gbufferData.normalWS);
		
		float3 relaxedPositionWS = frame.N * GBUFFER_NORMAL_OFFSET + positionWS;

		float3 Wi = sampleRandomDirectionCosineWeighted(texel, 0);
		Wi = frame.transformDirection(Wi);
		
		Li = traceIncomingRadiance(texel, relaxedPositionWS, Wi);

		// #todo: It happens :(
		if (any(isnan(frame.N)) || any(isnan(Wi)))
		{
			Li = 0;
		}
	}
	
	raytracingTexture[texel] = float4(Li, 1);
}

[shader("closesthit")]
void MainClosestHit(inout RayPayload payload, in IntersectionAttributes attr)
{
	uint objectID = g_closestHitCB.objectID;
	GPUSceneItem sceneItem = gpuSceneBuffer[objectID];

	hwrt::PrimitiveHitResult hitResult = hwrt::onPrimitiveHit(
		PrimitiveIndex(), attr.barycentrics, sceneItem, gVertexBuffer, gIndexBuffer);

	Material material = materials[objectID];
	// https://asawicki.info/news_1608_direct3d_12_-_watch_out_for_non-uniform_resource_index
	Texture2D albedoTex = albedoTextures[NonUniformResourceIndex(material.albedoTextureIndex)];
	float3 albedo = albedoTex.SampleLevel(albedoSampler, hitResult.texcoord, 0.0).rgb * material.albedoMultiplier.rgb;

	// Output payload
	payload.surfaceNormal = hitResult.surfaceNormal;
	payload.roughness     = material.roughness;
	payload.albedo        = albedo;
	payload.hitTime       = RayTCurrent();
	payload.emission      = material.emission;
	payload.objectID      = objectID;
	payload.metalMask     = material.metalMask;
}

[shader("miss")]
void MainMiss(inout RayPayload payload)
{
	payload.hitTime       = -1.0;
	payload.objectID      = OBJECT_ID_NONE;
}
