// https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-raytracing-hlsl-reference

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

// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
// TMin should be kept small to prevent missing geometry at close contact areas.
// #todo: See 'Ray Tracing Gems' series.
#define RAYGEN_T_MIN              0.001
#define RAYGEN_T_MAX              10000.0
#define MAX_PATH_LEN              5
#define SURFACE_NORMAL_OFFSET     0.001
// Precision of world position from scene depth is bad; need more bias.
#define GBUFFER_NORMAL_OFFSET     0.05
#define REFRACTION_START_OFFSET   0.1

// Sky pass will write sky pixels.
#define WRITE_SKY_PIXEL           0
// Temp boost sky light.
#define SKYBOX_BOOST              1.0

#define RANDOM_SEQUENCE_WIDTH     64
#define RANDOM_SEQUENCE_HEIGHT    64
#define RANDOM_SEQUENCE_LENGTH    (RANDOM_SEQUENCE_WIDTH * RANDOM_SEQUENCE_HEIGHT)

// Limit history count for glossy reflection mode.
#define MAX_GLOSSY_HISTORY        64

// EIndirectSpecularMode
#define TRACE_DISABLED            0
#define TRACE_FORCE_MIRROR        1
#define TRACE_BRDF                2

struct IndirectSpecularUniform
{
	float4      randFloats0[RANDOM_SEQUENCE_LENGTH / 4];
	float4      randFloats1[RANDOM_SEQUENCE_LENGTH / 4];
	float4x4    prevViewProjInvMatrix;
	float4x4    prevViewProjMatrix;
	uint        renderTargetWidth;
	uint        renderTargetHeight;
	uint        bInvalidateHistory; // If nonzero, force invalidate the whole history.
	uint        bLimitHistory;
	uint        traceMode;
};

struct ClosestHitPushConstants
{
	uint objectID;
};

// ---------------------------------------------------------
// Global root signature

ConstantBuffer<SceneUniform>            sceneUniform            : register(b0, space0);
ConstantBuffer<IndirectSpecularUniform> indirectSpecularUniform : register(b1, space0);
ByteAddressBuffer                       gIndexBuffer            : register(t0, space0);
ByteAddressBuffer                       gVertexBuffer           : register(t1, space0);
StructuredBuffer<GPUSceneItem>          gpuSceneBuffer          : register(t2, space0);
StructuredBuffer<Material>              materials               : register(t3, space0);
RaytracingAccelerationStructure         rtScene                 : register(t4, space0);
TextureCube                             skybox                  : register(t5, space0);
Texture2D<GBUFFER0_DATATYPE>            gbuffer0                : register(t6, space0);
Texture2D<GBUFFER1_DATATYPE>            gbuffer1                : register(t7, space0);
Texture2D                               sceneDepthTexture       : register(t8, space0);
Texture2D                               prevSceneDepthTexture   : register(t9, space0);
Texture2D                               prevColorTexture        : register(t10, space0);
RWTexture2D<float4>                     renderTarget            : register(u0, space0);
RWTexture2D<float4>                     currentColorTexture     : register(u1, space0);

// Material resource binding
#define TEMP_MAX_SRVS 1024
Texture2D albedoTextures[TEMP_MAX_SRVS]     : register(t0, space3); // bindless in another space

// Samplers
SamplerState albedoSampler : register(s0, space0);
SamplerState skyboxSampler : register(s1, space0);
SamplerState linearSampler : register(s2, space0);

// ---------------------------------------------------------
// Local root signature (closest hit)

ConstantBuffer<ClosestHitPushConstants> g_closestHitCB : register(b0, space2);

// ---------------------------------------------------------

// #todo: For triangle primitives only. Need to support user-defined attrib for procedural primitives.
typedef BuiltInTriangleIntersectionAttributes IntersectionAttributes;

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

