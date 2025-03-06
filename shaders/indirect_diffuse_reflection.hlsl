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

// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
// TMin should be kept small to prevent missing geometry at close contact areas.
// #todo: See 'Ray Tracing Gems' series.
#define RAYGEN_T_MIN              0.001
#define RAYGEN_T_MAX              10000.0
#define MAX_BOUNCE                3
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

struct IndirectDiffuseUniform
{
	float4      randFloats0[RANDOM_SEQUENCE_LENGTH / 4];
	float4      randFloats1[RANDOM_SEQUENCE_LENGTH / 4];
	float4x4    prevViewProjInvMatrix;
	float4x4    prevViewProjMatrix;
	uint        renderTargetWidth;
	uint        renderTargetHeight;
	uint        frameCounter;
	uint        mode;
};

struct VertexAttributes { float3 normal; float2 texcoord; };
struct ClosestHitPushConstants { uint objectID; };

// ---------------------------------------------------------
// Global root signature

ConstantBuffer<SceneUniform>            sceneUniform            : register(b0, space0);
ConstantBuffer<IndirectDiffuseUniform>  indirectDiffuseUniform  : register(b1, space0);
ByteAddressBuffer                       gIndexBuffer            : register(t0, space0);
ByteAddressBuffer                       gVertexBuffer           : register(t1, space0);
StructuredBuffer<GPUSceneItem>          gpuSceneBuffer          : register(t2, space0);
StructuredBuffer<Material>              materials               : register(t3, space0);
RaytracingAccelerationStructure         rtScene                 : register(t4, space0);
TextureCube                             skybox                  : register(t5, space0);
Texture3D                               stbnTexture             : register(t6, space0);
Texture2D<GBUFFER0_DATATYPE>            gbuffer0                : register(t7, space0);
Texture2D<GBUFFER1_DATATYPE>            gbuffer1                : register(t8, space0);
Texture2D                               sceneDepthTexture       : register(t9, space0);
Texture2D                               prevSceneDepthTexture   : register(t10, space0);
Texture2D                               prevColorTexture        : register(t11, space0);
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

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
	float3 surfaceNormal;
	float  roughness;

	float3 albedo;
	float  hitTime;

	float3 emission;
	uint   objectID;

	float  metalMask;
	uint3  _pad0;
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
	return float2(indirectDiffuseUniform.renderTargetWidth, indirectDiffuseUniform.renderTargetHeight);
}

float2 getScreenUV(uint2 texel)
{
	return (float2(texel) + float2(0.5, 0.5)) / getScreenResolution();
}

