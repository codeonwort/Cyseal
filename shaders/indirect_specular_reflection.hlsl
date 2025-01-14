// https://learn.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-raytracing-hlsl-reference

#include "common.hlsl"
#include "bsdf.hlsl"

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
#define MAX_BOUNCE                5
#define SURFACE_NORMAL_OFFSET     0.001
// Precision of world position from scene depth is bad; need more bias.
#define GBUFFER_NORMAL_OFFSET     0.05

// Temp boost sky light.
#define SKYBOX_BOOST              1.0

#define RANDOM_SEQUENCE_WIDTH     64
#define RANDOM_SEQUENCE_HEIGHT    64
#define RANDOM_SEQUENCE_LENGTH    (RANDOM_SEQUENCE_WIDTH * RANDOM_SEQUENCE_HEIGHT)

// EIndirectSpecularMode
#define TRACE_DISABLED            0
#define TRACE_FORCE_MIRROR        1
#define TRACE_BRDF                2

struct IndirectSpecularUniform
{
	float4      randFloats0[RANDOM_SEQUENCE_LENGTH / 4];
	float4      randFloats1[RANDOM_SEQUENCE_LENGTH / 4];
	float4x4    prevViewInvMatrix;
	float4x4    prevProjInvMatrix;
	float4x4    prevViewProjMatrix;
	uint        renderTargetWidth;
	uint        renderTargetHeight;
	uint        bInvalidateHistory; // If nonzero, force invalidate the whole history.
	uint        bLimitHistory;
	uint        traceMode;
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

//*********************************************************

// Global root signature
ConstantBuffer<SceneUniform>            sceneUniform            : register(b0, space0);
ConstantBuffer<IndirectSpecularUniform> indirectSpecularUniform : register(b1, space0);
ByteAddressBuffer                       gIndexBuffer            : register(t0, space0);
ByteAddressBuffer                       gVertexBuffer           : register(t1, space0);
StructuredBuffer<GPUSceneItem>          gpuSceneBuffer          : register(t2, space0);
StructuredBuffer<Material>              materials               : register(t3, space0);
RaytracingAccelerationStructure         rtScene                 : register(t4, space0);
TextureCube                             skybox                  : register(t5, space0);
Texture2D                               gbuffer0                : register(t6, space0);
Texture2D                               gbuffer1                : register(t7, space0);
Texture2D                               sceneDepthTexture       : register(t8, space0);
Texture2D                               prevSceneDepthTexture   : register(t9, space0);
RWTexture2D<float4>                     renderTarget            : register(u0, space0);
RWTexture2D<float4>                     currentColorTexture     : register(u1, space0);
RWTexture2D<float4>                     prevColorTexture        : register(u2, space0);

// Material resource binding
#define TEMP_MAX_SRVS 1024
Texture2D albedoTextures[TEMP_MAX_SRVS]     : register(t0, space3); // bindless in another space

// Local root signature (closest hit)
ConstantBuffer<ClosestHitPushConstants> g_closestHitCB : register(b0, space2);

SamplerState albedoSampler                  : register(s0, space0);
SamplerState skyboxSampler                  : register(s1, space0);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
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

float2 getScreenResolution()
{
	return float2(indirectSpecularUniform.renderTargetWidth, indirectSpecularUniform.renderTargetHeight);
}

float2 getScreenUV(uint2 texel)
{
	return (float2(texel) + float2(0.5, 0.5)) / getScreenResolution();
}

float getNdcZ(float sceneDepth)
{
	return sceneDepth; // clipZ is always [0,1] in DirectX
}

float4 getPositionCS(float2 screenUV, float z)
{
	return float4(2.0 * screenUV.x - 1.0, 1.0 - 2.0 * screenUV.y, z, 1.0);
}

float3 getWorldPositionFromSceneDepth(float2 screenUV, float sceneDepth)
{
	float4 positionCS = getPositionCS(screenUV, getNdcZ(sceneDepth));
	float4 positionWS = mul(positionCS, sceneUniform.viewProjInvMatrix);
	return positionWS.xyz / positionWS.w;
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

float3 traceIncomingRadiance(uint2 texel, float3 rayOrigin, float3 rayDir)
{
	RayPayload currentRayPayload = createRayPayload();

	RayDesc currentRay;
	currentRay.Origin = rayOrigin;
	currentRay.Direction = rayDir;
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
		float metallic = 0.0; // #todo: No metallic yet

		float2 randoms = getRandoms(texel, numBounces);

		// #todo: Handle transmission.
		float3 scatteredReflectance, scatteredDir; float scatteredPdf;
		if (indirectSpecularUniform.traceMode == TRACE_BRDF)
		{
			microfacetBRDF(
				currentRay.Direction, surfaceNormal,
				currentRayPayload.albedo, currentRayPayload.roughness, metallic,
				randoms.x, randoms.y,
				scatteredReflectance, scatteredDir, scatteredPdf);
		}
		else if (indirectSpecularUniform.traceMode == TRACE_FORCE_MIRROR)
		{
			scatteredReflectance = 1.0;
			scatteredDir = reflect(currentRay.Direction, surfaceNormal);
			scatteredPdf = 1.0;
		}
		
		// #todo: It happens :(
		if (any(isnan(scatteredReflectance)) || any(isnan(scatteredDir)))
		{
			scatteredPdf = 0.0;
		}

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

[shader("raygeneration")]
void MainRaygen()
{
	uint2 texel = DispatchRaysIndex().xy;
	float2 screenUV = getScreenUV(texel);

	float sceneDepth = sceneDepthTexture.Load(int3(texel, 0)).r;
	float3 positionWS = getWorldPositionFromSceneDepth(screenUV, sceneDepth);
	float3 viewDirection = normalize(positionWS - sceneUniform.cameraPosition.xyz);

	if (sceneDepth == 1.0)
	{
		float3 Wo = SKYBOX_BOOST * skybox.SampleLevel(skyboxSampler, viewDirection, 0.0).rgb;
		currentColorTexture[texel] = float4(Wo, 1.0);
		renderTarget[texel] = float4(Wo, 1.0);
		return;
	}

	float4 gbuffer0Data = gbuffer0.Load(int3(texel, 0));
	float4 gbuffer1Data = gbuffer1.Load(int3(texel, 0));
	GBufferData gbufferData = decodeGBuffers(gbuffer0Data, gbuffer1Data);

	float3 albedo = gbufferData.albedo;
	float3 normalWS = normalize(gbufferData.normalWS);
	float roughness = gbufferData.roughness;
	float metallic = 0.0; // #todo: No metallic yet

	float2 randoms = getRandoms(texel, 0);

	float3 scatteredReflectance, scatteredDir; float scatteredPdf;
	if (indirectSpecularUniform.traceMode == TRACE_BRDF)
	{
		// Consider only specular part for first indirect bounce, but consider both for further bounces.
		// Therefore it's L(D|S)SE path.
		float3 dummy;
		splitMicrofacetBRDF(viewDirection, normalWS, albedo, roughness, metallic, randoms.x, randoms.y,
			dummy, scatteredReflectance, scatteredDir, scatteredPdf);
	}
	else if (indirectSpecularUniform.traceMode == TRACE_FORCE_MIRROR)
	{
		scatteredReflectance = 1.0;
		scatteredDir = reflect(viewDirection, normalWS);
		scatteredPdf = 1.0;
	}
	
	float3 relaxedPositionWS = normalWS * GBUFFER_NORMAL_OFFSET + positionWS;
	float3 Li = traceIncomingRadiance(texel, relaxedPositionWS, scatteredDir);
	float3 Wo = (scatteredReflectance / scatteredPdf) * Li;

	float3 prevColor;
	float historyCount;

	if (indirectSpecularUniform.bInvalidateHistory == 0)
	{
		prevColor = prevColorTexture[texel].xyz;
		historyCount = prevColorTexture[texel].w;
	}
	else
	{
		prevColor = float3(0.0, 0.0, 0.0);
		historyCount = 0;
	}

	if (scatteredPdf == 0.0)
	{
		Wo = prevColor;
	}
	else
	{
		Wo = lerp(prevColor, Wo, 1.0 / (1.0 + historyCount));
		historyCount += 1;
	}

	// #wip: Should store history in moment texture
	currentColorTexture[texel] = float4(Wo, historyCount);
	renderTarget[texel] = float4(Wo, 1.0);
}

[shader("closesthit")]
void MainClosestHit(inout RayPayload payload, in MyAttributes attr)
{
	uint objectID = g_closestHitCB.objectID;
	GPUSceneItem sceneItem = gpuSceneBuffer[objectID];
	
	// #wip: Make raytracing_common.hlsl for raytracing passes
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

	float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
	
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
	surfaceNormal = normalize(mul(float4(surfaceNormal, 0.0), sceneItem.modelMatrix).xyz);
	payload.surfaceNormal = surfaceNormal;

	payload.roughness = material.roughness;
	payload.albedo = albedoTex.SampleLevel(albedoSampler, texcoord, 0.0).rgb * material.albedoMultiplier.rgb;
	payload.emission = material.emission;

	payload.hitTime = RayTCurrent();
	payload.objectID = objectID;
}

[shader("miss")]
void MainMiss(inout RayPayload payload)
{
	payload.objectID = OBJECT_ID_NONE;
	payload.hitTime = -1.0;
}