float2 getScreenResolution()
{
	return float2(indirectSpecularUniform.renderTargetWidth, indirectSpecularUniform.renderTargetHeight);
}

float2 getScreenUV(uint2 texel)
{
	return (float2(texel) + float2(0.5, 0.5)) / getScreenResolution();
}

float2 getRandoms(uint2 texel, uint bounce)
{
	uint first = texel.x + indirectSpecularUniform.renderTargetWidth * texel.y;
	uint seq0 = (first + bounce) % RANDOM_SEQUENCE_LENGTH;
	uint seq1 = (first + bounce) % RANDOM_SEQUENCE_LENGTH;
	float rand0 = indirectSpecularUniform.randFloats0[seq0 / 4][seq0 % 4];
	float rand1 = indirectSpecularUniform.randFloats1[seq1 / 4][seq1 % 4];
	return float2(rand0, rand1);
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
		return sceneUniform.sunIlluminance;
	}

	return 0;
}

// ---------------------------------------------------------
// Shader stages

MicrofacetBRDFOutput evaluateDefaultLit(float3 inRayDir, float3 surfaceNormal,
	float3 albedo, float roughness, float metalMask, float2 randoms)
{
	MicrofacetBRDFInput brdfInput;
	brdfInput.inRayDir      = inRayDir;
	brdfInput.surfaceNormal = surfaceNormal;
	brdfInput.baseColor     = albedo;
	brdfInput.roughness     = roughness;
	brdfInput.metallic      = metalMask;
	brdfInput.rand0         = randoms.x;
	brdfInput.rand1         = randoms.y;

	return microfacetBRDF(brdfInput);
}

MicrofacetBRDFOutput evaluateMirror(float3 inRayDir, float3 surfaceNormal)
{
	MicrofacetBRDFOutput brdfOutput;
	brdfOutput.diffuseReflectance = 0.0;
	brdfOutput.specularReflectance = 1.0;
	brdfOutput.outRayDir = reflect(inRayDir, surfaceNormal);
	brdfOutput.pdf = 1.0;
	return brdfOutput;
}

MicrofacetBRDFOutput evaluateTransparent(float3 inRayDir, float3 surfaceNormal, float prevIoR, float IoR, float3 transmittance)
{
	float3 V = inRayDir;
	float3 N = dot(surfaceNormal, V) <= 0 ? surfaceNormal : -surfaceNormal;
	float ior = prevIoR / IoR;

	MicrofacetBRDFOutput brdfOutput;
	brdfOutput.diffuseReflectance = 1.0 - transmittance;
	brdfOutput.specularReflectance = transmittance;
	brdfOutput.outRayDir = getRefractedDirection(V, N, ior);
	brdfOutput.pdf = 1.0;
	return brdfOutput;
}