float2 getRandoms(uint2 texel, uint bounce)
{
	uint first = texel.x + indirectDiffuseUniform.renderTargetWidth * texel.y;
	uint seq0 = (first + bounce) % RANDOM_SEQUENCE_LENGTH;
	uint seq1 = (first + bounce) % RANDOM_SEQUENCE_LENGTH;
	float rand0 = indirectDiffuseUniform.randFloats0[seq0 / 4][seq0 % 4];
	float rand1 = indirectDiffuseUniform.randFloats1[seq1 / 4][seq1 % 4];
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
	bool bUseSTBN = indirectDiffuseUniform.mode == MODE_STBN_SAMPLED;

	if (bUseSTBN)
	{
		int x = int(texel.x & 127);
		int y = int(texel.y & 127);
		int z = (indirectDiffuseUniform.frameCounter + bounce) & 63;
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

		// #todo: Sometimes surfaceNormal is NaN
		float3 surfaceNormal = currentRayPayload.surfaceNormal;
		float3 surfaceTangent, surfaceBitangent;
		computeTangentFrame(surfaceNormal, surfaceTangent, surfaceBitangent);

		float3 surfacePosition = currentRayPayload.hitTime * currentRay.Direction + currentRay.Origin;

		float2 randoms = getRandoms(texel, numBounces + 1);

#if 0
		float3 scatteredReflectance, scatteredDir; float scatteredPdf;
		microfacetBRDF(
			currentRay.Direction, surfaceNormal,
			currentRayPayload.albedo, currentRayPayload.roughness, currentRayPayload.metalMask,
			randoms.x, randoms.y,
			scatteredReflectance, scatteredDir, scatteredPdf);
#else
		float3 scatteredReflectance = currentRayPayload.albedo;
		float3 scatteredDir = sampleRandomDirectionCosineWeighted(texel, numBounces + 1);
		scatteredDir = (surfaceTangent * scatteredDir.x) + (surfaceBitangent * scatteredDir.y) + (surfaceNormal * scatteredDir.z);
		float scatteredPdf = 1;
#endif
		
		// #todo: Sometimes surfaceNormal is NaN
		if (any(isnan(surfaceNormal)) || any(isnan(scatteredDir)))
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
	float4 positionCS = worldSpaceToClipSpace(currPositionWS, indirectDiffuseUniform.prevViewProjMatrix);
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

	float4 colorAndHistory = prevColorTexture.SampleLevel(linearSampler, screenUV, 0);

	info.bValid = true;
	info.positionWS = clipSpaceToWorldSpace(positionCS, indirectDiffuseUniform.prevViewProjInvMatrix);
	info.linearDepth = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix); // Assume projInv is invariant
	info.color = colorAndHistory.rgb;
	info.historyCount = colorAndHistory.a; // #todo-diffuse: history is bilinear sampled...
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
		float3 Wo = 0;
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

	float NdotV = dot(-viewDirection, normalWS);

	// Temporal reprojection
	PrevFrameInfo prevFrame = getReprojectedInfo(positionWS);
	bool bTemporalReprojection = false;
	{
		float zAlignment = pow(1.0 - NdotV, 8);
		float depthDiff = abs(prevFrame.linearDepth - linearDepth) / linearDepth;
		float depthTolerance = lerp(1e-2f, 1e-1f, zAlignment);
		bool bClose = prevFrame.bValid && depthDiff < depthTolerance;

		bTemporalReprojection = bClose;
	}

	float3 surfaceTangent, surfaceBitangent;
	computeTangentFrame(normalWS, surfaceTangent, surfaceBitangent);

	float3 scatteredReflectance = albedo;
	float3 scatteredDir = sampleRandomDirectionCosineWeighted(texel, 0);
	scatteredDir = (surfaceTangent * scatteredDir.x) + (surfaceBitangent * scatteredDir.y) + (normalWS * scatteredDir.z);
	float scatteredPdf = 1;
	
	float3 relaxedPositionWS = normalWS * GBUFFER_NORMAL_OFFSET + positionWS;
	float3 Li = traceIncomingRadiance(texel, relaxedPositionWS, scatteredDir);
	float3 Wo = (scatteredReflectance / scatteredPdf) * Li;

	// #todo: It happens :(
	if (any(isnan(normalWS)) || any(isnan(scatteredDir)))
	{
		scatteredPdf = 0.0;
		Wo = 0;
	}

	//prevColor was already acquired by getPrevFrame()
	float historyCount = bTemporalReprojection ? prevFrame.historyCount : 0;

	if (scatteredPdf == 0.0)
	{
		Wo = prevFrame.color;
	}
	else
	{
		Wo = lerp(prevFrame.color, Wo, 1.0 / (1.0 + historyCount));
		historyCount += 1;
	}

	historyCount = min(historyCount, MAX_HISTORY);

	// #todo-diffuse: Should store history in moment texture
	currentColorTexture[texel] = float4(Wo, historyCount);
	renderTarget[texel] = float4(Wo, 1.0);
}

[shader("closesthit")]
void MainClosestHit(inout RayPayload payload, in MyAttributes attr)
{
	uint objectID = g_closestHitCB.objectID;
	GPUSceneItem sceneItem = gpuSceneBuffer[objectID];
	
	// #todo-diffuse: Make raytracing_common.hlsl for raytracing passes
	
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

	Material material = materials[objectID];
	// https://asawicki.info/news_1608_direct3d_12_-_watch_out_for_non-uniform_resource_index
	Texture2D albedoTex = albedoTextures[NonUniformResourceIndex(material.albedoTextureIndex)];
	float3 albedo = albedoTex.SampleLevel(albedoSampler, texcoord, 0.0).rgb * material.albedoMultiplier.rgb;

	// Output payload
	
	payload.surfaceNormal = surfaceNormal;
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