float3 traceIncomingRadiance(uint2 texel, float3 rayOrigin, float3 rayDir)
{
	RayPayload currentRayPayload = createRayPayload();
	float prevIoR = IOR_AIR; // #todo-refraction: Assume primary ray is always in air.

	RayDesc currentRay;
	currentRay.Origin = rayOrigin;
	currentRay.Direction = rayDir;
	currentRay.TMin = RAYGEN_T_MIN;
	currentRay.TMax = RAYGEN_T_MAX;

	float3 reflectanceHistory[MAX_PATH_LEN + 1];
	float3 radianceHistory[MAX_PATH_LEN + 1];
	float pdfHistory[MAX_PATH_LEN + 1];
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
			radianceHistory[pathLen] = sampleSky(currentRay.Direction);
			reflectanceHistory[pathLen] = 1;
			pdfHistory[pathLen] = 1;
			pathLen += 1;
			break;
		}

		// #todo: Sometimes surfaceNormal is NaN
		float3 surfaceNormal = currentRayPayload.surfaceNormal;
		float3 surfaceTangent, surfaceBitangent;
		computeTangentFrame(surfaceNormal, surfaceTangent, surfaceBitangent);

		float3 surfacePosition = currentRayPayload.hitTime * currentRay.Direction + currentRay.Origin;

		float2 randoms = getRandoms(texel, pathLen);

		float3 nextRayOffset = 0;
		MicrofacetBRDFOutput brdfOutput;
		if (materialID == MATERIAL_ID_DEFAULT_LIT)
		{
			if (indirectSpecularUniform.traceMode == TRACE_BRDF)
			{
				brdfOutput = evaluateDefaultLit(currentRay.Direction, surfaceNormal,
					currentRayPayload.albedo, currentRayPayload.roughness,
					currentRayPayload.metalMask, randoms);
			}
			else if (indirectSpecularUniform.traceMode == TRACE_FORCE_MIRROR)
			{
				brdfOutput = evaluateMirror(currentRay.Direction, surfaceNormal);
			}
			nextRayOffset = SURFACE_NORMAL_OFFSET * surfaceNormal;
		}
		else if (materialID == MATERIAL_ID_TRANSPARENT)
		{
			brdfOutput = evaluateTransparent(currentRay.Direction, surfaceNormal,
				prevIoR, currentRayPayload.indexOfRefraction, currentRayPayload.transmittance);
			nextRayOffset = REFRACTION_START_OFFSET * brdfOutput.outRayDir;
		}
		
		// #todo: Sometimes surfaceNormal is NaN so brdfOutput is also NaN.
		if (microfacetBRDFOutputHasNaN(brdfOutput))
		{
			brdfOutput.pdf = 0.0;
		}

		float3 E = 0;
		E += traceSun(surfacePosition);

		radianceHistory[pathLen] = currentRayPayload.emission + E;
		reflectanceHistory[pathLen] = brdfOutput.diffuseReflectance + brdfOutput.specularReflectance;
		pdfHistory[pathLen] = brdfOutput.pdf;

		if (brdfOutput.pdf <= 0.0)
		{
			break;
		}

		// Construct next ray.
		currentRay.Origin = surfacePosition + nextRayOffset;
		currentRay.Direction = brdfOutput.outRayDir;
		//currentRay.TMin = RAYGEN_T_MIN;
		//currentRay.TMax = RAYGEN_T_MAX;
		prevIoR = currentRayPayload.indexOfRefraction;
		pathLen += 1;
	}

	float3 Li = 0;
	for (uint i = 0; i < pathLen; ++i)
	{
		uint j = pathLen - i - 1;
		Li = reflectanceHistory[j] * (Li + radianceHistory[j]) / pdfHistory[j];
	}

	return Li;
}

struct PrevFrameInfo
{
	bool bValid;
	float3 positionWS;
	float linearDepth;
	float3 color;
	float historyCount;
};

PrevFrameInfo getReprojectedInfo(float3 currPositionWS)
{
	float4 positionCS = worldSpaceToClipSpace(currPositionWS, indirectSpecularUniform.prevViewProjMatrix);
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

	// #todo-specular: Super ghosting. Reject by color diff also.
	float4 colorAndHistory = prevColorTexture.SampleLevel(linearSampler, screenUV, 0);

	info.bValid = true;
	info.positionWS = clipSpaceToWorldSpace(positionCS, indirectSpecularUniform.prevViewProjInvMatrix);
	info.linearDepth = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix); // Assume projInv is invariant
	info.color = colorAndHistory.rgb;
	info.historyCount = colorAndHistory.a; // #todo-specular: history is bilinear sampled...
	return info;
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

	if (sceneDepth == DEVICE_Z_FAR)
	{
#if WRITE_SKY_PIXEL
		float3 Wo = sampleSky(viewDirection, 0.0);
#else
		float3 Wo = 0;
#endif
		currentColorTexture[texel] = float4(Wo, 1.0);
		renderTarget[texel] = float4(Wo, 1.0);
		return;
	}

	GBUFFER0_DATATYPE gbuffer0Data = gbuffer0.Load(int3(texel, 0));
	GBUFFER1_DATATYPE gbuffer1Data = gbuffer1.Load(int3(texel, 0));
	GBufferData gbufferData = decodeGBuffers(gbuffer0Data, gbuffer1Data);

	float3 albedo = gbufferData.albedo;
	float3 normalWS = normalize(gbufferData.normalWS);
	float roughness = gbufferData.roughness;
	float metalMask = gbufferData.metalMask;

	// Temporal reprojection
	PrevFrameInfo prevFrame = getReprojectedInfo(positionWS);
	bool bTemporalReprojection = false;
	{
		float zAlignment = pow(1.0 - dot(-viewDirection, normalWS), 8);
		float depthDiff = abs(prevFrame.linearDepth - linearDepth) / linearDepth;
		float depthTolerance = lerp(1e-2f, 1e-1f, zAlignment);
		bool bClose = prevFrame.bValid && depthDiff < depthTolerance;

		bTemporalReprojection = (indirectSpecularUniform.bInvalidateHistory == 0) && bClose;
	}

	float2 randoms = getRandoms(texel, 0);

	// Consider only specular part for first indirect bounce, but consider both for further bounces.
	// Therefore it's L(D|S)SE path.
	float3 Wo = 0;
	MicrofacetBRDFOutput brdfOutput;
	if (gbufferData.materialID == MATERIAL_ID_DEFAULT_LIT)
	{
		if (indirectSpecularUniform.traceMode == TRACE_BRDF)
		{
			brdfOutput = evaluateDefaultLit(viewDirection, normalWS, albedo, roughness, metalMask, randoms);

			// #todo: Sometimes surfaceNormal is NaN so brdfOutput is also NaN.
			if (microfacetBRDFOutputHasNaN(brdfOutput)) brdfOutput.pdf = 0.0;
		}
		else if (indirectSpecularUniform.traceMode == TRACE_FORCE_MIRROR)
		{
			brdfOutput = evaluateMirror(viewDirection, normalWS);
		}
		float3 relaxedPositionWS = normalWS * GBUFFER_NORMAL_OFFSET + positionWS;
		float3 Li = traceIncomingRadiance(texel, relaxedPositionWS, brdfOutput.outRayDir);
		
		Wo = (brdfOutput.specularReflectance / brdfOutput.pdf) * Li;
	}
	else if (gbufferData.materialID == MATERIAL_ID_TRANSPARENT)
	{
		float ior = gbufferData.indexOfRefraction;
		float3 transmittance = 1; // #todo-refraction: Read transmittance from gbuffer
		brdfOutput = evaluateTransparent(viewDirection, normalWS, IOR_AIR, ior, transmittance);

		float3 relaxedPositionWS = positionWS + (brdfOutput.outRayDir * REFRACTION_START_OFFSET);
		float3 Li = traceIncomingRadiance(texel, relaxedPositionWS, brdfOutput.outRayDir);

		Wo = (brdfOutput.specularReflectance / brdfOutput.pdf) * Li;
	}

	//prevColor was already acquired by getPrevFrame()
	float historyCount = 0;
	float3 prevWo = 0;
	if (bTemporalReprojection)
	{
		historyCount = prevFrame.historyCount;
		prevWo = prevFrame.color;
	}

	if (brdfOutput.pdf == 0.0)
	{
		Wo = prevWo;
	}
	else
	{
		Wo = lerp(prevWo, Wo, 1.0 / (1.0 + historyCount));
		historyCount += 1;
	}

	if (indirectSpecularUniform.bLimitHistory != 0)
	{
		historyCount = min(historyCount, MAX_GLOSSY_HISTORY);
	}

	// #todo-specular: Should store history in moment texture
	currentColorTexture[texel] = float4(Wo, historyCount);
	renderTarget[texel] = float4(Wo, 1.0);
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
